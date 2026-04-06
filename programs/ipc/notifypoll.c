#include "libc.h"

int program_main(int argc, char** argv) {
    uint64_t mask = 0xFFFFFFFFu;
    uint32_t bits = 0;

    if (argc >= 2) {
        if (mya_strto_u64(argv[1], &mask) != 0) {
            mya_putln("notifypoll: invalid mask");
            return 1;
        }
    }

    if (mya_proc_notify_poll((uint32_t)mask, 1u, &bits) != 0) {
        mya_putln("none");
        return 1;
    }

    mya_puts("bits=");
    mya_put_u32(bits);
    mya_puts("\n");
    return 0;
}
