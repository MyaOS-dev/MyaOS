static int program_reboot_run(const char* args) {
    (void)args;
    fat_flush_if_needed(&g_shell);
    console_write(&g_shell, "rebooting\n");
    if (g_shell.boot) {
        power_reboot(g_shell.boot);
    }
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
