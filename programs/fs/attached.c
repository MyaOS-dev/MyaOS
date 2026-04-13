#include "../lib/myaos.h"

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0u;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int mount_runtime_persistence_uncertain(const myaos_mount_info_t* mount) {
    if (!mount || mount->read_only) {
        return 0;
    }
    return str_eq(mount->fs_name, "fat32") ||
           str_eq(mount->fs_name, "ext2") ||
           str_eq(mount->fs_name, "ext3") ||
           str_eq(mount->fs_name, "ext4") ||
           str_eq(mount->fs_name, "ntfs");
}

int program_main(int argc, char** argv) {
    myaos_mount_info_t mounts[16];
    uint32_t count = 0;
    (void)argc;
    (void)argv;

    if (mya_fs_mounts(mounts, 16, &count) != 0) {
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
        } else if (mount_runtime_persistence_uncertain(&mounts[i])) {
            mya_puts(" volatile");
        }
        mya_puts("\n");
    }
    return 0;
}
