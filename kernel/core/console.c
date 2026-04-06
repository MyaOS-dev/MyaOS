#include "console.h"
#include "device.h"
#include "font.h"
#include "graphics.h"
#include "log.h"
#include <stddef.h>

#ifndef MYAOS_DEBUG
#define MYAOS_DEBUG 0
#endif

typedef struct {
    boot_info_t* boot;
    uint32_t fg;
    uint32_t bg;
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_col;
    uint32_t cursor_row;
} console_state_t;

static console_state_t g_console;
static uint8_t g_console_registered;

static inline void console_debug_putc(char c) {
#if MYAOS_DEBUG
    __asm__ __volatile__("outb %0, $0xe9" : : "a"((uint8_t)c));
#else
    (void)c;
#endif
}

static void fb_copy_overlap(uint32_t* dst, const uint32_t* src, uint32_t count) {
    if (!dst || !src || count == 0u || dst == src) {
        return;
    }

    if (dst < src) {
        uint32_t i = 0u;
        for (; i + 1u < count; i += 2u) {
            ((uint64_t*)dst)[i / 2u] = ((const uint64_t*)src)[i / 2u];
        }
        if (i < count) {
            dst[i] = src[i];
        }
    } else {
        uint32_t i = count;
        while (i >= 2u) {
            i -= 2u;
            ((uint64_t*)(dst + i))[0] = ((const uint64_t*)(src + i))[0];
        }
        if (i == 1u) {
            dst[0] = src[0];
        }
    }
}

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

static void u32_to_dec(uint32_t value, char* out, size_t out_size) {
    char rev[16];
    size_t n = 0;
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static void u64_to_dec(uint64_t value, char* out, size_t out_size) {
    char rev[32];
    size_t n = 0;
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static void console_scroll(void) {
    framebuffer_t* fb;
    uint32_t* pixels;
    uint32_t stride;
    uint32_t scroll_rows;
    uint32_t move_count;
    uint32_t clear_offset;
    uint32_t clear_count;

    if (!g_console.boot) {
        return;
    }

    fb = &g_console.boot->fb;
    pixels = (uint32_t*)(uintptr_t)fb->base;

    if (fb->height <= FONT_HEIGHT) {
        clear_screen(g_console.boot, g_console.bg);
        g_console.cursor_col = 0;
        g_console.cursor_row = 0;
        return;
    }

    stride = fb->pixels_per_scanline;
    scroll_rows = FONT_HEIGHT;
    move_count = (fb->height - scroll_rows) * stride;
    clear_offset = move_count;
    clear_count = scroll_rows * stride;

    fb_copy_overlap(pixels, pixels + (scroll_rows * stride), move_count);
    fb_fill(pixels + clear_offset, clear_count, g_console.bg);

    if (g_console.cursor_row > 0) {
        g_console.cursor_row--;
    }
}

static void console_newline(void) {
    g_console.cursor_col = 0;
    g_console.cursor_row++;

    if (g_console.cursor_row >= g_console.rows) {
        console_scroll();
        g_console.cursor_row = g_console.rows - 1;
    }
}

void console_init(boot_info_t* boot, uint32_t fg, uint32_t bg) {
    klog_init();
    g_console.boot = boot;
    g_console.fg = fg;
    g_console.bg = bg;
    g_console.cursor_col = 0;
    g_console.cursor_row = 0;

    if (!boot) {
        g_console.cols = 1;
        g_console.rows = 1;
        return;
    }

    g_console.cols = boot->fb.width / FONT_WIDTH;
    g_console.rows = boot->fb.height / FONT_HEIGHT;
    if (g_console.cols == 0) {
        g_console.cols = 1;
    }
    if (g_console.rows == 0) {
        g_console.rows = 1;
    }

    if (!g_console_registered && device_register(MYAOS_DEV_CONSOLE, "fb0", "framebuffer", NULL, NULL, NULL) == 0) {
        g_console_registered = 1;
    }

    console_reset();
}

void console_reset(void) {
    if (!g_console.boot) {
        return;
    }

    clear_screen(g_console.boot, g_console.bg);
    g_console.cursor_col = 0;
    g_console.cursor_row = 0;
}

void console_put_char(char c) {
    console_debug_putc(c);

    if (!g_console.boot) {
        return;
    }

    if (c == '\n') {
        console_newline();
        return;
    }

    if (c == '\b') {
        if (g_console.cursor_col > 0) {
            g_console.cursor_col--;
        } else if (g_console.cursor_row > 0) {
            g_console.cursor_row--;
            g_console.cursor_col = g_console.cols - 1;
        } else {
            return;
        }

        draw_char(
            g_console.boot,
            g_console.cursor_col * FONT_WIDTH,
            g_console.cursor_row * FONT_HEIGHT,
            ' ',
            g_console.fg,
            g_console.bg
        );
        return;
    }

    if (c < 32 || c > 126) {
        return;
    }

    draw_char(
        g_console.boot,
        g_console.cursor_col * FONT_WIDTH,
        g_console.cursor_row * FONT_HEIGHT,
        c,
        g_console.fg,
        g_console.bg
    );

    g_console.cursor_col++;
    if (g_console.cursor_col >= g_console.cols) {
        console_newline();
    }
}

void console_write(const char* text) {
    if (!text) {
        return;
    }

    klog_write(text);

    for (size_t i = 0; text[i]; i++) {
        console_put_char(text[i]);
    }
}

void console_write_len(const char* text, size_t len) {
    if (!text) {
        return;
    }

    klog_write_len(text, len);

    for (size_t i = 0; i < len; i++) {
        console_put_char(text[i]);
    }
}

void console_write_u32(uint32_t value) {
    char buf[16];
    u32_to_dec(value, buf, sizeof(buf));
    console_write(buf);
}

void console_write_u64(uint64_t value) {
    char buf[32];
    u64_to_dec(value, buf, sizeof(buf));
    console_write(buf);
}

void console_write_binary_as_text(const uint8_t* data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c <= 126)) {
            console_put_char(c);
        } else {
            console_put_char('.');
        }
    }
}

void console_get_cursor(uint32_t* out_col, uint32_t* out_row) {
    if (out_col) {
        *out_col = g_console.cursor_col;
    }
    if (out_row) {
        *out_row = g_console.cursor_row;
    }
}

uint32_t console_cols(void) {
    return g_console.cols;
}

void console_draw_cell(uint32_t col, uint32_t row, char c) {
    if (!g_console.boot || col >= g_console.cols || row >= g_console.rows) {
        return;
    }
    if (c < 32 || c > 126) {
        c = ' ';
    }
    draw_char(
        g_console.boot,
        col * FONT_WIDTH,
        row * FONT_HEIGHT,
        c,
        g_console.fg,
        g_console.bg
    );
}

boot_info_t* console_boot_info(void) {
    return g_console.boot;
}
