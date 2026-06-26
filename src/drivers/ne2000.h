#ifndef DANYA_NE2000_H
#define DANYA_NE2000_H

#include "../include/types.h"

#define NE2000_IO_BASE 0x300
#define NE2000_IRQ     3

/* Page 0 register offsets (matches QEMU ne2000.c) */
#define NE2000_REG_CR       0x00  /* Command Register */
#define NE2000_REG_PSTART   0x01  /* Page Start (W) / CLDALO (R) */
#define NE2000_REG_PSTOP    0x02  /* Page Stop (W) / CLDAHI (R) */
#define NE2000_REG_BNDRY    0x03  /* Boundary */
#define NE2000_REG_TPSR     0x04  /* TX Page Start (W) / TSR (R) */
#define NE2000_REG_TCNTLO   0x05  /* TX Byte Count Low (W) / NCR (R) */
#define NE2000_REG_TCNTHI   0x06  /* TX Byte Count High (W) / FIFO (R) */
#define NE2000_REG_ISR      0x07  /* Interrupt Status (R/W) */
#define NE2000_REG_RSARLO   0x08  /* Remote Start Addr Low (W) / CRDALO (R) */
#define NE2000_REG_RSARHI   0x09  /* Remote Start Addr High (W) / CRDAHI (R) */
#define NE2000_REG_RCNTLO   0x0A  /* Remote Byte Count Low (W) */
#define NE2000_REG_RCNTHI   0x0B  /* Remote Byte Count High (W) */
#define NE2000_REG_RXCR     0x0C  /* RX Config (W) / RSR (R) */
#define NE2000_REG_TXCR     0x0D  /* TX Config (W) / Counter0 (R) */
#define NE2000_REG_DCFG     0x0E  /* Data Config (W) / Counter1 (R) */
#define NE2000_REG_IMR      0x0F  /* Interrupt Mask (W) / Counter2 (R) */
#define NE2000_REG_RDP      0x10  /* Remote DMA Port (ASIC) */
#define NE2000_REG_RST      0x1F  /* Reset (R) */

/* Page 1 registers (QEMU adds page<<4 internally, use raw I/O addresses) */
#define NE2000_REG_PAR0     0x01  /* Physical Address 0-5 (I/O 0x01-0x06 on page 1) */
#define NE2000_REG_CURR     0x07  /* Current Page Register (I/O 0x07 on page 1) */
#define NE2000_REG_MAR0     0x08  /* Multicast Address 0-7 (I/O 0x08-0x0F on page 1) */

/* CR bits */
#define NE2000_CR_STP       0x01
#define NE2000_CR_STA       0x02
#define NE2000_CR_TXP       0x04
#define NE2000_CR_RD0       0x08
#define NE2000_CR_RD1       0x10
#define NE2000_CR_NODMA     0x20
#define NE2000_CR_PAGE0     0x00
#define NE2000_CR_PAGE1     0x40

/* ISR bits */
#define NE2000_ISR_PRX      0x01
#define NE2000_ISR_PTX      0x02
#define NE2000_ISR_RXE      0x04
#define NE2000_ISR_TXE      0x08
#define NE2000_ISR_OVW      0x10
#define NE2000_ISR_RDC      0x40
#define NE2000_ISR_RST      0x80

/* DCFG bits */
#define NE2000_DCFG_WTS     0x01  /* Word Transfer Select */
#define NE2000_DCFG_BOS     0x02  /* Byte Order Select (1=little-endian/x86) */
#define NE2000_DCFG_AR      0x10  /* Auto-Init Remote */
#define NE2000_DCFG_LS      0x20  /* Loopback Select */

/* RCR bits */
#define NE2000_RCR_AB       0x04  /* Accept Broadcast */
#define NE2000_RCR_AM       0x08  /* Accept Multicast */

/* Buffer layout */
#define NE2000_BUF_PAGE_START 0x4C
#define NE2000_BUF_PAGE_STOP  0x80
#define NE2000_TX_PAGE        0x40
#define NE2000_PAGE_SIZE      256
#define NE2000_MTU            1500

typedef struct {
    uint8_t data[NE2000_MTU];
    uint16_t len;
} ne2000_rx_packet_t;

void ne2000_init(void);
void ne2000_send(const uint8_t* buf, uint16_t len);
int  ne2000_poll(ne2000_rx_packet_t* pkt);
void ne2000_get_mac(uint8_t* mac);

#endif
