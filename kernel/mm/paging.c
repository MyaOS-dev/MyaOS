#include "paging.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define PAGE_PRESENT 0x001ULL
#define PAGE_RW 0x002ULL
#define PAGE_USER 0x004ULL
#define PAGE_PS 0x080ULL
#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define GIB (1024ULL * 1024ULL * 1024ULL)
#define MIB2 (2ULL * 1024ULL * 1024ULL)
#define MAX_IDENTITY_MAP_BYTES (512ULL * GIB)
#define USER_PML4_INDEX 1u

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

static paging_stats_t g_stats;
static uint64_t* g_kernel_pml4_virt;
static uint64_t g_kernel_cr3;
static uint64_t* g_active_pml4_virt;
static uint64_t g_active_cr3;

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

static inline void flush_tlb_page(uint64_t virt_addr) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt_addr) : "memory");
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

static uint64_t* table_from_entry(uint64_t entry) {
    return (uint64_t*)(uintptr_t)(entry & PAGE_ADDR_MASK);
}

static uint8_t entry_can_descend(uint64_t entry) {
    return (uint8_t)((entry & PAGE_PRESENT) != 0 && (entry & PAGE_PS) == 0);
}

static uint64_t* ensure_next_table(uint64_t* table, uint16_t index, uint64_t flags) {
    uint64_t entry = table[index];

    if (entry & PAGE_PRESENT) {
        if (entry & PAGE_PS) {
            return NULL;
        }
        if ((entry & flags) != flags) {
            table[index] = entry | flags;
            flush_tlb_page((uint64_t)(uintptr_t)table);
        }
        return table_from_entry(table[index]);
    }

    uint64_t phys = 0;
    uint64_t* next = alloc_table_page(&phys);
    if (!next) {
        return NULL;
    }

    table[index] = phys | PAGE_PRESENT | flags;
    return next;
}

static int map_4k_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint16_t i4 = (uint16_t)((virt_addr >> 39) & 0x1FFu);
    uint16_t i3 = (uint16_t)((virt_addr >> 30) & 0x1FFu);
    uint16_t i2 = (uint16_t)((virt_addr >> 21) & 0x1FFu);
    uint16_t i1 = (uint16_t)((virt_addr >> 12) & 0x1FFu);
    uint64_t* pdpt;
    uint64_t* pd;
    uint64_t* pt;

    if (!g_active_pml4_virt) {
        return -1;
    }

    pdpt = ensure_next_table(g_active_pml4_virt, i4, PAGE_RW | PAGE_USER);
    if (!pdpt) {
        return -1;
    }
    pd = ensure_next_table(pdpt, i3, PAGE_RW | PAGE_USER);
    if (!pd) {
        return -1;
    }
    pt = ensure_next_table(pd, i2, PAGE_RW | PAGE_USER);
    if (!pt) {
        return -1;
    }
    if (pt[i1] & PAGE_PRESENT) {
        return -1;
    }

    pt[i1] = (phys_addr & PAGE_ADDR_MASK) | PAGE_PRESENT | PAGE_USER | flags;
    flush_tlb_page(virt_addr);
    g_stats.mapped_bytes += PAGE_SIZE;
    return 0;
}

static uint64_t* walk_pt(uint64_t virt_addr) {
    uint16_t i4 = (uint16_t)((virt_addr >> 39) & 0x1FFu);
    uint16_t i3 = (uint16_t)((virt_addr >> 30) & 0x1FFu);
    uint16_t i2 = (uint16_t)((virt_addr >> 21) & 0x1FFu);
    uint64_t* pdpt;
    uint64_t* pd;
    uint64_t* pt;

    if (!g_active_pml4_virt || !entry_can_descend(g_active_pml4_virt[i4])) {
        return NULL;
    }
    pdpt = table_from_entry(g_active_pml4_virt[i4]);
    if (!entry_can_descend(pdpt[i3])) {
        return NULL;
    }
    pd = table_from_entry(pdpt[i3]);
    if (!entry_can_descend(pd[i2])) {
        return NULL;
    }
    pt = table_from_entry(pd[i2]);
    return pt;
}

