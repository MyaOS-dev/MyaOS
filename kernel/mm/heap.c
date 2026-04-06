#include "heap.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define HEAP_MAGIC 0x4D59414F53484541ULL
#define HEAP_CLASS_COUNT 5u
#define HEAP_CLASS_LARGE 0xFFFFFFFFu

typedef struct heap_small_node {
    struct heap_small_node* next;
} heap_small_node_t;

typedef struct {
    uint32_t block_size;
    heap_small_node_t* free_list;
    uint64_t total_blocks;
    uint64_t used_blocks;
} heap_class_t;

typedef struct __attribute__((aligned(16))) {
    uint64_t magic;
    uint32_t class_index;
    uint32_t page_count;
    uint64_t requested_size;
} heap_alloc_header_t;

static uint64_t g_heap_page_count;
static uint64_t g_heap_alloc_count;
static uint64_t g_heap_bytes_used;
static heap_class_t g_heap_classes[HEAP_CLASS_COUNT];

static const uint32_t g_block_sizes[HEAP_CLASS_COUNT] = {
    64u,
    128u,
    256u,
    512u,
    1024u,
};

static uint64_t bytes_to_pages(uint64_t bytes) {
    return (bytes + PAGE_SIZE - 1ULL) / PAGE_SIZE;
}

static int class_index_for_size(uint64_t total_size) {
    for (uint32_t i = 0; i < HEAP_CLASS_COUNT; i++) {
        if (total_size <= g_heap_classes[i].block_size) {
            return (int)i;
        }
    }
    return -1;
}

static int refill_small_class(uint32_t class_index) {
    heap_class_t* cls;
    uint64_t base;
    uint32_t block_count;

    if (class_index >= HEAP_CLASS_COUNT) {
        return -1;
    }

    cls = &g_heap_classes[class_index];
    base = pmm_alloc_page();
    if (base == 0u) {
        return -1;
    }

    block_count = (uint32_t)(PAGE_SIZE / cls->block_size);
    if (block_count == 0u) {
        pmm_free_page(base);
        return -1;
    }

    for (uint32_t i = 0; i < block_count; i++) {
        uint64_t block_addr = base + (uint64_t)i * cls->block_size;
        heap_small_node_t* node = (heap_small_node_t*)(uintptr_t)block_addr;
        node->next = cls->free_list;
        cls->free_list = node;
    }

    cls->total_blocks += block_count;
    g_heap_page_count++;
    return 0;
}

void heap_init(void) {
    g_heap_page_count = 0;
    g_heap_alloc_count = 0;
    g_heap_bytes_used = 0;

    for (uint32_t i = 0; i < HEAP_CLASS_COUNT; i++) {
        g_heap_classes[i].block_size = g_block_sizes[i];
        g_heap_classes[i].free_list = NULL;
        g_heap_classes[i].total_blocks = 0;
        g_heap_classes[i].used_blocks = 0;
    }
}

void* kmalloc(size_t size) {
    heap_alloc_header_t* header;
    uint64_t total_size;
    int class_index;

    if (size == 0u) {
        return NULL;
    }

    total_size = (uint64_t)size + sizeof(heap_alloc_header_t);
    class_index = class_index_for_size(total_size);

    if (class_index >= 0) {
        heap_class_t* cls = &g_heap_classes[(uint32_t)class_index];
        heap_small_node_t* node;

        if (!cls->free_list && refill_small_class((uint32_t)class_index) != 0) {
            return NULL;
        }

        node = cls->free_list;
        if (!node) {
            return NULL;
        }
        cls->free_list = node->next;
        cls->used_blocks++;

        header = (heap_alloc_header_t*)(void*)node;
        header->magic = HEAP_MAGIC;
        header->class_index = (uint32_t)class_index;
        header->page_count = 0u;
        header->requested_size = (uint64_t)size;
    } else {
        uint64_t page_count = bytes_to_pages(total_size);
        uint64_t base = pmm_alloc_pages(page_count);

        if (base == 0u) {
            return NULL;
        }

        header = (heap_alloc_header_t*)(uintptr_t)base;
        header->magic = HEAP_MAGIC;
        header->class_index = HEAP_CLASS_LARGE;
        header->page_count = (uint32_t)page_count;
        header->requested_size = (uint64_t)size;
        g_heap_page_count += page_count;
    }

    g_heap_alloc_count++;
    g_heap_bytes_used += (uint64_t)size;
    return (void*)(header + 1);
}

void kfree(void* ptr) {
    heap_alloc_header_t* header;

    if (!ptr) {
        return;
    }

    header = ((heap_alloc_header_t*)ptr) - 1;
    if (header->magic != HEAP_MAGIC) {
        return;
    }

    if (header->class_index < HEAP_CLASS_COUNT) {
        heap_class_t* cls = &g_heap_classes[header->class_index];
        heap_small_node_t* node = (heap_small_node_t*)(void*)header;

        header->magic = 0u;
        node->next = cls->free_list;
        cls->free_list = node;
        if (cls->used_blocks > 0u) {
            cls->used_blocks--;
        }
    } else if (header->class_index == HEAP_CLASS_LARGE && header->page_count > 0u) {
        uint64_t page_count = header->page_count;

        pmm_free_pages((uint64_t)(uintptr_t)header, page_count);
        if (g_heap_page_count >= page_count) {
            g_heap_page_count -= page_count;
        } else {
            g_heap_page_count = 0u;
        }
        header->magic = 0u;
    } else {
        return;
    }

    if (g_heap_alloc_count > 0u) {
        g_heap_alloc_count--;
    }
    if (g_heap_bytes_used >= header->requested_size) {
        g_heap_bytes_used -= header->requested_size;
    } else {
        g_heap_bytes_used = 0u;
    }
}

void heap_get_stats(heap_stats_t* out) {
    if (!out) {
        return;
    }

    out->page_count = g_heap_page_count;
    out->alloc_count = g_heap_alloc_count;
    out->bytes_used = g_heap_bytes_used;
    out->bytes_capacity = g_heap_page_count * PAGE_SIZE;
}
