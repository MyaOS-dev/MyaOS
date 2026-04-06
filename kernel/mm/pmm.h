#ifndef PMM_H
#define PMM_H

#include "boot.h"
#include <stdint.h>

typedef struct {
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
} pmm_stats_t;

void pmm_init(const boot_info_t* boot, uint64_t kernel_start, uint64_t kernel_end);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(uint64_t count);
void pmm_free_page(uint64_t page_addr);
void pmm_free_pages(uint64_t page_addr, uint64_t count);
void pmm_get_stats(pmm_stats_t* out);

#endif
