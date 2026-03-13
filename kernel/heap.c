#include "heap.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define KMALLOC_ALIGN 16ULL

typedef struct heap_page_header {
    struct heap_page_header* next;
    uint32_t used;
    uint32_t capacity;
} heap_page_header_t;

static heap_page_header_t* g_heap_head;
static heap_page_header_t* g_heap_tail;
static uint64_t g_heap_page_count;
static uint64_t g_heap_alloc_count;
static uint64_t g_heap_bytes_used;

static size_t align_up_size(size_t value, size_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static heap_page_header_t* heap_new_page(void) {
    uint64_t page_addr = pmm_alloc_page();
    if (page_addr == 0) {
        return NULL;
    }

    heap_page_header_t* page = (heap_page_header_t*)(uintptr_t)page_addr;
    page->next = NULL;
    page->used = 0;
    page->capacity = (uint32_t)(PAGE_SIZE - sizeof(heap_page_header_t));

    if (!g_heap_head) {
        g_heap_head = page;
        g_heap_tail = page;
    } else {
        g_heap_tail->next = page;
        g_heap_tail = page;
    }

    g_heap_page_count++;
    return page;
}

void heap_init(void) {
    g_heap_head = NULL;
    g_heap_tail = NULL;
    g_heap_page_count = 0;
    g_heap_alloc_count = 0;
    g_heap_bytes_used = 0;
    (void)heap_new_page();
}

void* kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size = align_up_size(size, KMALLOC_ALIGN);
    if (size > PAGE_SIZE - sizeof(heap_page_header_t)) {
        return NULL;
    }

    if (!g_heap_head && !heap_new_page()) {
        return NULL;
    }

    heap_page_header_t* page = g_heap_head;
    while (page) {
        uint32_t free_bytes = page->capacity - page->used;
        if (free_bytes >= size) {
            uint8_t* base = (uint8_t*)page + sizeof(heap_page_header_t);
            void* out = (void*)(base + page->used);
            page->used += (uint32_t)size;
            g_heap_alloc_count++;
            g_heap_bytes_used += size;
            return out;
        }
        page = page->next;
    }

    page = heap_new_page();
    if (!page) {
        return NULL;
    }

    uint8_t* base = (uint8_t*)page + sizeof(heap_page_header_t);
    page->used = (uint32_t)size;
    g_heap_alloc_count++;
    g_heap_bytes_used += size;
    return (void*)base;
}

void kfree(void* ptr) {
    (void)ptr;
}

void heap_get_stats(heap_stats_t* out) {
    if (!out) {
        return;
    }

    out->page_count = g_heap_page_count;
    out->alloc_count = g_heap_alloc_count;
    out->bytes_used = g_heap_bytes_used;
    out->bytes_capacity = g_heap_page_count * (PAGE_SIZE - sizeof(heap_page_header_t));
}
