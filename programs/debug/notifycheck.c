#include "libc.h"

int program_main(void) {
    const uint32_t test_bit = 0x20u;
    uint32_t bits = 0u;
    int32_t pid = mya_proc_getpid();

    if (pid <= 0) {
        mya_putln("notifycheck: bad pid");
        return 1;
    }

    if (mya_proc_notify(pid, test_bit) != 0) {
        mya_putln("notifycheck: send failed");
        return 1;
    }
    if (mya_proc_notify_wait(test_bit, 10u, 1u, &bits) != 0 || bits != test_bit) {
        mya_putln("notifycheck: wait failed");
        return 1;
    }

    bits = 0u;
    if (mya_proc_notify_poll(test_bit, 1u, &bits) == 0) {
        mya_putln("notifycheck: clear failed");
        return 1;
    }

    bits = 0xFFFFFFFFu;
    if (mya_proc_notify_wait(test_bit, 2u, 1u, &bits) != 0 || bits != 0u) {
        mya_putln("notifycheck: timeout failed");
        return 1;
    }

    mya_putln("notifycheck: ok");
    return 0;
}
