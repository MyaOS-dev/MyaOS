#include "graphics.h"
#include "font.h"
#include "heap.h"
#include <myaos/syscall.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t* g_backbuffer;
static uint64_t g_backbuffer_pixels;
static uint8_t g_backbuffer_enabled;

static void fb_fill(uint32_t* dst, uint32_t count, uint32_t color) {
    uint64_t fill64 = ((uint64_t)color << 32) | color;
    uint32_t i = 0u;

    if (!dst || count == 0u) {
        return;
    }

    for (; i + 1u < count; i += 2u) {
        ((uint64_t*)dst)[i / 2u] = fill64;
    }
    if (i < count) {
        dst[i] = color;
    }
}

static void fb_copy(uint32_t* dst, const uint32_t* src, uint32_t count) {
    uint32_t i = 0u;

    if (!dst || !src || count == 0u || dst == src) {
        return;
    }

    for (; i + 1u < count; i += 2u) {
        ((uint64_t*)dst)[i / 2u] = ((const uint64_t*)src)[i / 2u];
    }
    if (i < count) {
        dst[i] = src[i];
    }
}

static uint64_t fb_pixel_count(const boot_info_t* boot) {
    const framebuffer_t* fb;

    if (!boot) {
        return 0u;
    }
    fb = &boot->fb;
    if (fb->pixels_per_scanline == 0u || fb->height == 0u) {
        return 0u;
    }
    return (uint64_t)fb->pixels_per_scanline * (uint64_t)fb->height;
}

static void graphics_backbuffer_ensure(boot_info_t* boot) {
    uint64_t pixels;
    size_t bytes;
    uint32_t* front;

    if (!boot) {
        return;
    }

    pixels = fb_pixel_count(boot);
    if (pixels == 0u || pixels > (uint64_t)SIZE_MAX / sizeof(uint32_t)) {
        return;
    }

    if (g_backbuffer && g_backbuffer_pixels == pixels) {
        return;
    }

    if (g_backbuffer) {
        kfree(g_backbuffer);
        g_backbuffer = NULL;
        g_backbuffer_pixels = 0u;
    }

    bytes = (size_t)pixels * sizeof(uint32_t);
    g_backbuffer = (uint32_t*)kmalloc(bytes);
    if (!g_backbuffer) {
        return;
    }

    g_backbuffer_pixels = pixels;
    front = (uint32_t*)(uintptr_t)boot->fb.base;
    fb_copy(g_backbuffer, front, (uint32_t)pixels);
}

void graphics_set_mode(boot_info_t* boot, uint32_t mode) {
    uint32_t* front;

    if (!boot) {
        return;
    }
    if (mode != MYAOS_GFX_MODE_GRAPHICS) {
        g_backbuffer_enabled = 0u;
        return;
    }

    graphics_backbuffer_ensure(boot);
    if (!g_backbuffer) {
        g_backbuffer_enabled = 0u;
        return;
    }

    front = (uint32_t*)(uintptr_t)boot->fb.base;
    fb_copy(g_backbuffer, front, (uint32_t)fb_pixel_count(boot));
    g_backbuffer_enabled = 1u;
}

uint32_t* graphics_draw_buffer(boot_info_t* boot) {
    if (!boot) {
        return NULL;
    }
    if (g_backbuffer_enabled && g_backbuffer) {
        return g_backbuffer;
    }
    return (uint32_t*)(uintptr_t)boot->fb.base;
}

int graphics_present(boot_info_t* boot) {
    uint32_t* front;
    uint64_t pixels;

    if (!boot) {
        return -1;
    }
    if (!g_backbuffer_enabled || !g_backbuffer) {
        return 0;
    }

    pixels = fb_pixel_count(boot);
    front = (uint32_t*)(uintptr_t)boot->fb.base;
    fb_copy(front, g_backbuffer, (uint32_t)pixels);
    return 0;
}

void put_pixel(boot_info_t* boot, uint32_t x, uint32_t y, uint32_t color) {
    framebuffer_t* fb;
    uint32_t* pixels;

    if (!boot) {
        return;
    }

    fb = &boot->fb;
    if (x >= fb->width || y >= fb->height) {
        return;
    }

    pixels = graphics_draw_buffer(boot);
    if (!pixels) {
        return;
    }
    pixels[y * fb->pixels_per_scanline + x] = color;
}

