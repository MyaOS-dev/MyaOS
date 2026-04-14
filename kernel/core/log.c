#include "log.h"
#include "timer.h"

#define KLOG_BUFFER_SIZE 16384u

static char g_klog_buf[KLOG_BUFFER_SIZE];
static uint32_t g_klog_head;
static uint32_t g_klog_len;
static uint8_t g_klog_line_begin;

static void ring_putc(char c) {
    g_klog_buf[g_klog_head] = c;
    g_klog_head = (g_klog_head + 1u) % KLOG_BUFFER_SIZE;
    if (g_klog_len < KLOG_BUFFER_SIZE) {
        g_klog_len++;
    }
}

static void u64_to_dec(uint64_t value, char* out, uint32_t out_size) {
    char rev[32];
    uint32_t n = 0u;
    uint32_t pos = 0u;

    if (!out || out_size == 0u) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0u && pos + 1u < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static void ring_puts(const char* text) {
    if (!text) {
        return;
    }
    for (uint32_t i = 0; text[i]; i++) {
        ring_putc(text[i]);
    }
}

static void ring_put_line_prefix(void) {
    char tick_text[32];

    ring_putc('[');
    u64_to_dec(timer_ticks(), tick_text, sizeof(tick_text));
    ring_puts(tick_text);
    ring_putc(']');
    ring_putc(' ');
}

void klog_init(void) {
    g_klog_head = 0u;
    g_klog_len = 0u;
    g_klog_line_begin = 1u;
}

void klog_write_len(const char* text, size_t len) {
    if (!text || len == 0u) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        if (g_klog_line_begin) {
            ring_put_line_prefix();
            g_klog_line_begin = 0u;
        }
        ring_putc(text[i]);
        if (text[i] == '\n' || text[i] == '\r') {
            g_klog_line_begin = 1u;
        }
    }
}

void klog_write(const char* text) {
    size_t len = 0;

    if (!text) {
        return;
    }
    while (text[len]) {
        len++;
    }
    klog_write_len(text, len);
}

int klog_snapshot(char* out, uint32_t out_size, uint32_t* out_written) {
    uint32_t copy_len;
    uint32_t start;

    if (!out || out_size == 0u || !out_written) {
        return -1;
    }

    copy_len = g_klog_len;
    if (copy_len >= out_size) {
        copy_len = out_size - 1u;
    }

    start = (g_klog_head + KLOG_BUFFER_SIZE - copy_len) % KLOG_BUFFER_SIZE;
    for (uint32_t i = 0; i < copy_len; i++) {
        out[i] = g_klog_buf[(start + i) % KLOG_BUFFER_SIZE];
    }
    out[copy_len] = '\0';
    *out_written = copy_len;
    return 0;
}
