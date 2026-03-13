static int program_clear_run(const char* args) {
    (void)args;
    console_reset(&g_shell);
    return 0;
}
