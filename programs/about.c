static int program_about_run(const char* args) {
    (void)args;
    console_write(&g_shell, "myaos kernel shell\n");
    console_write(&g_shell, "features: irq pmm heap paging fat32 ramfs\n");
    return 0;
}
