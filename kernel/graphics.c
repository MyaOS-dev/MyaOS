#include "graphics.h"
#include "font.h"
#include <stdint.h>
#include <stddef.h>

void put_pixel(boot_info_t* boot, uint32_t x, uint32_t y, uint32_t color) {
    framebuffer_t* fb = &boot->fb;

    if (x >= fb->width || y >= fb->height) {
        return;
    }

    volatile uint32_t* pixels = (volatile uint32_t*)(uintptr_t)fb->base;
    pixels[y * fb->pixels_per_scanline + x] = color;
}

void clear_screen(boot_info_t* boot, uint32_t color) {
    framebuffer_t* fb = &boot->fb;
    volatile uint32_t* pixels = (volatile uint32_t*)(uintptr_t)fb->base;

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            pixels[y * fb->pixels_per_scanline + x] = color;
        }
    }
}

void draw_char(boot_info_t* boot, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    unsigned char uc = (unsigned char)c;

    if (uc >= 128) {
        uc = '?';
    }

    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = font8x8_basic[uc][row >> 1];

        for (uint32_t col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (bits & (1 << (7 - col))) ? fg : bg;
            put_pixel(boot, x + col, y + row, color);
        }
    }
}

void draw_string(boot_info_t* boot, uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg) {
    uint32_t start_x = x;

    while (*str) {
        if (*str == '\n') {
            x = start_x;
            y += FONT_HEIGHT;
            str++;
            continue;
        }

        draw_char(boot, x, y, *str, fg, bg);
        x += FONT_WIDTH;
        str++;
    }
}

void draw_symbol(boot_info_t* boot, int xcord, int ycord) {
    if (xcord <= 0 || ycord <= 0) {
        return;
    }

    framebuffer_t* fb = &boot->fb;
    volatile uint32_t* pixels = (volatile uint32_t*)(uintptr_t)fb->base;

    uint32_t x0 = FONT_WIDTH * (uint32_t)(xcord - 1);
    uint32_t y0 = FONT_HEIGHT * (uint32_t)(ycord - 1);
    uint32_t x1 = x0 + FONT_WIDTH;
    uint32_t y1 = y0 + FONT_HEIGHT;

    for (uint32_t y = y0; y < y1 && y < fb->height; y++) {
        for (uint32_t x = x0; x < x1 && x < fb->width; x++) {
            pixels[y * fb->pixels_per_scanline + x] = 0x00000000;
        }
    }
}