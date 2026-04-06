#ifndef GDT_X86_64_H
#define GDT_X86_64_H

#include <stdint.h>

#define GDT_SEL_KERNEL_CODE 0x08u
#define GDT_SEL_KERNEL_DATA 0x10u
#define GDT_SEL_USER_DATA 0x18u
#define GDT_SEL_USER_CODE 0x20u
#define GDT_SEL_TSS 0x28u

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t rsp0);

static inline uint16_t gdt_kernel_code_selector(void) { return (uint16_t)GDT_SEL_KERNEL_CODE; }
static inline uint16_t gdt_kernel_data_selector(void) { return (uint16_t)GDT_SEL_KERNEL_DATA; }
static inline uint16_t gdt_user_code_selector(void) { return (uint16_t)(GDT_SEL_USER_CODE | 0x3u); }
static inline uint16_t gdt_user_data_selector(void) { return (uint16_t)(GDT_SEL_USER_DATA | 0x3u); }

#endif
