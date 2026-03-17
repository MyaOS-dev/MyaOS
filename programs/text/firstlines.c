#include "libc.h"

#define HEAD_BUF_MAX 65536u

int program_main(int argc, char** argv) {
    static uint8_t buf[HEAD_BUF_MAX + 1u];
    uint32_t read_size = 0;
    uint64_t max_lines = 10;
    uint64_t lines = 0;

    if (argc < 2) {
        mya_putln("usage: head <file> [lines]");
        return 1;
    }

    if (argc >= 3) {
        if (mya_strto_u64(argv[2], &max_lines) != 0 || max_lines == 0) {
            mya_putln("head: invalid line count");
            return 1;
        }
    }

    if (mya_fs_read(argv[1], buf, HEAD_BUF_MAX, &read_size) != 0) {
        mya_putln("head: read failed");
        return 1;
    }

    for (uint32_t i = 0; i < read_size; i++) {
        mya_syscall(MYAOS_SYS_CONSOLE_WRITE, (uint64_t)(uintptr_t)&buf[i], 1, 0, 0, 0);
        if (buf[i] == '\n') {
            lines++;
            if (lines >= max_lines) {
                break;
            }
        }
    }

    if (read_size > 0 && buf[read_size - 1] != '\n' && lines < max_lines) {
        mya_puts("\n");
    }
    return 0;
}
