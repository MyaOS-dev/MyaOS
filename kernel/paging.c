#include "paging.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define PAGE_PRESENT 0x001ULL
#define PAGE_RW 0x002ULL
#define PAGE_PS 0x080ULL
#define GIB (1024ULL * 1024ULL * 1024ULL)
#define MIB2 (2ULL * 1024ULL * 1024ULL)
#define MAX_IDENTITY_MAP_BYTES (512ULL * GIB)

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

static paging_stats_t g_stats;

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1ULL) & ~(a - 1ULL);
}

static void mem_zero(void* ptr, size_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static inline void load_cr3(uint64_t value) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(value) : "memory");
}

static uint64_t* alloc_table_page(uint64_t* phys_out) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        return NULL;
    }

    uint64_t* page = (uint64_t*)(uintptr_t)phys;
    mem_zero(page, PAGE_SIZE);
    if (phys_out) {
        *phys_out = phys;
    }
    g_stats.table_pages++;
    return page;
}

static uint64_t find_max_phys(const boot_info_t* boot) {
    uint64_t max_phys = 0;
    if (!boot || boot->mmap == 0 || boot->desc_size == 0) {
        return 0;
    }

    const uint8_t* mmap = (const uint8_t*)(uintptr_t)boot->mmap;
    uint64_t off = 0;
    while (off + boot->desc_size <= boot->mmap_size) {
        const efi_memory_descriptor_t* desc =
            (const efi_memory_descriptor_t*)(mmap + off);
        uint64_t end = desc->physical_start + desc->number_of_pages * PAGE_SIZE;
        if (end > max_phys) {
            max_phys = end;
        }
        off += boot->desc_size;
    }

    uint64_t fb_end = boot->fb.base + boot->fb.size;
    if (fb_end > max_phys) {
        max_phys = fb_end;
    }

    uint64_t disk_end = boot->boot_disk_base + boot->boot_disk_size;
    if (disk_end > max_phys) {
        max_phys = disk_end;
    }

    return max_phys;
}

int paging_init(const boot_info_t* boot) {
    g_stats.mapped_bytes = 0;
    g_stats.table_pages = 0;
    g_stats.cr3 = 0;

    uint64_t max_phys = find_max_phys(boot);
    if (max_phys < 4ULL * GIB) {
        max_phys = 4ULL * GIB;
    }

    uint64_t map_limit = align_up(max_phys, GIB);
    if (map_limit == 0) {
        return -1;
    }
    if (map_limit > MAX_IDENTITY_MAP_BYTES) {
        map_limit = MAX_IDENTITY_MAP_BYTES;
    }

    uint64_t pdpt_entries = map_limit / GIB;
    if (pdpt_entries == 0 || pdpt_entries > 512) {
        return -2;
    }

    uint64_t pml4_phys = 0;
    uint64_t pdpt_phys = 0;
    uint64_t* pml4 = alloc_table_page(&pml4_phys);
    uint64_t* pdpt = alloc_table_page(&pdpt_phys);
    if (!pml4 || !pdpt) {
        return -3;
    }

    pml4[0] = pdpt_phys | PAGE_PRESENT | PAGE_RW;

    uint64_t page_index = 0;
    for (uint64_t i = 0; i < pdpt_entries; i++) {
        uint64_t pd_phys = 0;
        uint64_t* pd = alloc_table_page(&pd_phys);
        if (!pd) {
            return -4;
        }

        pdpt[i] = pd_phys | PAGE_PRESENT | PAGE_RW;

        for (uint64_t j = 0; j < 512; j++) {
            uint64_t page_addr = page_index * MIB2;
            pd[j] = page_addr | PAGE_PRESENT | PAGE_RW | PAGE_PS;
            page_index++;
        }
    }

    load_cr3(pml4_phys);

    g_stats.cr3 = pml4_phys;
    g_stats.mapped_bytes = map_limit;
    return 0;
}

void paging_get_stats(paging_stats_t* out) {
    if (!out) {
        return;
    }
    *out = g_stats;
}
