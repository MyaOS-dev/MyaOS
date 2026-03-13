static int program_ramls_run(const char* args) {
    (void)args;

    uint32_t count = ramfs_count(&g_ramfs);
    if (count == 0) {
        console_write(&g_shell, "ramfs is empty\n");
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        const ramfs_file_t* file = ramfs_file_at(&g_ramfs, i);
        if (!file) {
            continue;
        }

        console_write(&g_shell, file->name);
        console_write(&g_shell, " ");
        console_write_u32(&g_shell, file->size);
        console_write(&g_shell, " bytes\n");
    }

    return 0;
}
