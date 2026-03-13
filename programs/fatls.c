static int program_fatls_run(const char* args) {
    (void)args;

    if (!g_fat_ready) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }

    fat32_dirent_t entries[32];
    size_t count = 0;
    int rc = fat32_list_dir(&g_fat32, fat_current_cluster(), entries, 32, &count);

    if (rc == -1) {
        console_write(&g_shell, "fat32 read error\n");
        return 0;
    }
    if (count == 0) {
        console_write(&g_shell, "fat32 dir is empty\n");
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        console_write(&g_shell, entries[i].name);
        if (entries[i].is_dir) {
            console_write(&g_shell, " dir\n");
        } else {
            console_write(&g_shell, " ");
            console_write_u32(&g_shell, entries[i].size);
            console_write(&g_shell, " bytes\n");
        }
    }

    if (rc == -2) {
        console_write(&g_shell, "output truncated\n");
    }
    return 0;
}
