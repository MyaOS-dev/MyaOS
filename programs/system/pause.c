#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    uint64_t ticks = 1;

    if (argc > 1 && mya_strto_u64(argv[1], &ticks) != 0) {
        mya_putln("usage: sleep TICKS");
        return 1;
    }

    mya_proc_sleep(ticks);
    return 0;
}
