#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_sched_info_t info;
    (void)argc;
    (void)argv;

    if (mya_sched_info(&info) != 0) {
        mya_putln("sched info failed");
        return 1;
    }

    mya_puts("timer_hz: ");
    mya_put_u32(info.timer_hz);
    mya_puts("\n");
    mya_puts("timer_ticks: ");
    mya_put_u64(info.timer_ticks);
    mya_puts("\n");
    mya_puts("process_count: ");
    mya_put_u32(info.process_count);
    mya_puts("\n");
    mya_puts("current_pid: ");
    mya_put_u32(info.current_pid);
    mya_puts("\n");
    return 0;
}
