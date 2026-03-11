#include "boot.h"
#include <stdint.h>
#include <stddef.h>


void draw_symbol(boot_info_t* boot, int xcord, int ycord) {
    framebuffer_t* fb = &boot->fb;
    volatile uint32_t* pixels = (volatile uint32_t*)(uintptr_t)fb->base;
    for (uint32_t y = ycord; y < 8 + ycord; y++) {
        for (uint32_t x = xcord; x < 19 + xcord; x++) {
            pixels[y * fb->pixels_per_scanline + x] = 0x00000000;
        }
    }
}
