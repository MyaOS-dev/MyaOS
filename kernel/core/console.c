#include "console.h"
#include "device.h"
#include "font.h"
#include "graphics.h"
#include "heap.h"
#include "log.h"
#include <stddef.h>

#ifndef MYAOS_DEBUG
#define MYAOS_DEBUG 0
#endif

typedef struct {
    char ch;
    uint32_t fg;
    uint32_t bg;
} console_cell_t;

typedef struct {
    uint32_t default_fg;
    uint32_t default_bg;
    uint32_t fg;
    uint32_t bg;
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint8_t esc_state;
    uint8_t esc_param_active;
    uint8_t esc_param_count;
    uint8_t reserved0;
    uint32_t esc_params[8];
    uint32_t esc_param_value;
    console_cell_t* cells;
} console_vt_t;

typedef struct {
    boot_info_t* boot;
    uint32_t mode;
    uint32_t cols;
    uint32_t rows;
    uint32_t active_vt;
    uint32_t vt_count;
    console_vt_t vts[CONSOLE_TTY_MAX];
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

static int utf8_decode_one(const uint8_t* in, size_t len, uint32_t* out_cp, size_t* out_consumed) {
    uint8_t b0;
    uint32_t cp = 0u;

    if (!in || len == 0u || !out_cp || !out_consumed) {
        return -1;
    }

    b0 = in[0];
    if (b0 < 0x80u) {
        *out_cp = (uint32_t)b0;
        *out_consumed = 1u;
        return 0;
    }

    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (len < 2u || (in[1] & 0xC0u) != 0x80u) {
            return -1;
        }
        cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(in[1] & 0x3Fu);
        *out_cp = cp;
        *out_consumed = 2u;
        return 0;
    }

    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        uint8_t b1;
        uint8_t b2;
        if (len < 3u) {
            return -1;
        }
        b1 = in[1];
        b2 = in[2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return -1;
        }
        if ((b0 == 0xE0u && b1 < 0xA0u) || (b0 == 0xEDu && b1 >= 0xA0u)) {
            return -1;
        }
        cp = ((uint32_t)(b0 & 0x0Fu) << 12) |
             ((uint32_t)(b1 & 0x3Fu) << 6) |
             (uint32_t)(b2 & 0x3Fu);
        *out_cp = cp;
        *out_consumed = 3u;
        return 0;
    }

    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
        if (len < 4u) {
            return -1;
        }
        b1 = in[1];
        b2 = in[2];
        b3 = in[3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
            return -1;
        }
        if ((b0 == 0xF0u && b1 < 0x90u) || (b0 == 0xF4u && b1 > 0x8Fu)) {
            return -1;
        }
        cp = ((uint32_t)(b0 & 0x07u) << 18) |
             ((uint32_t)(b1 & 0x3Fu) << 12) |
             ((uint32_t)(b2 & 0x3Fu) << 6) |
             (uint32_t)(b3 & 0x3Fu);
        if (cp > 0x10FFFFu) {
            return -1;
        }
        *out_cp = cp;
        *out_consumed = 4u;
        return 0;
    }

    return -1;
}

static char console_codepoint_to_char(uint32_t cp) {
    if (cp == 0x1Bu) {
        return (char)0x1B;
    }
    if (cp == '\n' || cp == '\r' || cp == '\b' || cp == '\t') {
        return (char)cp;
    }
    if (cp >= 32u && cp <= 126u) {
        return (char)cp;
    }
    if (cp == 0x00A0u) {
        return ' ';
    }
    return '?';
}

static console_vt_t* console_vt_get(uint32_t index) {
    if (index >= g_console.vt_count || index >= CONSOLE_TTY_MAX) {
        return NULL;
    }
    return &g_console.vts[index];
}

static console_vt_t* console_vt_active(void) {
    return console_vt_get(g_console.active_vt);
}

static uint8_t console_vt_is_active(const console_vt_t* vt) {
    return vt != NULL && vt == console_vt_active();
}

