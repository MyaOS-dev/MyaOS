#include "log.h"

#define KLOG_BUFFER_SIZE 16384u

static char g_klog_buf[KLOG_BUFFER_SIZE];
static uint32_t g_klog_head;
static uint32_t g_klog_len;

void klog_init(void) {
    g_klog_head = 0u;
    g_klog_len = 0u;
}

void klog_write_len(const char* text, size_t len) {
    if (!text || len == 0u) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        g_klog_buf[g_klog_head] = text[i];
        g_klog_head = (g_klog_head + 1u) % KLOG_BUFFER_SIZE;
        if (g_klog_len < KLOG_BUFFER_SIZE) {
            g_klog_len++;
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
