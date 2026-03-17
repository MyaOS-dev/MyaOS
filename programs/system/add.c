#include "../lib/myaos.h"
#include <stdint.h>

static int parse_i64(const char* text, int64_t* out) {
    uint64_t value = 0;
    uint8_t negative = 0;
    size_t i = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }

    if (text[i] == '-') {
        negative = 1;
        i++;
    } else if (text[i] == '+') {
        i++;
    }

    if (!text[i]) {
        return -1;
    }

    for (; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
    }

    if (negative) {
        *out = -(int64_t)value;
    } else {
        *out = (int64_t)value;
    }
    return 0;
}

static void print_i64(int64_t value) {
    if (value < 0) {
        mya_puts("-");
        mya_put_u64((uint64_t)(-value));
        return;
    }
    mya_put_u64((uint64_t)value);
}

int program_main(int argc, char** argv) {
    char line[64];
    int64_t first = 0;
    int64_t second = 0;

    (void)argc;
    (void)argv;

    mya_puts("enter the first number: ");
    if (mya_console_readline(line, sizeof(line), 1) < 0 || parse_i64(line, &first) != 0) {
        mya_putln("invalid number");
        return 1;
    }

    mya_puts("enter the second number: ");
    if (mya_console_readline(line, sizeof(line), 1) < 0 || parse_i64(line, &second) != 0) {
        mya_putln("invalid number");
        return 1;
    }

    mya_puts("sum: ");
    print_i64(first + second);
    mya_puts("\n");
    return 0;
}
