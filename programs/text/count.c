#include "libc.h"

#define WC_BUF_MAX 65536u

static void print_count(const char* label, uint64_t value) {
    mya_puts(label);
    mya_puts(": ");
    mya_put_u64(value);
    mya_puts("\n");
}

int program_main(int argc, char** argv) {
    static uint8_t buf[WC_BUF_MAX];
    uint32_t read_size = 0;
    uint64_t lines = 0;
    uint64_t words = 0;
    uint64_t bytes = 0;
    int in_word = 0;

    if (argc < 2) {
        mya_putln("usage: wc <file>");
        return 1;
    }

    if (mya_fs_read(argv[1], buf, sizeof(buf), &read_size) != 0) {
        mya_putln("wc: read failed");
        return 1;
    }

    for (uint32_t i = 0; i < read_size; i++) {
        uint8_t c = buf[i];
        bytes++;
        if (c == '\n') {
            lines++;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            in_word = 0;
        } else if (!in_word) {
            words++;
            in_word = 1;
        }
    }

    print_count("lines", lines);
    print_count("words", words);
    print_count("bytes", bytes);
    return 0;
}
