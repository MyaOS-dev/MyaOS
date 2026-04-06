#ifndef PAGING_H
#define PAGING_H

#include "boot.h"
#include <stdint.h>

typedef struct {
    uint64_t mapped_bytes;
    uint64_t table_pages;
    uint64_t cr3;
} paging_stats_t;

int paging_init(const boot_info_t* boot);
uint64_t paging_kernel_cr3(void);
uint64_t paging_current_cr3(void);
int paging_switch_to(uint64_t cr3);
int paging_space_create(uint64_t* out_cr3);
void paging_space_destroy(uint64_t cr3);
int paging_alloc_user_range(uint64_t virt_addr, uint64_t size, uint8_t writable);
void paging_free_user_range(uint64_t virt_addr, uint64_t size);
int paging_user_range_accessible(uint64_t virt_addr, uint64_t size, uint8_t writable);
void paging_get_stats(paging_stats_t* out);

#endif
