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
void paging_get_stats(paging_stats_t* out);

#endif
