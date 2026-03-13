static int program_ramcat_run(const char* args) {
    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args ? args : "", name, sizeof(name), &rest)) {
        console_write(&g_shell, "usage: ramcat NAME\n");
        return 0;
    }
    (void)rest;

    const char* data = NULL;
    uint32_t size = 0;
    if (ramfs_read(&g_ramfs, name, &data, &size) != 0) {
        console_write(&g_shell, "ramfs file not found\n");
        return 0;
    }
    if (size == 0) {
        console_write(&g_shell, "(empty)\n");
        return 0;
    }

    console_write_binary_as_text(&g_shell, (const uint8_t*)data, size);
    if (data[size - 1] != '\n') {
        console_put_char(&g_shell, '\n');
    }
    return 0;
}
