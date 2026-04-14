#include "../lib/myaos.h"
#include <stdint.h>

int program_main(int argc, char** argv) {
    uint64_t pid_u64 = 0;
    int32_t exit_code = 143;
    int rc;

    if (argc < 2 || mya_strto_u64(argv[1], &pid_u64) != 0 || pid_u64 > 0x7FFFFFFFu) {
        mya_putln("usage: kill PID [EXIT_CODE]");
        return 1;
    }

    if (argc > 2) {
        uint64_t ec = 0;
        if (mya_strto_u64(argv[2], &ec) != 0 || ec > 0x7FFFFFFFu) {
            mya_putln("kill: bad exit code");
            return 1;
        }
        exit_code = (int32_t)ec;
    }

    rc = mya_proc_kill((int32_t)pid_u64, exit_code);
    if (rc != 0) {
        mya_puts("kill: ");
        mya_put_u32((uint32_t)pid_u64);
        mya_puts(": ");
        if (rc == -1) {
            mya_putln("cannot terminate process");
            mya_putln("hint: process may not exist, may already be finished, or is not your child");
        } else {
            mya_putln("kernel rejected terminate request");
            mya_putln("hint: run ps and retry with a valid process id");
        }
        return 1;
    }

    return 0;
}
