#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define EFI_CONVENTIONAL_MEMORY 7u
#define PMM_MAX_TRACKED_PAGES 1048576u
#define PMM_MAX_PHYS_ADDR (4ULL * 1024ULL * 1024ULL * 1024ULL)

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

static uint64_t g_free_pages[PMM_MAX_TRACKED_PAGES];
static uint64_t g_free_top;
static uint64_t g_allocated_pages;
static uint64_t g_managed_pages;

static uint64_t align_down(uint64_t v, uint64_t a) {
    return v & ~(a - 1ULL);
}

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1ULL) & ~(a - 1ULL);
}

static int ranges_overlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) {
    if (a0 >= a1 || b0 >= b1) {
        return 0;
    }
    return a0 < b1 && b0 < a1;
}

static int page_is_reserved(
    uint64_t page_addr,
    uint64_t kernel_start,
    uint64_t kernel_end,
    uint64_t fb_start,
    uint64_t fb_end,
    uint64_t disk_start,
    uint64_t disk_end,
    uint64_t mmap_start,
    uint64_t mmap_end
) {
    uint64_t page_end = page_addr + PAGE_SIZE;

    if (page_end > PMM_MAX_PHYS_ADDR) {
        return 1;
    }
    if (page_addr < 0x100000ULL) {
        return 1;
    }
    if (ranges_overlap(page_addr, page_end, kernel_start, kernel_end)) {
        return 1;
    }
    if (ranges_overlap(page_addr, page_end, fb_start, fb_end)) {
        return 1;
    }
    if (ranges_overlap(page_addr, page_end, disk_start, disk_end)) {
        return 1;
    }
    if (ranges_overlap(page_addr, page_end, mmap_start, mmap_end)) {
        return 1;
    }
    return 0;
}

void pmm_init(const boot_info_t* boot, uint64_t kernel_start, uint64_t kernel_end) {
    g_free_top = 0;
    g_allocated_pages = 0;
    g_managed_pages = 0;

    if (!boot || boot->mmap == 0 || boot->desc_size == 0) {
        return;
    }

    uint64_t kernel_reserved_start = align_down(kernel_start, PAGE_SIZE);
    uint64_t kernel_reserved_end = align_up(kernel_end, PAGE_SIZE);
    uint64_t fb_start = boot->fb.base;
    uint64_t fb_end = boot->fb.base + boot->fb.size;
    uint64_t disk_start = boot->boot_disk_base;
    uint64_t disk_end = boot->boot_disk_base + boot->boot_disk_size;
    uint64_t mmap_start = boot->mmap;
    uint64_t mmap_end = boot->mmap + boot->mmap_size;

    const uint8_t* mmap = (const uint8_t*)(uintptr_t)boot->mmap;
    uint64_t offset = 0;

    while (offset + boot->desc_size <= boot->mmap_size) {
        const efi_memory_descriptor_t* desc =
            (const efi_memory_descriptor_t*)(mmap + offset);

        if (desc->type == EFI_CONVENTIONAL_MEMORY) {
            uint64_t region = desc->physical_start;
            for (uint64_t page = 0; page < desc->number_of_pages; page++) {
                uint64_t page_addr = region + page * PAGE_SIZE;
                if (page_is_reserved(
                        page_addr,
                        kernel_reserved_start,
                        kernel_reserved_end,
                        fb_start,
                        fb_end,
                        disk_start,
                        disk_end,
                        mmap_start,
                        mmap_end
                    )) {
                    continue;
                }

                if (g_free_top >= PMM_MAX_TRACKED_PAGES) {
                    break;
                }

                g_free_pages[g_free_top++] = page_addr;
                g_managed_pages++;
            }
        }

        offset += boot->desc_size;
    }
}

uint64_t pmm_alloc_page(void) {
    if (g_free_top == 0) {
        return 0;
    }

    uint64_t best_idx = 0;
    uint64_t page_addr = g_free_pages[0];
    for (uint64_t i = 1; i < g_free_top; i++) {
        if (g_free_pages[i] < page_addr) {
            page_addr = g_free_pages[i];
            best_idx = i;
        }
    }

    g_free_top--;
    g_free_pages[best_idx] = g_free_pages[g_free_top];
    g_allocated_pages++;
    return page_addr;
}

void pmm_free_page(uint64_t page_addr) {
    if (page_addr == 0 || g_free_top >= PMM_MAX_TRACKED_PAGES) {
        return;
    }

    g_free_pages[g_free_top++] = align_down(page_addr, PAGE_SIZE);
    if (g_allocated_pages > 0) {
        g_allocated_pages--;
    }
}

void pmm_get_stats(pmm_stats_t* out) {
    if (!out) {
        return;
    }

    out->managed_pages = g_managed_pages;
    out->free_pages = g_free_top;
    out->allocated_pages = g_allocated_pages;
}