void clear_screen(boot_info_t* boot, uint32_t color) {
    framebuffer_t* fb;
    uint32_t* pixels;

    if (!boot) {
        return;
    }
    fb = &boot->fb;
    pixels = graphics_draw_buffer(boot);
    if (!pixels) {
        return;
    }
    fb_fill(pixels, fb->pixels_per_scanline * fb->height, color);
}

void draw_line(boot_info_t* boot, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    int32_t dx;
    int32_t sx;
    int32_t dy;
    int32_t sy;
    int32_t err;

    if (!boot) {
        return;
    }

    dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    sx = (x0 < x1) ? 1 : -1;
    dy = (y0 < y1) ? (y0 - y1) : (y1 - y0);
    sy = (y0 < y1) ? 1 : -1;
    err = dx + dy;

    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            put_pixel(boot, (uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }

        {
            int32_t e2 = err << 1;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

void fill_rect(boot_info_t* boot, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    framebuffer_t* fb;
    uint32_t* pixels;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int64_t x1_raw;
    int64_t y1_raw;

    if (!boot || width <= 0 || height <= 0) {
        return;
    }

    fb = &boot->fb;
    pixels = graphics_draw_buffer(boot);
    if (!pixels) {
        return;
    }

    x0 = x;
    y0 = y;
    x1_raw = (int64_t)x + (int64_t)width;
    y1_raw = (int64_t)y + (int64_t)height;
    if (x1_raw > 0x7FFFFFFFll) {
        x1 = 0x7FFFFFFF;
    } else if (x1_raw < -0x80000000ll) {
        x1 = -0x80000000;
    } else {
        x1 = (int32_t)x1_raw;
    }
    if (y1_raw > 0x7FFFFFFFll) {
        y1 = 0x7FFFFFFF;
    } else if (y1_raw < -0x80000000ll) {
        y1 = -0x80000000;
    } else {
        y1 = (int32_t)y1_raw;
    }

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (int32_t)fb->width) {
        x1 = (int32_t)fb->width;
    }
    if (y1 > (int32_t)fb->height) {
        y1 = (int32_t)fb->height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int32_t row = y0; row < y1; row++) {
        uint32_t* line = pixels + (uint32_t)row * fb->pixels_per_scanline + (uint32_t)x0;
        fb_fill(line, (uint32_t)(x1 - x0), color);
    }
}

void draw_rect_frame(boot_info_t* boot, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    fill_rect(boot, x, y, width, 1, color);
    fill_rect(boot, x, y + height - 1, width, 1, color);
    fill_rect(boot, x, y, 1, height, color);
    fill_rect(boot, x + width - 1, y, 1, height, color);
}

void draw_char(boot_info_t* boot, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    framebuffer_t* fb;
    uint32_t* pixels;
    uint32_t max_rows;
    uint32_t max_cols;
    unsigned char uc = (unsigned char)c;

    if (!boot) {
        return;
    }

    fb = &boot->fb;
    pixels = graphics_draw_buffer(boot);
    if (!pixels) {
        return;
    }

    if (uc >= 128u) {
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

    for (uint32_t row = 0u; row < max_rows; row++) {
        uint8_t bits = font8x8_basic[uc][row >> 1];
        uint32_t* row_px = pixels + ((y + row) * fb->pixels_per_scanline + x);

        for (uint32_t col = 0u; col < max_cols; col++) {
            row_px[col] = (bits & (1u << (7u - col))) ? fg : bg;
        }
    }
}

void draw_string(boot_info_t* boot, uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg) {
    uint32_t start_x = x;

    while (str && *str) {
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
    framebuffer_t* fb;
    uint32_t* pixels;
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    if (!boot || xcord <= 0 || ycord <= 0) {
        return;
    }

    fb = &boot->fb;
    pixels = graphics_draw_buffer(boot);
    if (!pixels) {
        return;
    }

    x0 = FONT_WIDTH * (uint32_t)(xcord - 1);
    y0 = FONT_HEIGHT * (uint32_t)(ycord - 1);
    x1 = x0 + FONT_WIDTH;
    y1 = y0 + FONT_HEIGHT;

    for (uint32_t y = y0; y < y1 && y < fb->height; y++) {
        for (uint32_t x = x0; x < x1 && x < fb->width; x++) {
            pixels[y * fb->pixels_per_scanline + x] = 0x00000000u;
        }
    }
}
