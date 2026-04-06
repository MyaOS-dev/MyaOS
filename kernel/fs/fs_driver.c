#include "fs_driver.h"
#include "blockio.h"
#include "extfs.h"
#include "fat32.h"
#include "ntfs.h"
#include "vfs.h"
#include "vfs_extfs.h"
#include "vfs_fat32.h"
#include "vfs_myafs.h"
#include "vfs_ramfs.h"
#include <myaos/syscall.h>

#define FS_DRIVER_FAT_MOUNTS VFS_MAX_MOUNTS
#define FS_DRIVER_EXT_MOUNTS VFS_MAX_MOUNTS
#define FS_DRIVER_RAMFS_MOUNTS 4u
#define FS_DRIVER_MYAFS_MOUNTS 4u

static vfs_fat32_t g_fat_mounts[FS_DRIVER_FAT_MOUNTS];
static uint8_t g_fat_mount_used[FS_DRIVER_FAT_MOUNTS];
static vfs_extfs_t g_ext_mounts[FS_DRIVER_EXT_MOUNTS];
static uint8_t g_ext_mount_used[FS_DRIVER_EXT_MOUNTS];
static vfs_ramfs_t g_ramfs_mounts[FS_DRIVER_RAMFS_MOUNTS];
static uint8_t g_ramfs_mount_used[FS_DRIVER_RAMFS_MOUNTS];
static vfs_myafs_t g_myafs_mounts[FS_DRIVER_MYAFS_MOUNTS];
static uint8_t g_myafs_mount_used[FS_DRIVER_MYAFS_MOUNTS];

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

    if (dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_eq_ci(const char* a, const char* b) {
    size_t i = 0;

    if (!a || !b) {
        return 0;
    }

    while (a[i] && b[i]) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int str_starts_with_ci(const char* text, const char* prefix) {
    size_t i = 0;

    if (!text || !prefix) {
        return 0;
    }

    while (prefix[i]) {
        char a = text[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int parse_u32(const char* text, uint32_t* out) {
    uint32_t value = 0;
    size_t i = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }

    while (text[i]) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
        i++;
    }

    *out = value;
    return 0;
}

static int parse_disk_source(const char* source, uint32_t* out_disk_id) {
    if (!source || !out_disk_id || !str_starts_with_ci(source, "disk")) {
        return -1;
    }
    return parse_u32(source + 4, out_disk_id);
}

static int probe_fat32_disk(const blockio_disk_t* disk) {
    fat32_fs_t fs;

    if (!disk || !disk->image_base || disk->image_size < 512u) {
        return 0;
    }

    return fat32_mount(&fs, disk->image_base, disk->image_size) == 0;
}

static int probe_myafs_disk(const blockio_disk_t* disk) {
    return vfs_myafs_probe_disk(disk);
}

static int probe_extfs_disk(const blockio_disk_t* disk, char* out_name, size_t out_size) {
    if (!disk || !disk->image_base) {
        return 0;
    }

    return extfs_probe_image(disk->image_base, disk->image_size, out_name, out_size);
}

static int probe_ntfs_disk(const blockio_disk_t* disk) {
    if (!disk || !disk->image_base) {
        return 0;
    }

    return ntfs_probe_image(disk->image_base, disk->image_size);
}

static int alloc_fat_mount_slot(void) {
    for (uint32_t i = 0; i < FS_DRIVER_FAT_MOUNTS; i++) {
        if (!g_fat_mount_used[i]) {
            g_fat_mount_used[i] = 1;
            return (int)i;
        }
    }
    return -1;
}

static void release_fat_mount_slot(int slot) {
    if (slot >= 0 && (uint32_t)slot < FS_DRIVER_FAT_MOUNTS) {
        g_fat_mount_used[slot] = 0;
    }
}

static int alloc_ext_mount_slot(void) {
    for (uint32_t i = 0; i < FS_DRIVER_EXT_MOUNTS; i++) {
        if (!g_ext_mount_used[i]) {
            g_ext_mount_used[i] = 1;
            return (int)i;
        }
    }
    return -1;
}

static void release_ext_mount_slot(int slot) {
    if (slot >= 0 && (uint32_t)slot < FS_DRIVER_EXT_MOUNTS) {
        g_ext_mount_used[slot] = 0;
    }
}

static int alloc_ramfs_mount_slot(void) {
    for (uint32_t i = 0; i < FS_DRIVER_RAMFS_MOUNTS; i++) {
        if (!g_ramfs_mount_used[i]) {
            g_ramfs_mount_used[i] = 1;
            return (int)i;
        }
    }
    return -1;
}

static void release_ramfs_mount_slot(int slot) {
    if (slot >= 0 && (uint32_t)slot < FS_DRIVER_RAMFS_MOUNTS) {
        g_ramfs_mount_used[slot] = 0;
    }
}

static int alloc_myafs_mount_slot(void) {
    for (uint32_t i = 0; i < FS_DRIVER_MYAFS_MOUNTS; i++) {
        if (!g_myafs_mount_used[i]) {
            g_myafs_mount_used[i] = 1;
            return (int)i;
        }
    }
    return -1;
}

static void release_myafs_mount_slot(int slot) {
    if (slot >= 0 && (uint32_t)slot < FS_DRIVER_MYAFS_MOUNTS) {
        g_myafs_mount_used[slot] = 0;
    }
}

static int mount_fat32_disk(uint32_t disk_id, const char* mount_path) {
    const blockio_disk_t* disk = blockio_get_disk(disk_id);
    int slot;

    if (!disk) {
        return -1;
    }

    slot = alloc_fat_mount_slot();
    if (slot < 0) {
        return -1;
    }

    if (vfs_fat32_mount_disk(&g_fat_mounts[slot], disk) != 0) {
        release_fat_mount_slot(slot);
        return -1;
    }

    if (vfs_mount(mount_path, "fat32", disk->name, disk_id, (uint8_t)disk->read_only, &g_fat_mounts[slot], vfs_fat32_ops()) != 0) {
        release_fat_mount_slot(slot);
        return -1;
    }

    return 0;
}

static int mount_ext_disk(uint32_t disk_id, const char* mount_path) {
    const blockio_disk_t* disk = blockio_get_disk(disk_id);
    int slot;
    char fs_name[MYAOS_NAME_MAX];
    uint8_t read_only;

    if (!disk) {
        return -1;
    }

    slot = alloc_ext_mount_slot();
    if (slot < 0) {
        return -1;
    }

    if (vfs_extfs_mount_disk(&g_ext_mounts[slot], disk) != 0) {
        release_ext_mount_slot(slot);
        return -1;
    }

    if (vfs_extfs_kind_name(&g_ext_mounts[slot], fs_name, sizeof(fs_name)) != 0) {
        release_ext_mount_slot(slot);
        return -1;
    }

    read_only = (uint8_t)(disk->read_only || vfs_extfs_write_level(&g_ext_mounts[slot]) == 0u);
    if (vfs_mount(
            mount_path,
            fs_name,
            disk->name,
            disk_id,
            read_only,
            &g_ext_mounts[slot],
            vfs_extfs_ops()
        ) != 0) {
        release_ext_mount_slot(slot);
        return -1;
    }

    return 0;
}

static int mount_ramfs(const char* mount_path) {
    int slot = alloc_ramfs_mount_slot();

    if (slot < 0) {
        return -1;
    }

    vfs_ramfs_init(&g_ramfs_mounts[slot]);
    if (vfs_mount(mount_path, "ramfs", "ramfs", MYAOS_INVALID_DISK_ID, 0, &g_ramfs_mounts[slot], vfs_ramfs_ops()) != 0) {
        release_ramfs_mount_slot(slot);
        return -1;
    }

    return 0;
}

static int mount_myafs(const char* mount_path) {
    int slot = alloc_myafs_mount_slot();

    if (slot < 0) {
        return -1;
    }

    vfs_myafs_init(&g_myafs_mounts[slot], "myafs");
    if (vfs_mount(mount_path, "myafs", "myafs", MYAOS_INVALID_DISK_ID, 0, &g_myafs_mounts[slot], vfs_myafs_ops()) != 0) {
        release_myafs_mount_slot(slot);
        return -1;
    }

    return 0;
}

static int mount_myafs_disk(uint32_t disk_id, const char* mount_path) {
    const blockio_disk_t* disk = blockio_get_disk(disk_id);
    int slot;

    if (!disk) {
        return -1;
    }

    slot = alloc_myafs_mount_slot();
    if (slot < 0) {
        return -1;
    }

    if (vfs_myafs_mount_disk(&g_myafs_mounts[slot], disk) != 0) {
        release_myafs_mount_slot(slot);
        return -1;
    }

    if (vfs_mount(mount_path, "myafs", disk->name, disk_id, (uint8_t)disk->read_only, &g_myafs_mounts[slot], vfs_myafs_ops()) != 0) {
        release_myafs_mount_slot(slot);
        return -1;
    }

    return 0;
}

void fs_driver_init(void) {
    for (uint32_t i = 0; i < FS_DRIVER_FAT_MOUNTS; i++) {
        g_fat_mount_used[i] = 0;
    }
    for (uint32_t i = 0; i < FS_DRIVER_EXT_MOUNTS; i++) {
        g_ext_mount_used[i] = 0;
    }
    for (uint32_t i = 0; i < FS_DRIVER_RAMFS_MOUNTS; i++) {
        g_ramfs_mount_used[i] = 0;
    }
    for (uint32_t i = 0; i < FS_DRIVER_MYAFS_MOUNTS; i++) {
        g_myafs_mount_used[i] = 0;
    }
}

int fs_driver_probe_disk(uint32_t disk_id, char* out_name, size_t out_size) {
    const blockio_disk_t* disk = blockio_get_disk(disk_id);
    char ext_name[MYAOS_NAME_MAX];

    if (!disk || !out_name || out_size == 0) {
        return -1;
    }

    if (probe_myafs_disk(disk)) {
        str_copy(out_name, "myafs", out_size);
    } else if (probe_fat32_disk(disk)) {
        str_copy(out_name, "fat32", out_size);
    } else if (probe_extfs_disk(disk, ext_name, sizeof(ext_name))) {
        str_copy(out_name, ext_name, out_size);
    } else if (probe_ntfs_disk(disk)) {
        str_copy(out_name, "ntfs", out_size);
    } else {
        str_copy(out_name, "unknown", out_size);
    }

    return 0;
}

int fs_driver_mount_source(const char* source, const char* mount_path, const char* fs_name) {
    uint32_t disk_id = 0;
    const blockio_disk_t* disk;
    char ext_name[MYAOS_NAME_MAX];

    if (!source || !mount_path) {
        return -1;
    }

    if (str_eq_ci(source, "ramfs")) {
        if (fs_name && fs_name[0] && !str_eq_ci(fs_name, "ramfs") && !str_eq_ci(fs_name, "auto")) {
            return -1;
        }
        return mount_ramfs(mount_path);
    }
    if (str_eq_ci(source, "myafs")) {
        if (fs_name && fs_name[0] && !str_eq_ci(fs_name, "myafs") && !str_eq_ci(fs_name, "auto")) {
            return -1;
        }
        return mount_myafs(mount_path);
    }

    if (parse_disk_source(source, &disk_id) != 0) {
        return -1;
    }
    disk = blockio_get_disk(disk_id);
    if (!disk) {
        return -1;
    }

    if (!fs_name || !fs_name[0] || str_eq_ci(fs_name, "auto")) {
        if (probe_myafs_disk(disk)) {
            return mount_myafs_disk(disk_id, mount_path);
        }
        if (probe_fat32_disk(disk)) {
            return mount_fat32_disk(disk_id, mount_path);
        }
        if (probe_extfs_disk(disk, ext_name, sizeof(ext_name))) {
            return mount_ext_disk(disk_id, mount_path);
        }
        if (probe_ntfs_disk(disk)) {
            return -2;
        }
        return -1;
    }

    if (str_eq_ci(fs_name, "myafs")) {
        return mount_myafs_disk(disk_id, mount_path);
    }
    if (str_eq_ci(fs_name, "fat32")) {
        return mount_fat32_disk(disk_id, mount_path);
    }
    if ((str_eq_ci(fs_name, "ext2") || str_eq_ci(fs_name, "ext3") || str_eq_ci(fs_name, "ext4")) &&
        probe_extfs_disk(disk, ext_name, sizeof(ext_name))) {
        return str_eq_ci(fs_name, ext_name) ? mount_ext_disk(disk_id, mount_path) : -1;
    }
    if (str_eq_ci(fs_name, "ntfs") && probe_ntfs_disk(disk)) {
        return -2;
    }

    return -1;
}
