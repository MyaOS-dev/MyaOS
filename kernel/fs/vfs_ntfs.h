#ifndef VFS_NTFS_H
#define VFS_NTFS_H

#include "blockio.h"
#include "ntfs.h"
#include "vfs.h"

typedef struct {
    ntfs_fs_t fs;
    const blockio_disk_t* disk;
    uint8_t dirty;
    uint8_t volatile_mode;
} vfs_ntfs_t;

int vfs_ntfs_mount_disk(vfs_ntfs_t* mount, const blockio_disk_t* disk);
const vfs_ops_t* vfs_ntfs_ops(void);

#endif