static uint32_t console_cell_count(void) {
    uint64_t total = (uint64_t)g_console.cols * (uint64_t)g_console.rows;
    if (total > 0xFFFFFFFFull) {
        return 0u;
    }
    return (uint32_t)total;
}

static console_cell_t* console_cell_at(console_vt_t* vt, uint32_t col, uint32_t row) {
    uint32_t idx;

    if (!vt || !vt->cells || col >= g_console.cols || row >= g_console.rows) {
        return NULL;
    }
    idx = row * g_console.cols + col;
    return &vt->cells[idx];
}

static char console_cell_sanitize(char c) {
    if (c < 32 || c > 126) {
        return ' ';
    }
    return c;
}

static void console_vt_reset_escape(console_vt_t* vt) {
    if (!vt) {
        return;
    }
    vt->esc_state = 0u;
    vt->esc_param_active = 0u;
    vt->esc_param_count = 0u;
    vt->esc_param_value = 0u;
}

static void console_vt_fill_cells(console_vt_t* vt, char ch, uint32_t fg, uint32_t bg) {
    uint32_t count;

    if (!vt || !vt->cells) {
        return;
    }
    count = console_cell_count();
    ch = console_cell_sanitize(ch);
    for (uint32_t i = 0u; i < count; i++) {
        vt->cells[i].ch = ch;
        vt->cells[i].fg = fg;
        vt->cells[i].bg = bg;
    }
}

static void console_vt_render(const console_vt_t* vt) {
    uint32_t rows;
    uint32_t cols;

    if (!g_console.boot || !vt) {
        return;
    }
    clear_screen(g_console.boot, vt->bg);
    if (!vt->cells) {
        return;
    }
    rows = g_console.rows;
    cols = g_console.cols;
    for (uint32_t row = 0u; row < rows; row++) {
        for (uint32_t col = 0u; col < cols; col++) {
            const console_cell_t* cell = &vt->cells[row * cols + col];
            draw_char(
                g_console.boot,
                col * FONT_WIDTH,
                row * FONT_HEIGHT,
                console_cell_sanitize(cell->ch),
                cell->fg,
                cell->bg
            );
        }
    }
}

static void console_vt_draw_cell(const console_vt_t* vt, uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg) {
    if (!g_console.boot || !vt || g_console.mode != CONSOLE_MODE_TEXT || !console_vt_is_active(vt)) {
        return;
    }
    if (col >= g_console.cols || row >= g_console.rows) {
        return;
    }
    draw_char(
        g_console.boot,
        col * FONT_WIDTH,
        row * FONT_HEIGHT,
        console_cell_sanitize(c),
        fg,
        bg
    );
}

static void console_vt_set_cell(console_vt_t* vt, uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg) {
    console_cell_t* cell = console_cell_at(vt, col, row);
    if (!cell) {
        return;
    }
    cell->ch = console_cell_sanitize(c);
    cell->fg = fg;
    cell->bg = bg;
}

static void console_vt_clear_span(console_vt_t* vt, uint32_t row, uint32_t col_begin, uint32_t col_end) {
    if (!vt || row >= g_console.rows || g_console.cols == 0u) {
        return;
    }
    if (col_begin >= g_console.cols) {
        return;
    }
    if (col_end > g_console.cols) {
        col_end = g_console.cols;
    }
    if (col_begin >= col_end) {
        return;
    }
    for (uint32_t col = col_begin; col < col_end; col++) {
        console_vt_set_cell(vt, col, row, ' ', vt->fg, vt->bg);
        console_vt_draw_cell(vt, col, row, ' ', vt->fg, vt->bg);
    }
}

