#ifndef GDT_PORTABLE_H
#define GDT_PORTABLE_H

#include <stdint.h>

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t rsp0);

static inline uint16_t gdt_kernel_code_selector(void) { return 0x08u; }
static inline uint16_t gdt_kernel_data_selector(void) { return 0x10u; }
static inline uint16_t gdt_user_code_selector(void) { return 0x1Bu; }
static inline uint16_t gdt_user_data_selector(void) { return 0x23u; }

#endif
