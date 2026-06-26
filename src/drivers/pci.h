#ifndef DANYA_PCI_H
#define DANYA_PCI_H

#include "../include/types.h"

#define PCI_MAX_BUSES     256
#define PCI_MAX_DEVICES   32
#define PCI_MAX_FUNCTIONS 8
#define PCI_MAX_DEVICES_FOUND 128

// PCI config address port
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

// Standard PCI header offsets
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS           0x0B
#define PCI_CACHE_LINE      0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BIST            0x0F
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D
#define PCI_SECONDARY_BUS   0x19  // for bridges

// Header types
#define PCI_HEADER_DEVICE     0x00
#define PCI_HEADER_BRIDGE     0x01
#define PCI_HEADER_CARDBUS    0x02
#define PCI_HEADER_MULTIFUNC  0x80

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision_id;
    uint8_t  header_type;
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
} pci_device_t;

// Known vendor name table
typedef struct {
    uint16_t vendor_id;
    const char* name;
} pci_vendor_t;

void      pci_init(void);
uint32_t  pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
int       pci_device_check(uint8_t bus, uint8_t dev, uint8_t func);
void      pci_scan_bus(uint8_t bus);
void      pci_scan_all(void);
int       pci_device_count(void);
const pci_device_t* pci_get_device(int index);
const char* pci_vendor_name(uint16_t vendor_id);
const char* pci_class_name(uint8_t class_code, uint8_t subclass);

#endif
