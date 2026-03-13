static int program_ramrm_run(const char* args) {
    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args ? args : "", name, sizeof(name), &rest)) {
        console_write(&g_shell, "usage: ramrm NAME\n");
        return 0;
    }
    (void)rest;

    if (ramfs_remove(&g_ramfs, name) != 0) {
        console_write(&g_shell, "ramfs file not found\n");
        return 0;
    }
    console_write(&g_shell, "ramfs file removed\n");
    return 0;
}
