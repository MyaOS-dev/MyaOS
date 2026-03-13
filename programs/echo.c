static int program_echo_run(const char* args) {
    const char* exec_args = args ? args : "";
    console_write(&g_shell, exec_args);
    console_put_char(&g_shell, '\n');
    return 0;
}
