#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_proc_info_t procs[32];
    uint32_t count = 0;
    (void)argc;
    (void)argv;

    if (mya_proc_list(procs, 32, &count) != 0) {
        mya_putln("ps failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_put_u32(procs[i].pid);
        mya_puts(" ");
        mya_puts("p");
        mya_put_u32(procs[i].priority);
        mya_puts(" ");
        mya_puts(mya_proc_state_name(procs[i].state));
        mya_puts(" ");
        mya_puts(procs[i].name);
        if (procs[i].background) {
            mya_puts(" &");
        }
        mya_puts("\n");
    }
    return 0;
}
