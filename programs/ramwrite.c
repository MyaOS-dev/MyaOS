static int program_ramwrite_run(const char* args) {
    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args ? args : "", name, sizeof(name), &rest)) {
        console_write(&g_shell, "usage: ramwrite NAME TEXT\n");
        return 0;
    }

    int rc = ramfs_write(&g_ramfs, name, rest);
    if (rc == -1) {
        console_write(&g_shell, "ramfs invalid name\n");
        return 0;
    }
    if (rc == -2) {
        console_write(&g_shell, "ramfs text too long\n");
        return 0;
    }
    if (rc == -3) {
        console_write(&g_shell, "ramfs is full\n");
        return 0;
    }

    console_write(&g_shell, "ramfs write ok\n");
    return 0;
}
