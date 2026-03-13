static int program_fatcd_run(const char* args) {
    if (!g_fat_ready) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }

    char target[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args ? args : "", target, sizeof(target), &rest)) {
        console_write(&g_shell, "usage: fatcd DIR\n");
        return 0;
    }
    (void)rest;

    if (str_eq(target, "/")) {
        fat_path_reset();
        return 0;
    }

    if (str_eq(target, "..")) {
        if (g_fat_path_depth > 0) {
            g_fat_path_depth--;
        }
        return 0;
    }

    fat32_dirent_t entry;
    int rc = fat32_lookup(&g_fat32, fat_current_cluster(), target, &entry);
    if (rc != 0) {
        console_write(&g_shell, "fat32 dir not found\n");
        return 0;
    }
    if (!entry.is_dir) {
        console_write(&g_shell, "fat32 target is not dir\n");
        return 0;
    }
    if (g_fat_path_depth + 1 >= FAT_PATH_DEPTH_MAX) {
        console_write(&g_shell, "fat32 path depth limit\n");
        return 0;
    }

    g_fat_path_depth++;
    g_fat_path_clusters[g_fat_path_depth] = entry.first_cluster;
    str_copy(g_fat_path_names[g_fat_path_depth], entry.name, FAT32_NAME_MAX);
    return 0;
}
