#include "vmm.h"
#include "pmm.h"
#include "../include/io.h"
#include "../libc/string.h"

#define PAGE_DIRECTORY_ENTRIES 1024
#define PAGE_TABLE_ENTRIES     1024
#define ENTRIES_PER_TABLE      1024

static page_directory_entry_t* current_directory;
static page_directory_entry_t* kernel_directory;

static page_table_entry_t* get_page_table(page_directory_entry_t* dir, uint32_t index, bool create) {
    if (dir[index].present) {
        return (page_table_entry_t*)(dir[index].table << 12);
    }

    if (!create) return NULL;

    page_table_entry_t* table = (page_table_entry_t*)pmm_alloc_page();
    if (!table) return NULL;

    memset(table, 0, sizeof(page_table_entry_t) * PAGE_TABLE_ENTRIES);
    dir[index].present = 1;
    dir[index].rw      = 1;
    dir[index].user    = 0;
    dir[index].table   = (uint32_t)table >> 12;

    return table;
}

void vmm_init(void) {
    kernel_directory = (page_directory_entry_t*)pmm_alloc_page();
    memset(kernel_directory, 0, sizeof(page_directory_entry_t) * PAGE_DIRECTORY_ENTRIES);

    for (uint32_t i = 0; i < 16; i++) {
        page_table_entry_t* table = (page_table_entry_t*)pmm_alloc_page();
        if (!table) continue;

        memset(table, 0, sizeof(page_table_entry_t) * PAGE_TABLE_ENTRIES);

        for (uint32_t j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            uint32_t phys = i * ENTRIES_PER_TABLE * PAGE_SIZE + j * PAGE_SIZE;
            table[j].present = 1;
            table[j].rw      = 1;
            table[j].user    = 0;
            table[j].frame   = phys >> 12;
        }

        kernel_directory[i].present = 1;
        kernel_directory[i].rw      = 1;
        kernel_directory[i].user    = 0;
        kernel_directory[i].table   = (uint32_t)table >> 12;
    }

    current_directory = kernel_directory;

    asm volatile("mov %0, %%cr3" : : "r"(kernel_directory));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    page_table_entry_t* table = get_page_table(current_directory, dir_index, true);
    if (!table) return;

    table[table_index].present = 1;
    table[table_index].rw      = (flags & 0x2) ? 1 : 0;
    table[table_index].user    = (flags & 0x4) ? 1 : 0;
    table[table_index].frame   = phys >> 12;

    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(uint32_t virt) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    page_table_entry_t* table = get_page_table(current_directory, dir_index, false);
    if (!table) return;

    table[table_index].present = 0;
    table[table_index].frame   = 0;

    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void* vmm_get_physical(uint32_t virt) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    page_table_entry_t* table = get_page_table(current_directory, dir_index, false);
    if (!table || !table[table_index].present) return NULL;

    return (void*)((table[table_index].frame << 12) | (virt & 0xFFF));
}

void vmm_switch_directory(page_directory_entry_t* dir) {
    current_directory = dir;
    asm volatile("mov %0, %%cr3" : : "r"(dir));
}

page_directory_entry_t* vmm_create_directory(void) {
    page_directory_entry_t* dir = (page_directory_entry_t*)pmm_alloc_page();
    if (!dir) return NULL;

    memset(dir, 0, sizeof(page_directory_entry_t) * PAGE_DIRECTORY_ENTRIES);

    for (uint32_t i = 0; i < 16; i++) {
        if (kernel_directory[i].present) {
            dir[i] = kernel_directory[i];
        }
    }

    return dir;
}

void vmm_free_directory(page_directory_entry_t* dir) {
    for (uint32_t i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        if (dir[i].present && dir[i].user) {
            page_table_entry_t* table = (page_table_entry_t*)(dir[i].table << 12);
            pmm_free_page(table);
        }
    }
    pmm_free_page(dir);
}
