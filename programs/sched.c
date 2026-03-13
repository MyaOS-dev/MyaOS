static int program_sched_run(const char* args) {
    (void)args;

    console_write(&g_shell, "timer_hz: ");
    console_write_u32(&g_shell, timer_hz());
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "timer_ticks: ");
    console_write_u64(&g_shell, timer_ticks());
    console_put_char(&g_shell, '\n');

    console_write(&g_shell, "task_count: ");
    uint32_t count = scheduler_task_count();
    console_write_u32(&g_shell, count);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "current_task_index: ");
    console_write_u32(&g_shell, scheduler_current_task());
    console_put_char(&g_shell, '\n');

    for (uint32_t i = 0; i < count; i++) {
        scheduler_task_stats_t stats;
        if (scheduler_get_task_stats(i, &stats) != 0) {
            continue;
        }
        console_write(&g_shell, "task ");
        console_write_u32(&g_shell, i);
        console_write(&g_shell, ": ");
        console_write(&g_shell, stats.name);
        console_write(&g_shell, " runs=");
        console_write_u64(&g_shell, stats.run_count);
        console_write(&g_shell, " last_tick=");
        console_write_u64(&g_shell, stats.last_run_tick);
        console_put_char(&g_shell, '\n');
    }

    return 0;
}
