#include "ne2000.h"
#include "../include/io.h"
#include "../kernel/idt.h"
#include "../drivers/vga.h"
#include "../drivers/timer.h"
#include "../libc/string.h"

static uint16_t io_base;
static uint8_t  mac_addr[6];

static inline void ne_outb(uint8_t reg, uint8_t val) { outb(io_base + reg, val); }
static inline uint8_t ne_inb(uint8_t reg) { return inb(io_base + reg); }

/* --- Remote DMA (ASIC port at 0x10) --- */
static void ne_dma_write(uint16_t nic_addr, const uint8_t* src, uint16_t len) {
    ne_outb(NE2000_REG_RSARLO, (uint8_t)(nic_addr & 0xFF));
    ne_outb(NE2000_REG_RSARHI, (uint8_t)(nic_addr >> 8));
    ne_outb(NE2000_REG_RCNTLO, (uint8_t)(len & 0xFF));
    ne_outb(NE2000_REG_RCNTHI, (uint8_t)(len >> 8));
    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_RD1 | NE2000_CR_PAGE0);
    uint16_t port = io_base + NE2000_REG_RDP;
    uint16_t words = (len + 1) / 2;
    for (uint16_t i = 0; i < words; i++) {
        uint16_t w = src[i * 2];
        if (i * 2 + 1 < len) w |= (uint16_t)src[i * 2 + 1] << 8;
        outw(port, w);
    }
    for (int i = 0; i < 100000; i++) {
        if (ne_inb(NE2000_REG_ISR) & NE2000_ISR_RDC) {
            ne_outb(NE2000_REG_ISR, NE2000_ISR_RDC);
            break;
        }
    }
}

static void ne_dma_read(uint16_t nic_addr, uint8_t* dst, uint16_t len) {
    ne_outb(NE2000_REG_RSARLO, (uint8_t)(nic_addr & 0xFF));
    ne_outb(NE2000_REG_RSARHI, (uint8_t)(nic_addr >> 8));
    ne_outb(NE2000_REG_RCNTLO, (uint8_t)(len & 0xFF));
    ne_outb(NE2000_REG_RCNTHI, (uint8_t)(len >> 8));
    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_RD0 | NE2000_CR_PAGE0);
    uint16_t port = io_base + NE2000_REG_RDP;
    uint16_t words = (len + 1) / 2;
    for (uint16_t i = 0; i < words; i++) {
        uint16_t w = inw(port);
        dst[i * 2] = (uint8_t)(w & 0xFF);
        if (i * 2 + 1 < len) dst[i * 2 + 1] = (uint8_t)(w >> 8);
    }
}

static void ne_irq_handler(stack_state_t* state) {
    UNUSED(state);
    uint8_t isr = ne_inb(NE2000_REG_ISR);
    ne_outb(NE2000_REG_ISR, isr);
}

