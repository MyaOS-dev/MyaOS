#include "../lib/myaos.h"

#define ATTACH_DISK_MAX 16u

static int str_starts_with(const char* text, const char* prefix) {
    size_t i = 0;

    if (!text || !prefix) {
        return 0;
    }

    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_disk_info(myaos_disk_info_t* dst, const myaos_disk_info_t* src) {
    if (!dst || !src) {
        return;
    }

    dst->id = src->id;
    dst->block_size = src->block_size;
    dst->block_count = src->block_count;
    dst->size_bytes = src->size_bytes;
    dst->read_only = src->read_only;
    dst->is_boot = src->is_boot;
    str_copy(dst->name, src->name, sizeof(dst->name));
    str_copy(dst->fs_name, src->fs_name, sizeof(dst->fs_name));
}

static int lookup_disk_info(const char* source, myaos_disk_info_t* out_disk) {
    myaos_disk_info_t disks[ATTACH_DISK_MAX];
    uint32_t count = 0;
    uint64_t id64 = 0;
    uint32_t id = 0;

    if (!source || !out_disk || !str_starts_with(source, "disk")) {
        return -1;
    }
    if (mya_strto_u64(source + 4, &id64) != 0 || id64 > 0xFFFFFFFFull) {
        return -1;
    }
    if (mya_disk_list(disks, ATTACH_DISK_MAX, &count) != 0) {
        return -1;
    }

    id = (uint32_t)id64;
    for (uint32_t i = 0; i < count; i++) {
        if (disks[i].id == id) {
            copy_disk_info(out_disk, &disks[i]);
            return 0;
        }
    }

    return -1;
}

static void print_mount_error(const char* source, const char* fs_name, int rc) {
    myaos_disk_info_t disk;
    int have_disk = lookup_disk_info(source, &disk) == 0;

    if (rc == -2) {
        mya_puts("mount driver not implemented");
        if (have_disk && disk.fs_name[0]) {
            mya_puts(" for ");
            mya_puts(disk.fs_name);
        }
        mya_puts("\n");
        if (have_disk && disk.fs_name[0]) {
            mya_putln("hint: filesystem is detected, but this kernel build has no mount backend for it");
        }
        return;
    }

    mya_puts("mount failed");
    if (have_disk && disk.fs_name[0]) {
        mya_puts(" (detected ");
        mya_puts(disk.fs_name);
        mya_puts(")");
    }
    if (fs_name && fs_name[0] && !mya_streq(fs_name, "auto")) {
        mya_puts(" requested=");
        mya_puts(fs_name);
    }
    mya_puts("\n");
}

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
        if (rc != 0) {
            print_mount_error(argv[1], fs_name, rc);
            return 1;
        }
    }

    return 0;
}