static void console_vt_erase_display(console_vt_t* vt, uint32_t mode) {
    uint32_t row;

    if (!vt || g_console.rows == 0u || g_console.cols == 0u) {
        return;
    }

    if (mode >= 2u) {
        console_vt_fill_cells(vt, ' ', vt->fg, vt->bg);
        if (console_vt_is_active(vt) && g_console.mode == CONSOLE_MODE_TEXT) {
            console_vt_render(vt);
        }
        return;
    }

    if (mode == 0u) {
        for (row = vt->cursor_row; row < g_console.rows; row++) {
            uint32_t col_begin = (row == vt->cursor_row) ? vt->cursor_col : 0u;
            console_vt_clear_span(vt, row, col_begin, g_console.cols);
        }
        return;
    }

    for (row = 0u; row <= vt->cursor_row && row < g_console.rows; row++) {
        uint32_t col_end = (row == vt->cursor_row) ? (vt->cursor_col + 1u) : g_console.cols;
        console_vt_clear_span(vt, row, 0u, col_end);
    }
}

static void console_vt_erase_line(console_vt_t* vt, uint32_t mode) {
    uint32_t col_begin = 0u;
    uint32_t col_end = g_console.cols;

    if (!vt || vt->cursor_row >= g_console.rows) {
        return;
    }

    if (mode == 0u) {
        col_begin = vt->cursor_col;
    } else if (mode == 1u) {
        col_end = vt->cursor_col + 1u;
    }
    console_vt_clear_span(vt, vt->cursor_row, col_begin, col_end);
}

static void console_scroll(console_vt_t* vt) {
    framebuffer_t* fb;
    uint32_t* pixels;
    uint32_t stride;
    uint32_t scroll_rows;
    uint32_t move_count;
    uint32_t clear_offset;
    uint32_t clear_count;

    if (!g_console.boot || !vt) {
        return;
    }

    if (vt->cells) {
        uint32_t cols = g_console.cols;
        uint32_t rows = g_console.rows;
        if (rows == 0u || cols == 0u) {
            return;
        }
        for (uint32_t row = 1u; row < rows; row++) {
            for (uint32_t col = 0u; col < cols; col++) {
                vt->cells[(row - 1u) * cols + col] = vt->cells[row * cols + col];
            }
        }
        for (uint32_t col = 0u; col < cols; col++) {
            console_cell_t* cell = &vt->cells[(rows - 1u) * cols + col];
            cell->ch = ' ';
            cell->fg = vt->fg;
            cell->bg = vt->bg;
        }
        if (console_vt_is_active(vt) && g_console.mode == CONSOLE_MODE_TEXT) {
            console_vt_render(vt);
        }
        return;
    }

    fb = &g_console.boot->fb;
    pixels = (uint32_t*)(uintptr_t)fb->base;
    if (fb->height <= FONT_HEIGHT) {
        clear_screen(g_console.boot, vt->bg);
        return;
    }

    stride = fb->pixels_per_scanline;
    scroll_rows = FONT_HEIGHT;
    move_count = (fb->height - scroll_rows) * stride;
    clear_offset = move_count;
    clear_count = scroll_rows * stride;

    fb_copy_overlap(pixels, pixels + (scroll_rows * stride), move_count);
    fb_fill(pixels + clear_offset, clear_count, vt->bg);
}

static void console_newline(console_vt_t* vt) {
    if (!vt) {
        return;
    }
    vt->cursor_col = 0u;
    vt->cursor_row++;
    if (vt->cursor_row >= g_console.rows) {
        console_scroll(vt);
        vt->cursor_row = (g_console.rows == 0u) ? 0u : (g_console.rows - 1u);
    }
}

static uint32_t ansi_palette_color(uint8_t idx, uint8_t bright) {
    static const uint32_t normal[8] = {
        0x00000000u, 0x00AA0000u, 0x0000AA00u, 0x00AA5500u,
        0x000000AAu, 0x00AA00AAu, 0x0000AAAAu, 0x00AAAAAAu
    };
    static const uint32_t hi[8] = {
        0x00555555u, 0x00FF5555u, 0x0055FF55u, 0x00FFFF55u,
        0x005555FFu, 0x00FF55FFu, 0x0055FFFFu, 0x00FFFFFFu
    };

    if (idx > 7u) {
        idx = 7u;
    }
    return bright ? hi[idx] : normal[idx];
}

