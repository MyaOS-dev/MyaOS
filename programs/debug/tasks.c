#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_proc_info_t procs[32];
    uint32_t count = 0;
    uint64_t total_ticks = 0u;
    (void)argc;
    (void)argv;

    if (mya_proc_list(procs, 32, &count) != 0) {
        mya_putln("ps failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        total_ticks += procs[i].cpu_ticks_used;
    }

    mya_puts("pid ppid prio state cpu% ticks name\n");
    for (uint32_t i = 0; i < count; i++) {
        uint64_t pct10 = (total_ticks == 0u) ? 0u : (procs[i].cpu_ticks_used * 1000u) / total_ticks;

        mya_put_u32(procs[i].pid);
        mya_puts(" ");
        mya_put_u32(procs[i].ppid);
        mya_puts(" ");
        mya_puts("p");
        mya_put_u32(procs[i].priority);
        mya_puts(" ");
        mya_puts(mya_proc_state_name(procs[i].state));
        mya_puts(" ");
        mya_put_u32((uint32_t)(pct10 / 10u));
        mya_puts(".");
        mya_put_u32((uint32_t)(pct10 % 10u));
        mya_puts(" ");
        mya_put_u64(procs[i].cpu_ticks_used);
        mya_puts(" ");
        mya_puts(procs[i].name);
        if (procs[i].background) {
            mya_puts(" &");
        }
        mya_puts("\n");
    }
    return 0;
}
