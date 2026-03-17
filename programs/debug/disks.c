#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_disk_info_t disks[8];
    uint32_t count = 0;
    (void)argc;
    (void)argv;

    if (mya_disk_list(disks, 8, &count) != 0) {
        mya_putln("disks failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(disks[i].name);
        mya_puts(" ");
        mya_puts(disks[i].fs_name[0] ? disks[i].fs_name : "unknown");
        mya_puts(" ");
        mya_put_u64(disks[i].size_bytes);
        mya_puts(" bytes");
        if (disks[i].read_only) {
            mya_puts(" ro");
        }
        if (disks[i].is_boot) {
            mya_puts(" boot");
        }
        mya_puts("\n");
    }

    return 0;
}
