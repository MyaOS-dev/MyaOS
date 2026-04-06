#include "gdt.h"

void gdt_init(void) {}

void gdt_set_kernel_stack(uint64_t rsp0) {
    (void)rsp0;
}