static void console_apply_sgr(console_vt_t* vt, uint32_t code) {
    if (!vt) {
        return;
    }
    if (code == 0u) {
        vt->fg = vt->default_fg;
        vt->bg = vt->default_bg;
        return;
    }
    if (code == 39u) {
        vt->fg = vt->default_fg;
        return;
    }
    if (code == 49u) {
        vt->bg = vt->default_bg;
        return;
    }
    if (code >= 30u && code <= 37u) {
        vt->fg = ansi_palette_color((uint8_t)(code - 30u), 0u);
        return;
    }
    if (code >= 90u && code <= 97u) {
        vt->fg = ansi_palette_color((uint8_t)(code - 90u), 1u);
        return;
    }
    if (code >= 40u && code <= 47u) {
        vt->bg = ansi_palette_color((uint8_t)(code - 40u), 0u);
        return;
    }
    if (code >= 100u && code <= 107u) {
        vt->bg = ansi_palette_color((uint8_t)(code - 100u), 1u);
        return;
    }
}

static void console_escape_add_param(console_vt_t* vt, uint32_t value) {
    if (!vt) {
        return;
    }
    if (vt->esc_param_count < (uint8_t)(sizeof(vt->esc_params) / sizeof(vt->esc_params[0]))) {
        vt->esc_params[vt->esc_param_count++] = value;
    }
}

static int console_handle_escape(console_vt_t* vt, char c) {
    if (!vt) {
        return 0;
    }
    if (vt->esc_state == 0u) {
        if ((uint8_t)c == 0x1Bu) {
            vt->esc_state = 1u;
            vt->esc_param_active = 0u;
            vt->esc_param_count = 0u;
            vt->esc_param_value = 0u;
            return 1;
        }
        return 0;
    }

    if (vt->esc_state == 1u) {
        if (c == '[') {
            vt->esc_state = 2u;
            vt->esc_param_active = 0u;
            vt->esc_param_count = 0u;
            vt->esc_param_value = 0u;
            return 1;
        }
        vt->esc_state = 0u;
        return 1;
    }

    if (vt->esc_state == 2u) {
        if (c >= '0' && c <= '9') {
            vt->esc_param_value = vt->esc_param_value * 10u + (uint32_t)(c - '0');
            vt->esc_param_active = 1u;
            return 1;
        }
        if (c == ';') {
            uint32_t value = vt->esc_param_active ? vt->esc_param_value : 0u;
            console_escape_add_param(vt, value);
            vt->esc_param_value = 0u;
            vt->esc_param_active = 0u;
            return 1;
        }

        if (vt->esc_param_active || vt->esc_param_count > 0u) {
            uint32_t value = vt->esc_param_active ? vt->esc_param_value : 0u;
            console_escape_add_param(vt, value);
        }

        if (c == 'm') {
            if (vt->esc_param_count == 0u) {
                console_apply_sgr(vt, 0u);
            } else {
                for (uint8_t i = 0u; i < vt->esc_param_count; i++) {
                    console_apply_sgr(vt, vt->esc_params[i]);
                }
            }
        } else if (c == 'J') {
            uint32_t mode = (vt->esc_param_count > 0u) ? vt->esc_params[0] : 0u;
            console_vt_erase_display(vt, mode);
        } else if (c == 'K') {
            uint32_t mode = (vt->esc_param_count > 0u) ? vt->esc_params[0] : 0u;
            console_vt_erase_line(vt, mode);
        } else if (c == 'H' || c == 'f') {
            uint32_t row = (vt->esc_param_count > 0u) ? vt->esc_params[0] : 1u;
            uint32_t col = (vt->esc_param_count > 1u) ? vt->esc_params[1] : 1u;

            if (row == 0u) {
                row = 1u;
            }
            if (col == 0u) {
                col = 1u;
            }
            if (row > g_console.rows) {
                row = g_console.rows;
            }
            if (col > g_console.cols) {
                col = g_console.cols;
            }

            vt->cursor_row = row - 1u;
            vt->cursor_col = col - 1u;
        }

        vt->esc_state = 0u;
        vt->esc_param_active = 0u;
        vt->esc_param_count = 0u;
        vt->esc_param_value = 0u;
        return 1;
    }

    vt->esc_state = 0u;
    return 0;
}

