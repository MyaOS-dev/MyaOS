#ifndef VFS_FAT32_H
#define VFS_FAT32_H

#include "vfs.h"
#include "fat32.h"
#include "boot.h"
#include "blockio.h"
#include <stdint.h>

typedef struct {
    fat32_fs_t fs;
    boot_info_t* boot;
    const blockio_disk_t* disk;
    uint8_t demo_mode;
    uint8_t dirty;
} vfs_fat32_t;

int vfs_fat32_mount_boot(vfs_fat32_t* mount, boot_info_t* boot);
int vfs_fat32_mount_disk(vfs_fat32_t* mount, const blockio_disk_t* disk);
const vfs_ops_t* vfs_fat32_ops(void);
int vfs_fat32_demo_mode(const vfs_fat32_t* mount);

#endif
