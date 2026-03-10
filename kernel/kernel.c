#include "boot.h"
#include "graphics.c"
#include <stdint.h>
#include <stddef.h>

extern write_symbol(boot_info_t* boot);

void kernel_main(boot_info_t* boot) {
    framebuffer_t* fb = &boot->fb;
    volatile uint32_t* pixels = (volatile uint32_t*)(uintptr_t)fb->base;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            pixels[y * fb->pixels_per_scanline + x] = 0x00ffffff;
        }
    }
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}