static void console_vt_init_defaults(console_vt_t* vt, uint32_t fg, uint32_t bg) {
    if (!vt) {
        return;
    }
    vt->default_fg = fg;
    vt->default_bg = bg;
    vt->fg = fg;
    vt->bg = bg;
    vt->cursor_col = 0u;
    vt->cursor_row = 0u;
    vt->cells = NULL;
    console_vt_reset_escape(vt);
}

static void console_release_vts(void) {
    for (uint32_t i = 0u; i < CONSOLE_TTY_MAX; i++) {
        if (g_console.vts[i].cells) {
            kfree(g_console.vts[i].cells);
            g_console.vts[i].cells = NULL;
        }
    }
}

void console_init(boot_info_t* boot, uint32_t fg, uint32_t bg) {
    uint64_t cell_count64;
    size_t cell_bytes = 0u;

    klog_init();
    console_release_vts();

    g_console.boot = boot;
    g_console.mode = CONSOLE_MODE_TEXT;
    g_console.active_vt = 0u;
    g_console.vt_count = 1u;
    g_console.cols = 1u;
    g_console.rows = 1u;

    for (uint32_t i = 0u; i < CONSOLE_TTY_MAX; i++) {
        console_vt_init_defaults(&g_console.vts[i], fg, bg);
    }

    if (!boot) {
        return;
    }

    graphics_set_mode(boot, g_console.mode);

    g_console.cols = boot->fb.width / FONT_WIDTH;
    g_console.rows = boot->fb.height / FONT_HEIGHT;
    if (g_console.cols == 0u) {
        g_console.cols = 1u;
    }
    if (g_console.rows == 0u) {
        g_console.rows = 1u;
    }

    cell_count64 = (uint64_t)g_console.cols * (uint64_t)g_console.rows;
    if (cell_count64 != 0u && cell_count64 <= ((uint64_t)(~(size_t)0) / (uint64_t)sizeof(console_cell_t))) {
        cell_bytes = (size_t)(cell_count64 * (uint64_t)sizeof(console_cell_t));
    }

    if (cell_bytes != 0u) {
        uint32_t allocated = 0u;
        for (uint32_t i = 0u; i < CONSOLE_TTY_MAX; i++) {
            console_cell_t* cells = (console_cell_t*)kmalloc(cell_bytes);
            if (!cells) {
                break;
            }
            g_console.vts[i].cells = cells;
            allocated = i + 1u;
        }
        if (allocated > 0u) {
            g_console.vt_count = allocated;
        }
    }

    for (uint32_t i = 0u; i < g_console.vt_count; i++) {
        console_vt_fill_cells(&g_console.vts[i], ' ', fg, bg);
    }

    if (!g_console_registered && device_register(MYAOS_DEV_CONSOLE, "fb0", "framebuffer", NULL, NULL, NULL) == 0) {
        g_console_registered = 1;
    }

    console_reset();
}

void console_reset(void) {
    console_vt_t* vt = console_vt_active();

    if (!g_console.boot || !vt) {
        return;
    }

    vt->fg = vt->default_fg;
    vt->bg = vt->default_bg;
    vt->cursor_col = 0u;
    vt->cursor_row = 0u;
    console_vt_reset_escape(vt);
    console_vt_fill_cells(vt, ' ', vt->fg, vt->bg);

    if (g_console.mode == CONSOLE_MODE_TEXT) {
        console_vt_render(vt);
    }
}

