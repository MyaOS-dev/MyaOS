#include "gdt.h"
#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} tss_t;

static uint64_t g_gdt[7];
static tss_t g_tss;

static void mem_zero(void* ptr, size_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static uint64_t make_seg_descriptor(uint8_t access, uint8_t flags) {
    uint64_t desc = 0;

    desc |= 0xFFFFULL;
    desc |= ((uint64_t)access) << 40;
    desc |= 0xFULL << 48;
    desc |= ((uint64_t)(flags & 0x0Fu)) << 52;
    return desc;
}

static void set_tss_descriptor(uint16_t selector, const tss_t* tss, uint32_t limit) {
    uint64_t base = (uint64_t)(uintptr_t)tss;
    uint32_t index = (uint32_t)(selector >> 3);
    uint64_t low = 0;
    uint64_t high = 0;

    low |= (uint64_t)(limit & 0xFFFFu);
    low |= (base & 0xFFFFFFULL) << 16;
    low |= (uint64_t)0x89u << 40;
    low |= (uint64_t)((limit >> 16) & 0xFu) << 48;
    low |= (base & 0xFF000000ULL) << 32;

    high = base >> 32;

    g_gdt[index] = low;
    g_gdt[index + 1] = high;
}

static void load_gdt_and_segments(void) {
    gdtr_t gdtr;
    uint16_t data_sel = (uint16_t)GDT_SEL_KERNEL_DATA;

    gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1u);
    gdtr.base = (uint64_t)(uintptr_t)&g_gdt[0];

    __asm__ __volatile__("lgdt %0" : : "m"(gdtr) : "memory");

    __asm__ __volatile__(
        "pushq %[cs]\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov %[ds], %%ds\n"
        "mov %[ds], %%es\n"
        "mov %[ds], %%ss\n"
        "mov %[ds], %%fs\n"
        "mov %[ds], %%gs\n"
        :
        : [cs] "i"((uint64_t)GDT_SEL_KERNEL_CODE), [ds] "r"(data_sel)
        : "rax", "memory"
    );
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}

void gdt_init(void) {
    uint16_t tss_sel = (uint16_t)GDT_SEL_TSS;

    mem_zero(g_gdt, sizeof(g_gdt));
    mem_zero(&g_tss, sizeof(g_tss));

    g_gdt[0] = 0;
    g_gdt[1] = make_seg_descriptor(0x9Au, 0xAu);
    g_gdt[2] = make_seg_descriptor(0x92u, 0xCu);
    g_gdt[3] = make_seg_descriptor(0xF2u, 0xCu);
    g_gdt[4] = make_seg_descriptor(0xFAu, 0xAu);

    g_tss.iopb_offset = (uint16_t)sizeof(g_tss);
    set_tss_descriptor(tss_sel, &g_tss, (uint32_t)(sizeof(g_tss) - 1u));

    load_gdt_and_segments();
    __asm__ __volatile__("ltr %0" : : "r"(tss_sel) : "memory");
}
