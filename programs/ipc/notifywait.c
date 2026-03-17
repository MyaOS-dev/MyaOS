#include "libc.h"

int program_main(int argc, char** argv) {
    uint64_t mask = 0xFFFFFFFFu;
    uint64_t timeout_ticks = 100u;
    uint32_t bits = 0;

    if (argc >= 2) {
        if (mya_strto_u64(argv[1], &mask) != 0) {
            mya_putln("notifywait: invalid mask");
            return 1;
        }
    }
    if (argc >= 3) {
        if (mya_strto_u64(argv[2], &timeout_ticks) != 0) {
            mya_putln("notifywait: invalid timeout");
            return 1;
        }
    }

    if (mya_proc_notify_wait((uint32_t)mask, (uint32_t)timeout_ticks, 1u, &bits) != 0 || bits == 0u) {
        mya_putln("none");
        return 1;
    }

    mya_puts("bits=");
    mya_put_u32(bits);
    mya_puts("\n");
    return 0;
}
