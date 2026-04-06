#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t page_count;
    uint64_t alloc_count;
    uint64_t bytes_used;
    uint64_t bytes_capacity;
} heap_stats_t;

void heap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void heap_get_stats(heap_stats_t* out);

#endif
