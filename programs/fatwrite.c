static int program_fatwrite_run(const char* args) {
    if (!g_fat_ready) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }

    char name[SHELL_TOKEN_MAX];
    const char* text = NULL;
    if (!parse_first_token(args ? args : "", name, sizeof(name), &text)) {
        console_write(&g_shell, "usage: fatwrite FILE TEXT\n");
        return 0;
    }

    int rc = fat32_write_file(
        &g_fat32,
        fat_current_cluster(),
        name,
        (const uint8_t*)text,
        (uint32_t)str_len(text)
    );
    if (rc == -3) {
        console_write(&g_shell, "fat32 target is directory\n");
        return 0;
    }
    if (rc != 0) {
        console_write(&g_shell, "fat32 write failed\n");
        return 0;
    }

    console_write(&g_shell, "fat32 write ok\n");
    g_fat_dirty = 1;
    return 0;
}
