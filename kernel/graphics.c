#include "boot.h"
#include <stdint.h>
#include <stddef.h>


void write_symbol(boot_info_t* boot) {
    framebuffer_t* fb = &boot->fb;
    volatile uint32_t* pixels = (volatile uint32_t*)(uintptr_t)fb->base;
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 19; x++) {
            pixels[y * fb->pixels_per_scanline + x] = 0x00000000;
        }
    }
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}