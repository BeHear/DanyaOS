#include "ata.h"
#include "../include/io.h"
#include "../libc/string.h"
#include "../drivers/vga.h"

static ata_device_t devices[4];
static int ata_device_count = 0;

static void ata_delay(uint16_t io_base) {
    inb(io_base + 7);
    inb(io_base + 7);
    inb(io_base + 7);
    inb(io_base + 7);
}

static int ata_wait_ready(uint16_t io_base) {
    uint8_t status;
    for (int i = 0; i < 100000; i++) {
        status = inb(io_base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(uint16_t io_base) {
    uint8_t status;
    for (int i = 0; i < 100000; i++) {
        status = inb(io_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static void ata_select_drive(uint16_t io_base, uint8_t slave) {
    outb(io_base + ATA_REG_DRIVE, 0xA0 | (slave << 4));
    ata_delay(io_base);
}

static int ata_identify(uint16_t io_base, uint16_t ctrl_base, uint8_t slave, ata_device_t* dev) {
    ata_select_drive(io_base, slave);
    outb(io_base + ATA_REG_SECCOUNT, 0);
    outb(io_base + ATA_REG_LBA_LO, 0);
    outb(io_base + ATA_REG_LBA_MID, 0);
    outb(io_base + ATA_REG_LBA_HI, 0);
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    ata_delay(io_base);

    uint8_t status = inb(io_base + ATA_REG_STATUS);
    if (status == 0) return -1;

    if (ata_wait_ready(io_base) != 0) return -1;

    uint16_t ident[256];
    for (int i = 0; i < 256; i++) {
        ident[i] = inw(io_base + ATA_REG_DATA);
    }

    dev->io_base = io_base;
    dev->ctrl_base = ctrl_base;
    dev->slave = slave;
    dev->present = 1;
    dev->max_lba = ident[60] | ((uint32_t)ident[61] << 16);

    // Extract model string (words 27-46, 40 chars, byte-swapped)
    for (int i = 0; i < 20; i++) {
        dev->model[i * 2] = (char)(ident[27 + i] >> 8);
        dev->model[i * 2 + 1] = (char)(ident[27 + i] & 0xFF);
    }
    dev->model[40] = '\0';

    // Trim trailing spaces
    for (int i = 39; i >= 0; i--) {
        if (dev->model[i] == ' ' || dev->model[i] == '\0')
            dev->model[i] = '\0';
        else
            break;
    }

    return 0;
}

void ata_init(void) {
    ata_device_count = 0;
    memset(devices, 0, sizeof(devices));

    // Detect primary master
    if (ata_identify(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 0, &devices[0]) == 0) {
        ata_device_count++;
        vga_printf("  [ OK ] ATA: %s (%u MB)\n", devices[0].model,
                   (devices[0].max_lba * 512) / (1024 * 1024));
    }

    // Detect primary slave
    if (ata_identify(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 1, &devices[1]) == 0) {
        ata_device_count++;
        vga_printf("  [ OK ] ATA: %s (%u MB)\n", devices[1].model,
                   (devices[1].max_lba * 512) / (1024 * 1024));
    }

    // Detect secondary master
    if (ata_identify(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 0, &devices[2]) == 0) {
        ata_device_count++;
        vga_printf("  [ OK ] ATA: %s (%u MB)\n", devices[2].model,
                   (devices[2].max_lba * 512) / (1024 * 1024));
    }

    // Detect secondary slave
    if (ata_identify(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 1, &devices[3]) == 0) {
        ata_device_count++;
        vga_printf("  [ OK ] ATA: %s (%u MB)\n", devices[3].model,
                   (devices[3].max_lba * 512) / (1024 * 1024));
    }
}

static int ata_read_sector(uint16_t io_base, uint32_t lba, void* buf) {
    if (ata_wait_ready(io_base) != 0) return -1;

    outb(io_base + ATA_REG_FEATURES, 0);
    outb(io_base + ATA_REG_SECCOUNT, 1);
    outb(io_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(io_base + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    if (ata_wait_drq(io_base) != 0) return -1;

    uint16_t* ptr = (uint16_t*)buf;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(io_base + ATA_REG_DATA);
    }

    return 0;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void* buf) {
    ata_device_t* dev = ata_get_device();
    if (!dev) return -1;
    uint16_t io_base = dev->io_base;
    uint8_t* ptr = (uint8_t*)buf;

    for (uint32_t i = 0; i < count; i++) {
        if (ata_read_sector(io_base, lba + i, ptr + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ata_write_sector(uint16_t io_base, uint32_t lba, const void* buf) {
    if (ata_wait_ready(io_base) != 0) return -1;

    outb(io_base + ATA_REG_FEATURES, 0);
    outb(io_base + ATA_REG_SECCOUNT, 1);
    outb(io_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(io_base + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    if (ata_wait_drq(io_base) != 0) return -1;

    const uint16_t* ptr = (const uint16_t*)buf;
    for (int i = 0; i < 256; i++) {
        outw(io_base + ATA_REG_DATA, ptr[i]);
    }

    outb(io_base + ATA_REG_COMMAND, 0x00);
    ata_delay(io_base);

    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void* buf) {
    ata_device_t* dev = ata_get_device();
    if (!dev) return -1;
    uint16_t io_base = dev->io_base;
    const uint8_t* ptr = (const uint8_t*)buf;

    for (uint32_t i = 0; i < count; i++) {
        if (ata_write_sector(io_base, lba + i, ptr + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

ata_device_t* ata_get_device(void) {
    for (int i = 0; i < 4; i++) {
        if (devices[i].present) return &devices[i];
    }
    return NULL;
}