static void destroy_user_pd(uint64_t* pd) {
    if (!pd) {
        return;
    }

    for (uint32_t i = 0; i < 512u; i++) {
        uint64_t pd_entry = pd[i];
        if (!entry_can_descend(pd_entry)) {
            pd[i] = 0;
            continue;
        }

        uint64_t* pt = table_from_entry(pd_entry);
        for (uint32_t j = 0; j < 512u; j++) {
            uint64_t pte = pt[j];
            uint64_t phys;
            if ((pte & PAGE_PRESENT) == 0) {
                continue;
            }
            phys = pte & PAGE_ADDR_MASK;
            pt[j] = 0;
            if (phys != 0) {
                pmm_free_page(phys);
            }
        }
        pmm_free_page((uint64_t)(uintptr_t)pt);
        if (g_stats.table_pages > 0) {
            g_stats.table_pages--;
        }
        pd[i] = 0;
    }

    pmm_free_page((uint64_t)(uintptr_t)pd);
    if (g_stats.table_pages > 0) {
        g_stats.table_pages--;
    }
}

static void destroy_user_pdpt(uint64_t* pdpt) {
    if (!pdpt) {
        return;
    }

    for (uint32_t i = 0; i < 512u; i++) {
        uint64_t pdpt_entry = pdpt[i];
        if (!entry_can_descend(pdpt_entry)) {
            pdpt[i] = 0;
            continue;
        }

        destroy_user_pd(table_from_entry(pdpt_entry));
        pdpt[i] = 0;
    }

    pmm_free_page((uint64_t)(uintptr_t)pdpt);
    if (g_stats.table_pages > 0) {
        g_stats.table_pages--;
    }
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

    for (uint32_t i = 0; i < boot->disk_count && i < BOOT_MAX_DISKS; i++) {
        uint64_t extra_disk_end = boot->disks[i].image_base + boot->disks[i].image_size;
        if (extra_disk_end > max_phys) {
            max_phys = extra_disk_end;
        }
    }

    return max_phys;
}

int paging_init(const boot_info_t* boot) {
    g_stats.mapped_bytes = 0;
    g_stats.table_pages = 0;
    g_stats.cr3 = 0;
    g_kernel_pml4_virt = NULL;
    g_kernel_cr3 = 0;
    g_active_pml4_virt = NULL;
    g_active_cr3 = 0;

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

    g_kernel_pml4_virt = pml4;
    g_kernel_cr3 = pml4_phys;
    g_active_pml4_virt = pml4;
    g_active_cr3 = pml4_phys;
    load_cr3(pml4_phys);
    g_stats.cr3 = pml4_phys;
    g_stats.mapped_bytes = map_limit;
    return 0;
}

uint64_t paging_kernel_cr3(void) {
    return g_kernel_cr3;
}

uint64_t paging_current_cr3(void) {
    return g_active_cr3;
}

int paging_switch_to(uint64_t cr3) {
    if (cr3 == 0) {
        return -1;
    }

    g_active_cr3 = cr3;
    g_active_pml4_virt = (uint64_t*)(uintptr_t)cr3;
    load_cr3(cr3);
    return 0;
}

int paging_space_create(uint64_t* out_cr3) {
    uint64_t phys;
    uint64_t* pml4;

    if (!out_cr3 || !g_kernel_pml4_virt || g_kernel_cr3 == 0) {
        return -1;
    }

    phys = pmm_alloc_page();
    if (phys == 0) {
        return -1;
    }

    pml4 = (uint64_t*)(uintptr_t)phys;
    mem_zero(pml4, PAGE_SIZE);
    for (uint32_t i = 0; i < 512u; i++) {
        pml4[i] = g_kernel_pml4_virt[i];
    }
    pml4[USER_PML4_INDEX] = 0;
    g_stats.table_pages++;
    *out_cr3 = phys;
    return 0;
}

