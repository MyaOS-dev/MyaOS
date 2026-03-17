#include "libc.h"

#define GREP_BUF_MAX 65536u

static int contains(const char* line, size_t line_len, const char* needle) {
    size_t needle_len = mya_strlen(needle);
    if (needle_len == 0) {
        return 1;
    }
    if (line_len < needle_len) {
        return 0;
    }

    for (size_t i = 0; i + needle_len <= line_len; i++) {
        size_t j = 0;
        while (j < needle_len && line[i + j] == needle[j]) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

int program_main(int argc, char** argv) {
    static uint8_t buf[GREP_BUF_MAX + 1u];
    uint32_t read_size = 0;
    size_t line_start = 0;

    if (argc < 3) {
        mya_putln("usage: grep <pattern> <file>");
        return 1;
    }

    if (mya_fs_read(argv[2], buf, GREP_BUF_MAX, &read_size) != 0) {
        mya_putln("grep: read failed");
        return 1;
    }
    buf[read_size] = 0;

    for (size_t i = 0; i <= read_size; i++) {
        if (i == read_size || buf[i] == '\n') {
            size_t len = i - line_start;
            if (contains((const char*)buf + line_start, len, argv[1])) {
                size_t saved = i;
                buf[saved] = 0;
                mya_puts((const char*)buf + line_start);
                mya_puts("\n");
                buf[saved] = (saved == read_size) ? 0 : '\n';
            }
            line_start = i + 1;
        }
    }

    return 0;
}
