static int program_fatcat_run(const char* args) {
    if (!g_fat_ready) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }

    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args ? args : "", name, sizeof(name), &rest)) {
        console_write(&g_shell, "usage: fatcat FILE\n");
        return 0;
    }
    (void)rest;

    uint8_t data[SHELL_FATCAT_MAX + 1];
    uint32_t read_size = 0;
    int rc = fat32_read_file(&g_fat32, fat_current_cluster(), name, data, SHELL_FATCAT_MAX, &read_size);

    if (rc == -2) {
        console_write(&g_shell, "fat32 file not found\n");
        return 0;
    }
    if (rc == -3) {
        console_write(&g_shell, "fat32 target is directory\n");
        return 0;
    }
    if (rc == -4) {
        console_write(&g_shell, "fat32 file too large for buffer\n");
        return 0;
    }
    if (rc != 0) {
        console_write(&g_shell, "fat32 read failed\n");
        return 0;
    }
    if (read_size == 0) {
        console_write(&g_shell, "(empty)\n");
        return 0;
    }

    data[read_size] = 0;
    console_write_binary_as_text(&g_shell, data, read_size);
    if (data[read_size - 1] != '\n') {
        console_put_char(&g_shell, '\n');
    }
    return 0;
}
