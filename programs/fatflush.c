static int program_fatflush_run(const char* args) {
    (void)args;

    if (!g_fat_ready) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }
    if (g_fat_demo_mode) {
        console_write(&g_shell, "fat32 demo image cannot writeback\n");
        return 0;
    }
    if (!g_fat_dirty) {
        console_write(&g_shell, "fat32 clean\n");
        return 0;
    }

    int rc = blockio_writeback(g_shell.boot);
    if (rc != 0) {
        console_write(&g_shell, "fat32 writeback failed\n");
        return 0;
    }

    g_fat_dirty = 0;
    console_write(&g_shell, "fat32 writeback ok\n");
    return 0;
}
