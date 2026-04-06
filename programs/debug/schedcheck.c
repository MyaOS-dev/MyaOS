#include "../lib/myaos.h"

#define SCHEDCHECK_SAMPLE_TICKS 160u

static const myaos_proc_info_t* find_proc(const myaos_proc_info_t* procs, uint32_t count, int32_t pid) {
    for (uint32_t i = 0; i < count; i++) {
        if ((int32_t)procs[i].pid == pid) {
            return &procs[i];
        }
    }
    return NULL;
}

int program_main(int argc, char** argv) {
    const char* burn_argv[] = { "/bin/cpuburn.elf", NULL };
    myaos_spawn_opts_t low_opts;
    myaos_spawn_opts_t high_opts;
    myaos_proc_info_t procs[32];
    uint32_t count = 0;
    int32_t low_pid = -1;
    int32_t high_pid = -1;
    const myaos_proc_info_t* low_info;
    const myaos_proc_info_t* high_info;
    int32_t exit_code = 0;

    (void)argc;
    (void)argv;

    low_opts.flags = MYAOS_SPAWN_BACKGROUND;
    low_opts.stdout_append = 0u;
    low_opts.priority = MYAOS_PROC_PRIO_LOW;
    low_opts.reserved0 = 0u;
    low_opts.cpu_limit_ticks = 0u;
    low_opts.stdout_path[0] = '\0';

    high_opts.flags = MYAOS_SPAWN_BACKGROUND;
    high_opts.stdout_append = 0u;
    high_opts.priority = MYAOS_PROC_PRIO_HIGH;
    high_opts.reserved0 = 0u;
    high_opts.cpu_limit_ticks = 0u;
    high_opts.stdout_path[0] = '\0';

    if (mya_proc_spawn_ex("/bin/cpuburn.elf", 1, burn_argv, &low_opts, &low_pid) != 0) {
        mya_putln("schedcheck: failed to start low-priority burner");
        return 1;
    }
    if (mya_proc_spawn_ex("/bin/cpuburn.elf", 1, burn_argv, &high_opts, &high_pid) != 0) {
        (void)mya_proc_kill(low_pid, 0);
        mya_putln("schedcheck: failed to start high-priority burner");
        return 1;
    }

    mya_proc_sleep(SCHEDCHECK_SAMPLE_TICKS);

    if (mya_proc_list(procs, 32, &count) != 0) {
        (void)mya_proc_kill(low_pid, 0);
        (void)mya_proc_kill(high_pid, 0);
        mya_putln("schedcheck: failed to list processes");
        return 1;
    }

    low_info = find_proc(procs, count, low_pid);
    high_info = find_proc(procs, count, high_pid);
    if (!low_info || !high_info) {
        (void)mya_proc_kill(low_pid, 0);
        (void)mya_proc_kill(high_pid, 0);
        mya_putln("schedcheck: burner process disappeared");
        return 1;
    }

    mya_puts("schedcheck low cpu_ticks=");
    mya_put_u64(low_info->cpu_ticks_used);
    mya_puts(" run_count=");
    mya_put_u64(low_info->run_count);
    mya_puts("\n");

    mya_puts("schedcheck high cpu_ticks=");
    mya_put_u64(high_info->cpu_ticks_used);
    mya_puts(" run_count=");
    mya_put_u64(high_info->run_count);
    mya_puts("\n");

    (void)mya_proc_kill(low_pid, 0);
    (void)mya_proc_kill(high_pid, 0);
    (void)mya_proc_wait(low_pid, &exit_code);
    (void)mya_proc_wait(high_pid, &exit_code);

    if (high_info->cpu_ticks_used <= low_info->cpu_ticks_used) {
        mya_putln("schedcheck: priority scheduling check failed");
        return 1;
    }

    mya_putln("schedcheck: priority scheduling check passed");
    return 0;
}
