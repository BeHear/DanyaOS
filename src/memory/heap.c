#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "../libc/string.h"

#define HEAP_START  0x00400000
#define HEAP_END    0x01000000

typedef struct block {
    size_t size;
    bool   free;
    struct block* next;
    struct block* prev;
    uint32_t magic;
} block_t;

#define BLOCK_MAGIC 0xDEADBEEF
#define BLOCK_SIZE  sizeof(block_t)
#define ALIGN4(x)   (((x) + 3) & ~3)

static block_t* heap_head = NULL;
static uint32_t heap_current = 0;

static block_t* find_free_block(size_t size) {
    block_t* current = heap_head;
    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static block_t* expand_heap(size_t size) {
    uint32_t needed = BLOCK_SIZE + size;
    uint32_t pages = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t alloc_base = heap_current;

    for (uint32_t i = 0; i < pages; i++) {
        void* page = pmm_alloc_page();
        if (!page) return NULL;
        vmm_map_page(heap_current, (uint32_t)page, 0x3);
        memset((void*)heap_current, 0, PAGE_SIZE);
        heap_current += PAGE_SIZE;
    }

    block_t* block = (block_t*)(alloc_base);
    block->size = size;
    block->free = false;
    block->magic = BLOCK_MAGIC;
    block->next = NULL;
    block->prev = NULL;

    if (heap_head) {
        block_t* last = heap_head;
        while (last->next) last = last->next;
        last->next = block;
        block->prev = last;
    } else {
        heap_head = block;
    }

    return block;
}

void heap_init(void) {
    heap_current = HEAP_START;
    heap_head = NULL;

    uint32_t heap_size = 256 * 1024;
    uint32_t pages_needed = (heap_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = 0; i < pages_needed; i++) {
        void* page = pmm_alloc_page();
        if (!page) break;
        vmm_map_page(heap_current, (uint32_t)page, 0x3);
        heap_current += PAGE_SIZE;
    }

    block_t* first = (block_t*)HEAP_START;
    first->size = heap_size - BLOCK_SIZE;
    first->free = true;
    first->magic = BLOCK_MAGIC;
    first->next = NULL;
    first->prev = NULL;
    heap_head = first;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN4(size);

    block_t* block = find_free_block(size);
    if (block) {
        if (block->size >= size + BLOCK_SIZE + 4) {
            block_t* new_block = (block_t*)((uint32_t)block + BLOCK_SIZE + size);
            new_block->size = block->size - size - BLOCK_SIZE;
            new_block->free = true;
            new_block->magic = BLOCK_MAGIC;
            new_block->next = block->next;
            new_block->prev = block;

            if (block->next) block->next->prev = new_block;
            block->next = new_block;
            block->size = size;
        }
        block->free = false;
        return (void*)((uint32_t)block + BLOCK_SIZE);
    }

    block = expand_heap(size);
    if (!block) return NULL;
    return (void*)((uint32_t)block + BLOCK_SIZE);
}

void* kmalloc_aligned(size_t size) {
    if (size == 0) return NULL;
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    uint32_t base = heap_current;
    for (uint32_t i = 0; i < pages; i++) {
        void* page = pmm_alloc_page();
        if (!page) return NULL;
        vmm_map_page(heap_current, (uint32_t)page, 0x3);
        memset((void*)heap_current, 0, PAGE_SIZE);
        heap_current += PAGE_SIZE;
    }
    return (void*)base;
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_t* block = (block_t*)((uint32_t)ptr - BLOCK_SIZE);
    if (block->magic != BLOCK_MAGIC) return;

    block->free = true;

    if (block->next && block->next->free) {
        block->size += BLOCK_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    if (block->prev && block->prev->free) {
        block->prev->size += BLOCK_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }

    block_t* block = (block_t*)((uint32_t)ptr - BLOCK_SIZE);
    if (block->magic != BLOCK_MAGIC) return NULL;

    if (block->size >= size) return ptr;

    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}
