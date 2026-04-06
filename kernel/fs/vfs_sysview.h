#ifndef VFS_SYSVIEW_H
#define VFS_SYSVIEW_H

#include "vfs.h"
#include <stdint.h>

#define VFS_SYSVIEW_KIND_DEVICES 1u
#define VFS_SYSVIEW_KIND_PROCESSES 2u
#define VFS_SYSVIEW_KIND_CONFIG 3u

typedef struct {
    uint8_t kind;
} vfs_sysview_t;

void vfs_sysview_init(vfs_sysview_t* view, uint8_t kind);
const vfs_ops_t* vfs_sysview_ops(void);

#endif
