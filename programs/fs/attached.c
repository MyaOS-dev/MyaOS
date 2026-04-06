#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_mount_info_t mounts[8];
    uint32_t count = 0;
    (void)argc;
    (void)argv;

    if (mya_fs_mounts(mounts, 8, &count) != 0) {
        mya_putln("mounts failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(mounts[i].path);
        mya_puts(" -> ");
        mya_puts(mounts[i].fs_name);
        if (mounts[i].source[0]) {
            mya_puts(" from ");
            mya_puts(mounts[i].source);
        }
        if (mounts[i].read_only) {
            mya_puts(" ro");
        }
        mya_puts("\n");
    }
    return 0;
}
