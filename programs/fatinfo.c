static int program_fatinfo_run(const char* args) {
    (void)args;

    if (!g_fat32.mounted) {
        console_write(&g_shell, "fat32 not mounted\n");
        return 0;
    }

    console_write(&g_shell, "fat32 mounted\n");
    console_write(&g_shell, "bytes_per_sector: ");
    console_write_u32(&g_shell, g_fat32.bytes_per_sector);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "sectors_per_cluster: ");
    console_write_u32(&g_shell, g_fat32.sectors_per_cluster);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "reserved_sectors: ");
    console_write_u32(&g_shell, g_fat32.reserved_sectors);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "fat_count: ");
    console_write_u32(&g_shell, g_fat32.fat_count);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "sectors_per_fat: ");
    console_write_u32(&g_shell, g_fat32.sectors_per_fat);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "total_sectors: ");
    console_write_u32(&g_shell, g_fat32.total_sectors);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, g_fat_demo_mode ? "source: demo image\n" : "source: boot disk\n");

    return 0;
}
