#include "../lib/myaos.h"
#include <stdint.h>

int program_main(int argc, char** argv) {
    uint64_t pid_u64 = 0;
    int32_t exit_code = 143;

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

    if (mya_proc_kill((int32_t)pid_u64, exit_code) != 0) {
        mya_putln("kill: failed");
        return 1;
    }

    return 0;
}
