#include "pmm.h"
#include "../include/io.h"
#include "../libc/string.h"
#include "../drivers/vga.h"

#define BITMAP_SIZE 8192

static uint8_t pmm_bitmap[BITMAP_SIZE];
static uint32_t total_pages;
static uint32_t used_pages;

static inline void bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint32_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline bool bitmap_test(uint32_t bit) {
    return pmm_bitmap[bit / 8] & (1 << (bit % 8));
}

void pmm_init(uint32_t total_memory) {
    total_pages = total_memory / PAGE_SIZE;
    if (total_pages > BITMAP_SIZE * 8)
        total_pages = BITMAP_SIZE * 8;

    memset(pmm_bitmap, 0x00, BITMAP_SIZE);

    extern uint32_t __kernel_end;
    uint32_t kernel_end = (uint32_t)&__kernel_end;
    uint32_t reserved_pages = (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < reserved_pages; i++) {
        bitmap_set(i);
    }

    uint32_t bitmap_pages = (BITMAP_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < bitmap_pages; i++) {
        bitmap_set(i);
    }

    uint32_t vga_buffer_start = 0xB8000 / PAGE_SIZE;
    uint32_t vga_buffer_end = (0xB8000 + VGA_WIDTH * VGA_HEIGHT * 2 + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = vga_buffer_start; i < vga_buffer_end; i++) {
        bitmap_set(i);
    }

    used_pages = 0;
    for (uint32_t i = 0; i < total_pages; i++) {
        if (bitmap_test(i)) used_pages++;
    }
}

void* pmm_alloc_page(void) {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free_page(void* page) {
    uint32_t index = (uint32_t)page / PAGE_SIZE;
    if (index < total_pages && bitmap_test(index)) {
        bitmap_clear(index);
        used_pages--;
    }
}

uint32_t pmm_get_free_count(void) {
    return total_pages - used_pages;
}

uint32_t pmm_get_total_count(void) {
    return total_pages;
}
