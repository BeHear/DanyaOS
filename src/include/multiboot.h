#ifndef DANYA_MULTIBOOT_H
#define DANYA_MULTIBOOT_H

#include "../include/types.h"

#define MULTIBOOT_MAGIC         0x1BADB002
#define MULTIBOOT2_MAGIC        0x36d76289
#define MULTIBOOT_BOOT_MAGIC    0x2BADB002
#define MULTIBOOT_MEM_INFO  0x00000002
#define MULTIBOOT_FLAGS     (MULTIBOOT_MEM_INFO)
#define MULTIBOOT_CHECKSUM  -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

#define MULTIBOOT_FLAG_MEM      0x1
#define MULTIBOOT_FLAG_BOOTDEV  0x2
#define MULTIBOOT_FLAG_CMDLINE  0x4
#define MULTIBOOT_FLAG_MODS     0x8
#define MULTIBOOT_FLAG_MMAP     0x20

typedef struct {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
} __attribute__((packed)) multiboot_header_t;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed)) multiboot_info_t;

typedef struct {
    uint32_t size;
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

#define MMAP_TYPE_USABLE 1

#endif
