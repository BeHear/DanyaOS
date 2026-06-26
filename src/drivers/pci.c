#include "pci.h"
#include "../include/io.h"
#include "../libc/string.h"
#include "../drivers/vga.h"

static pci_device_t devices[PCI_MAX_DEVICES_FOUND];
static int device_count = 0;

// Known PCI vendor IDs
static const pci_vendor_t vendor_table[] = {
    {0x0000, "Unknown"},
    {0x0010, "Allied Telesis"},
    {0x1013, "Cirrus Logic"},
    {0x1014, "IBM"},
    {0x1022, "AMD"},
    {0x1023, "Trident"},
    {0x102B, "Matrox"},
    {0x1033, "NEC"},
    {0x1039, "SiS"},
    {0x103C, "HP"},
    {0x1043, "ASUSTeK"},
    {0x104A, "STMicroelectronics"},
    {0x104C, "TI"},
    {0x1050, "Winbond"},
    {0x1054, "Hitachi"},
    {0x105A, "Promise"},
    {0x106B, "Apple"},
    {0x1078, "Cyrix"},
    {0x1080, "Cyrix/NS"},
    {0x10B7, "3Com"},
    {0x10DE, "NVIDIA"},
    {0x10EC, "Realtek"},
    {0x1106, "VIA"},
    {0x1112, "FIC"},
    {0x1186, "D-Link"},
    {0x11AB, "Marvell"},
    {0x11AD, "Netgear"},
    {0x11C1, "Lucent/Agere"},
    {0x11D4, "Adaptec"},
    {0x1200, "Compaq/HP"},
    {0x1274, "Ensoniq"},
    {0x1282, "Davicom"},
    {0x1412, "IC Ensemble"},
    {0x14E4, "Broadcom"},
    {0x15AD, "VMware"},
    {0x168C, "Qualcomm Atheros"},
    {0x18C9, "ARVOO"},
    {0x1AF4, "Red Hat/QEMU"},
    {0x8086, "Intel"},
    {0x8087, "Intel (2)"},
    {0x80EE, "VirtualBox"},
    {0xFFFF, NULL} // sentinel
};

// Class -> description mapping
static const char* pci_get_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "Generic System";
        case 0x09: return "Input Device";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Data Acquisition";
        case 0x12: return "Processing Accelerator";
        case 0x13: return "Non-Essential Instrumentation";
        case 0x40: return "Co-Processor";
        case 0xFF: return "Vendor Specific";
        default:   return "Other";
    }
}

const char* pci_class_name(uint8_t class_code, uint8_t subclass) {
    UNUSED(subclass);
    return pci_get_class_name(class_code);
}

const char* pci_vendor_name(uint16_t vendor_id) {
    for (int i = 0; vendor_table[i].name != NULL; i++) {
        if (vendor_table[i].vendor_id == vendor_id) {
            return vendor_table[i].name;
        }
    }
    return "Unknown";
}

uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)(((uint32_t)bus << 16) |
                                   ((uint32_t)dev << 11) |
                                   ((uint32_t)func << 8) |
                                   (offset & 0xFC) |
                                   0x80000000);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

// Check if device exists at bus:dev.func
int pci_device_check(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t vendor = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
    return (vendor != 0xFFFFFFFF) ? 1 : 0;
}

// Scan a single function
static void pci_add_device(uint8_t bus, uint8_t dev, uint8_t func) {
    if (device_count >= PCI_MAX_DEVICES_FOUND) return;

    uint32_t id   = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
    uint32_t rev  = pci_config_read(bus, dev, func, PCI_REVISION_ID);
    uint32_t head = pci_config_read(bus, dev, func, PCI_HEADER_TYPE);
    uint32_t irq  = pci_config_read(bus, dev, func, PCI_INTERRUPT_LINE);

    pci_device_t* d = &devices[device_count];
    d->bus    = bus;
    d->device = dev;
    d->func   = func;
    d->vendor_id  = (uint16_t)(id & 0xFFFF);
    d->device_id  = (uint16_t)((id >> 16) & 0xFFFF);
    d->class_code = (uint8_t)((rev >> 16) & 0xFF);
    d->subclass   = (uint8_t)((rev >> 8) & 0xFF);
    d->prog_if    = (uint8_t)(rev & 0xFF);
    d->revision_id = (uint8_t)((rev >> 24) & 0xFF);
    d->header_type = (uint8_t)(head & 0x7F);
    d->interrupt_line = (uint8_t)(irq & 0xFF);
    d->interrupt_pin  = (uint8_t)((irq >> 8) & 0xFF);

    device_count++;
}

// Scan all functions of a device
static void pci_scan_device(uint8_t bus, uint8_t dev) {
    if (!pci_device_check(bus, dev, 0)) return;

    pci_add_device(bus, dev, 0);

    // Check if multi-function device
    uint32_t head = pci_config_read(bus, dev, 0, PCI_HEADER_TYPE);
    if (head & PCI_HEADER_MULTIFUNC) {
        for (uint8_t func = 1; func < PCI_MAX_FUNCTIONS; func++) {
            if (pci_device_check(bus, dev, func)) {
                pci_add_device(bus, dev, func);
            }
        }
    }
}

void pci_scan_bus(uint8_t bus) {
    for (uint8_t dev = 0; dev < PCI_MAX_DEVICES; dev++) {
        pci_scan_device(bus, dev);
    }
}

void pci_scan_all(void) {
    device_count = 0;

    // Scan bus 0
    pci_scan_bus(0);

    // Check for PCI-PCI bridges on bus 0
    for (int i = 0; i < device_count; i++) {
        if (devices[i].class_code == 0x06 && devices[i].subclass == 0x04) {
            // PCI-PCI bridge found, scan secondary bus
            // Read secondary bus number from bridge config
            uint32_t sec = pci_config_read(devices[i].bus, devices[i].device,
                                           devices[i].func, PCI_SECONDARY_BUS);
            uint8_t secondary_bus = (uint8_t)((sec >> 8) & 0xFF);
            if (secondary_bus != 0) {
                pci_scan_bus(secondary_bus);
            }
        }
    }
}

void pci_init(void) {
    vga_puts("  [ OK ] PCI: scanning...\n");
    pci_scan_all();
    vga_printf("  [ OK ] PCI: %d devices found\n", device_count);
}

int pci_device_count(void) {
    return device_count;
}

const pci_device_t* pci_get_device(int index) {
    if (index < 0 || index >= device_count) return NULL;
    return &devices[index];
}