void ne2000_init(void) {
    io_base = NE2000_IO_BASE;

    /* Software reset */
    inb(io_base + NE2000_REG_RST);
    for (volatile int i = 0; i < 10000; i++);

    /* Stop */
    ne_outb(NE2000_REG_CR, NE2000_CR_STP);
    for (volatile int i = 0; i < 1000; i++);
    /* Start */
    ne_outb(NE2000_REG_CR, NE2000_CR_STA);
    for (volatile int i = 0; i < 1000; i++);

    mac_addr[0]=0x52; mac_addr[1]=0x54; mac_addr[2]=0x00;
    mac_addr[3]=0x12; mac_addr[4]=0x34; mac_addr[5]=0x56;

    /* Page 1: set MAC (PAR at 0x11) */
    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_PAGE1);
    for (int i = 0; i < 6; i++)
        ne_outb(NE2000_REG_PAR0 + i, mac_addr[i]);
    ne_outb(NE2000_REG_CURR, NE2000_BUF_PAGE_START + 1);

    /* Page 0 */
    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_PAGE0);

    /* Buffer */
    ne_outb(NE2000_REG_PSTART, NE2000_BUF_PAGE_START);
    ne_outb(NE2000_REG_PSTOP,  NE2000_BUF_PAGE_STOP);
    ne_outb(NE2000_REG_BNDRY,  NE2000_BUF_PAGE_START);

    /* DCFG: word mode (WTS=1), auto-init, little-endian */
    ne_outb(NE2000_REG_DCFG, NE2000_DCFG_WTS | NE2000_DCFG_AR | NE2000_DCFG_LS);

    /* RCR: accept broadcast */
    ne_outb(NE2000_REG_RXCR, NE2000_RCR_AB);

    /* TXCR: normal */
    ne_outb(NE2000_REG_TXCR, 0x00);

    /* Clear all interrupts */
    ne_outb(NE2000_REG_ISR, 0xFF);

    /* IMR: enable RX, TX, RDC */
    ne_outb(NE2000_REG_IMR, NE2000_ISR_PRX | NE2000_ISR_PTX | NE2000_ISR_RDC);

    /* Register IRQ */
    idt_register_handler(NE2000_IRQ + 32, ne_irq_handler);
    uint8_t mask = inb(0x21);
    mask &= ~(1 << NE2000_IRQ);
    outb(0x21, mask);

    vga_printf("  [ OK ] NE2000: MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
               mac_addr[0], mac_addr[1], mac_addr[2],
               mac_addr[3], mac_addr[4], mac_addr[5]);
}

void ne2000_send(const uint8_t* buf, uint16_t len) {
    if (len > NE2000_MTU) len = NE2000_MTU;

    /* Write packet to NIC RAM at TX_PAGE via remote DMA */
    uint16_t tx_offset = NE2000_TX_PAGE * NE2000_PAGE_SIZE;
    ne_dma_write(tx_offset, buf, len);

    /* Set TPSR (offset 0x04) and TX byte count (offsets 0x05/0x06) */
    ne_outb(NE2000_REG_TPSR, NE2000_TX_PAGE);
    ne_outb(NE2000_REG_TCNTLO, (uint8_t)(len & 0xFF));
    ne_outb(NE2000_REG_TCNTHI, (uint8_t)(len >> 8));

    /* Trigger TX: set TXP bit (bit 2) */
    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_TXP | NE2000_CR_PAGE0);

    /* Wait for TX complete (PTX in ISR) */
    for (int i = 0; i < 100000; i++) {
        if (ne_inb(NE2000_REG_ISR) & NE2000_ISR_PTX) {
            ne_outb(NE2000_REG_ISR, NE2000_ISR_PTX);
            return;
        }
    }
}

int ne2000_poll(ne2000_rx_packet_t* pkt) {
    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_PAGE0);
    uint8_t bndry = ne_inb(NE2000_REG_BNDRY);

    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_PAGE1);
    uint8_t curr = ne_inb(NE2000_REG_CURR);
    ne_outb(NE2000_REG_CR, NE2000_CR_STA | NE2000_CR_PAGE0);

    if (curr == bndry) return 0;

    uint8_t page = bndry + 1;
    if (page >= NE2000_BUF_PAGE_STOP) page = NE2000_BUF_PAGE_START;

    uint16_t nic_addr = page * NE2000_PAGE_SIZE;
    uint8_t hdr[4];
    ne_dma_read(nic_addr, hdr, 4);

    uint16_t pkt_len = hdr[2] | ((uint16_t)hdr[3] << 8);
    uint8_t  next_pge = hdr[1];

    if (pkt_len < 4) { pkt->len = 0; ne_outb(NE2000_REG_BNDRY, page); return 0; }
    uint16_t data_len = pkt_len - 4;
    if (data_len > NE2000_MTU) data_len = NE2000_MTU;

    if (data_len > 0) ne_dma_read(nic_addr + 4, pkt->data, data_len);
    pkt->len = data_len;

    uint8_t new_bndry = (next_pge >= NE2000_BUF_PAGE_START && next_pge < NE2000_BUF_PAGE_STOP)
                        ? (next_pge - 1) : (curr - 1);
    ne_outb(NE2000_REG_BNDRY, new_bndry);
    return 1;
}

void ne2000_get_mac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = mac_addr[i];
}
