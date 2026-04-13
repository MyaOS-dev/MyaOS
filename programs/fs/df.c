#include "../lib/myaos.h"

#define DF_MAX_MOUNTS 32u
#define DF_MAX_DISKS 16u

static const myaos_disk_info_t* disk_by_id(const myaos_disk_info_t* disks, uint32_t disk_count, uint32_t id) {
    for (uint32_t i = 0; i < disk_count; i++) {
        if (disks[i].id == id) {
            return &disks[i];
        }
    }
    return NULL;
}

int program_main(int argc, char** argv) {
    myaos_mount_info_t mounts[DF_MAX_MOUNTS];
    myaos_disk_info_t disks[DF_MAX_DISKS];
    uint32_t mount_count = 0;
    uint32_t disk_count = 0;
    uint64_t total_bytes = 0;

    (void)argc;
    (void)argv;

    if (mya_fs_mounts(mounts, DF_MAX_MOUNTS, &mount_count) != 0) {
        mya_putln("df: failed to read mounts");
        return 1;
    }
    (void)mya_disk_list(disks, DF_MAX_DISKS, &disk_count);

    mya_putln("mount fs source size_bytes flags");
    for (uint32_t i = 0; i < mount_count; i++) {
        const myaos_disk_info_t* disk = NULL;

        mya_puts(mounts[i].path);
        mya_puts(" ");
        mya_puts(mounts[i].fs_name);
        mya_puts(" ");
        mya_puts(mounts[i].source[0] ? mounts[i].source : "-");
        mya_puts(" ");

        if (mounts[i].disk_id != MYAOS_INVALID_DISK_ID) {
            disk = disk_by_id(disks, disk_count, mounts[i].disk_id);
        }
        if (disk) {
            mya_put_u64(disk->size_bytes);
            total_bytes += disk->size_bytes;
        } else {
            mya_puts("-");
        }

        mya_puts(" ");
        if (mounts[i].read_only) {
            mya_puts("ro");
        } else {
            mya_puts("rw");
        }
        mya_puts("\n");
    }

    mya_puts("total_bytes=");
    mya_put_u64(total_bytes);
    mya_puts("\n");

    return 0;
}