void paging_space_destroy(uint64_t cr3) {
    uint64_t* pml4;
    uint64_t pml4_user_entry;

    if (cr3 == 0 || cr3 == g_kernel_cr3) {
        return;
    }

    pml4 = (uint64_t*)(uintptr_t)cr3;
    pml4_user_entry = pml4[USER_PML4_INDEX];
    if (entry_can_descend(pml4_user_entry)) {
        destroy_user_pdpt(table_from_entry(pml4_user_entry));
        pml4[USER_PML4_INDEX] = 0;
    }

    pmm_free_page(cr3);
    if (g_stats.table_pages > 0) {
        g_stats.table_pages--;
    }
}

int paging_alloc_user_range(uint64_t virt_addr, uint64_t size, uint8_t writable) {
    uint64_t aligned_start = virt_addr & ~(PAGE_SIZE - 1ULL);
    uint64_t aligned_end = align_up(virt_addr + size, PAGE_SIZE);
    uint64_t flags = writable ? PAGE_RW : 0;

    if (size == 0 || aligned_end <= aligned_start) {
        return -1;
    }

    for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        uint8_t* page_ptr;

        if (phys == 0) {
            paging_free_user_range(aligned_start, addr - aligned_start);
            return -1;
        }
        if (map_4k_page(addr, phys, flags) != 0) {
            pmm_free_page(phys);
            paging_free_user_range(aligned_start, addr - aligned_start);
            return -1;
        }

        /* Zero through identity-mapped physical address so kernel writes do
           not depend on user RW permissions of the virtual mapping. */
        page_ptr = (uint8_t*)(uintptr_t)phys;
        mem_zero(page_ptr, PAGE_SIZE);
    }

    return 0;
}

void paging_free_user_range(uint64_t virt_addr, uint64_t size) {
    uint64_t aligned_start = virt_addr & ~(PAGE_SIZE - 1ULL);
    uint64_t aligned_end = align_up(virt_addr + size, PAGE_SIZE);

    if (size == 0 || aligned_end <= aligned_start) {
        return;
    }

    for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
        uint64_t* pt = walk_pt(addr);
        uint16_t i1 = (uint16_t)((addr >> 12) & 0x1FFu);
        uint64_t entry;
        uint64_t phys;

        if (!pt) {
            continue;
        }
        entry = pt[i1];
        if (!(entry & PAGE_PRESENT)) {
            continue;
        }

        phys = entry & PAGE_ADDR_MASK;
        pt[i1] = 0;
        flush_tlb_page(addr);
        if (phys != 0) {
            pmm_free_page(phys);
        }
        if (g_stats.mapped_bytes >= PAGE_SIZE) {
            g_stats.mapped_bytes -= PAGE_SIZE;
        } else {
            g_stats.mapped_bytes = 0;
        }
    }
}

int paging_user_range_accessible(uint64_t virt_addr, uint64_t size, uint8_t writable) {
    uint64_t aligned_start;
    uint64_t aligned_end;

    if (size == 0) {
        return 1;
    }
    if (virt_addr == 0 || virt_addr + size < virt_addr) {
        return 0;
    }

    aligned_start = virt_addr & ~(PAGE_SIZE - 1ULL);
    aligned_end = align_up(virt_addr + size, PAGE_SIZE);
    if (aligned_end <= aligned_start) {
        return 0;
    }

    for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
        uint64_t* pt = walk_pt(addr);
        uint16_t i1 = (uint16_t)((addr >> 12) & 0x1FFu);
        uint64_t entry;

        if (!pt) {
            return 0;
        }
        entry = pt[i1];
        if ((entry & PAGE_PRESENT) == 0 || (entry & PAGE_USER) == 0) {
            return 0;
        }
        if (writable && (entry & PAGE_RW) == 0) {
            return 0;
        }
    }

    return 1;
}

void paging_get_stats(paging_stats_t* out) {
    if (!out) {
        return;
    }
    *out = g_stats;
}
