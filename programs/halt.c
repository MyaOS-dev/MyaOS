static int program_halt_run(const char* args) {
    (void)args;
    fat_flush_if_needed(&g_shell);
    console_write(&g_shell, "cpu halted\n");
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
