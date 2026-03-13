static int program_ramclear_run(const char* args) {
    (void)args;
    ramfs_clear(&g_ramfs);
    console_write(&g_shell, "ramfs cleared\n");
    return 0;
}
