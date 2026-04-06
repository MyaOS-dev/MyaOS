#ifndef VFS_RAMFS_H
#define VFS_RAMFS_H

#include "vfs.h"
#include "ramfs.h"

typedef struct {
    ramfs_t fs;
} vfs_ramfs_t;

void vfs_ramfs_init(vfs_ramfs_t* mount);
const vfs_ops_t* vfs_ramfs_ops(void);

#endif
