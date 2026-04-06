#ifndef VFS_EXTFS_H
#define VFS_EXTFS_H

#include "blockio.h"
#include "extfs.h"
#include "vfs.h"

typedef struct {
    extfs_t fs;
    const blockio_disk_t* disk;
} vfs_extfs_t;

int vfs_extfs_mount_disk(vfs_extfs_t* mount, const blockio_disk_t* disk);
const vfs_ops_t* vfs_extfs_ops(void);
int vfs_extfs_kind_name(const vfs_extfs_t* mount, char* out_name, size_t out_size);
uint8_t vfs_extfs_write_level(const vfs_extfs_t* mount);

#endif
