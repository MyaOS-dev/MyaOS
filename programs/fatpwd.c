static int program_fatpwd_run(const char* args) {
    (void)args;

    if (!g_fat_ready) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }

    console_put_char(&g_shell, '/');
    for (uint32_t i = 1; i <= g_fat_path_depth; i++) {
        console_write(&g_shell, g_fat_path_names[i]);
        if (i != g_fat_path_depth) {
            console_put_char(&g_shell, '/');
        }
    }
    console_put_char(&g_shell, '\n');
    return 0;
}
