static int program_fatmkdir_run(const char* args) {
    if (!g_fat_ready) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }

    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args ? args : "", name, sizeof(name), &rest)) {
        console_write(&g_shell, "usage: fatmkdir DIR\n");
        return 0;
    }
    (void)rest;

    int rc = fat32_mkdir(&g_fat32, fat_current_cluster(), name);
    if (rc == -2) {
        console_write(&g_shell, "fat32 entry exists\n");
        return 0;
    }
    if (rc != 0) {
        console_write(&g_shell, "fat32 mkdir failed\n");
        return 0;
    }

    console_write(&g_shell, "fat32 mkdir ok\n");
    g_fat_dirty = 1;
    return 0;
}
