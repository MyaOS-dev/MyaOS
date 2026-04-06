#include "../lib/myaos.h"

static void print_disks(void) {
    myaos_disk_info_t disks[8];
    uint32_t count = 0;

    if (mya_disk_list(disks, 8, &count) != 0) {
        mya_putln("disk list failed");
        return;
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
}

static void print_mounts(void) {
    myaos_mount_info_t mounts[8];
    uint32_t count = 0;

    if (mya_fs_mounts(mounts, 8, &count) != 0) {
        mya_putln("mount list failed");
        return;
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
}

int program_main(int argc, char** argv) {
    const char* fs_name = "auto";

    if (argc < 3) {
        mya_putln("usage: attach SOURCE PATH [FSTYPE]");
        mya_putln("sources: disk0 disk1 ... or ramfs or myafs");
        mya_putln("");
        mya_putln("disks:");
        print_disks();
        mya_putln("");
        mya_putln("mounts:");
        print_mounts();
        return 0;
    }

    if (argc > 3) {
        fs_name = argv[3];
    }

    (void)mya_fs_mkdir(argv[2]);
    {
        int rc = mya_fs_mount(argv[1], argv[2], fs_name);
        if (rc == -2) {
            mya_putln("mount driver not implemented");
            return 1;
        }
        if (rc != 0) {
            mya_putln("mount failed");
            return 1;
        }
    }

    return 0;
}
