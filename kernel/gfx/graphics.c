#include "graphics.h"
#include "font.h"
#include <stdint.h>
#include <stddef.h>

static void fb_fill(uint32_t* dst, uint32_t count, uint32_t color) {
    uint64_t fill64 = ((uint64_t)color << 32) | color;
    uint32_t i = 0u;

    for (; i + 1u < count; i += 2u) {
        ((uint64_t*)dst)[i / 2u] = fill64;
    }
    if (i < count) {
        dst[i] = color;
    }
}

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
    uint32_t* pixels = (uint32_t*)(uintptr_t)fb->base;
    fb_fill(pixels, fb->pixels_per_scanline * fb->height, color);
}

void draw_char(boot_info_t* boot, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    framebuffer_t* fb = &boot->fb;
    uint32_t* pixels = (uint32_t*)(uintptr_t)fb->base;
    uint32_t max_rows;
    uint32_t max_cols;
    unsigned char uc = (unsigned char)c;

    if (uc >= 128) {
        uc = '?';
    }

    if (x >= fb->width || y >= fb->height) {
        return;
    }

    max_cols = fb->width - x;
    if (max_cols > FONT_WIDTH) {
        max_cols = FONT_WIDTH;
    }
    max_rows = fb->height - y;
    if (max_rows > FONT_HEIGHT) {
        max_rows = FONT_HEIGHT;
    }

    for (uint32_t row = 0; row < max_rows; row++) {
        uint8_t bits = font8x8_basic[uc][row >> 1];
        uint32_t* row_px = pixels + ((y + row) * fb->pixels_per_scanline + x);

        for (uint32_t col = 0; col < max_cols; col++) {
            row_px[col] = (bits & (1u << (7 - col))) ? fg : bg;
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