void console_put_char(char c) {
    console_vt_t* vt = console_vt_active();

    console_debug_putc(c);

    if (!g_console.boot || !vt) {
        return;
    }
    if (console_handle_escape(vt, c)) {
        return;
    }
    if (g_console.mode == CONSOLE_MODE_GRAPHICS) {
        return;
    }

    if (c == '\r') {
        vt->cursor_col = 0u;
        return;
    }
    if (c == '\n') {
        console_newline(vt);
        return;
    }

    if (c == '\b') {
        if (vt->cursor_col > 0u) {
            vt->cursor_col--;
        } else if (vt->cursor_row > 0u) {
            vt->cursor_row--;
            vt->cursor_col = g_console.cols - 1u;
        } else {
            return;
        }
        console_vt_set_cell(vt, vt->cursor_col, vt->cursor_row, ' ', vt->fg, vt->bg);
        console_vt_draw_cell(vt, vt->cursor_col, vt->cursor_row, ' ', vt->fg, vt->bg);
        return;
    }

    if (c < 32 || c > 126) {
        return;
    }

    console_vt_set_cell(vt, vt->cursor_col, vt->cursor_row, c, vt->fg, vt->bg);
    console_vt_draw_cell(vt, vt->cursor_col, vt->cursor_row, c, vt->fg, vt->bg);

    vt->cursor_col++;
    if (vt->cursor_col >= g_console.cols) {
        console_newline(vt);
    }
}

void console_write(const char* text) {
    size_t len = 0u;
    if (!text) {
        return;
    }
    while (text[len]) {
        len++;
    }
    console_write_len(text, len);
}

void console_write_len(const char* text, size_t len) {
    size_t i = 0u;

    if (!text) {
        return;
    }

    klog_write_len(text, len);

    while (i < len) {
        uint32_t cp = 0u;
        size_t consumed = 1u;

        if (utf8_decode_one((const uint8_t*)text + i, len - i, &cp, &consumed) != 0) {
            cp = 0xFFFDu;
            consumed = 1u;
        }
        console_put_char(console_codepoint_to_char(cp));
        i += consumed;
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
    console_vt_t* vt = console_vt_active();

    if (out_col) {
        *out_col = vt ? vt->cursor_col : 0u;
    }
    if (out_row) {
        *out_row = vt ? vt->cursor_row : 0u;
    }
}

uint32_t console_cols(void) {
    return g_console.cols;
}

void console_draw_cell(uint32_t col, uint32_t row, char c) {
    console_vt_t* vt = console_vt_active();
    console_cell_t* cell;
    uint32_t fg;
    uint32_t bg;

    if (!g_console.boot || !vt || col >= g_console.cols || row >= g_console.rows) {
        return;
    }

    c = console_cell_sanitize(c);
    cell = console_cell_at(vt, col, row);
    if (cell) {
        cell->ch = c;
        fg = cell->fg;
        bg = cell->bg;
    } else {
        fg = vt->fg;
        bg = vt->bg;
    }
    console_vt_draw_cell(vt, col, row, c, fg, bg);
}

void console_set_mode(uint32_t mode) {
    console_vt_t* vt;

    if (mode != CONSOLE_MODE_TEXT && mode != CONSOLE_MODE_GRAPHICS) {
        return;
    }
    if (g_console.mode == mode) {
        return;
    }
    g_console.mode = mode;
    graphics_set_mode(g_console.boot, mode);
    if (mode == CONSOLE_MODE_TEXT) {
        vt = console_vt_active();
        if (vt) {
            console_vt_render(vt);
        }
    }
}

uint32_t console_get_mode(void) {
    return g_console.mode;
}

uint32_t console_tty_count(void) {
    return g_console.vt_count;
}

uint32_t console_tty_active(void) {
    return g_console.active_vt;
}

int console_tty_switch(uint32_t index) {
    console_vt_t* vt;

    if (index >= g_console.vt_count) {
        return -1;
    }

    g_console.active_vt = index;
    vt = console_vt_active();
    if (g_console.mode == CONSOLE_MODE_TEXT && vt) {
        console_vt_render(vt);
    }
    return 0;
}

boot_info_t* console_boot_info(void) {
    return g_console.boot;
}
