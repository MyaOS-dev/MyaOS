#include "libc.h"

int program_main(int argc, char** argv) {
    uint64_t pid = 0;
    uint64_t bits = 0;

    if (argc < 3) {
        mya_putln("usage: notify <pid> <bits>");
        return 1;
    }

    if (mya_strto_u64(argv[1], &pid) != 0 || mya_strto_u64(argv[2], &bits) != 0) {
        mya_putln("notify: invalid arguments");
        return 1;
    }

    if (mya_proc_notify((int32_t)pid, (uint32_t)bits) != 0) {
        mya_putln("notify: failed");
        return 1;
    }

    return 0;
}
