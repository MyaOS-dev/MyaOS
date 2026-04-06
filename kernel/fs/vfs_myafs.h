#ifndef VFS_MYAFS_H
#define VFS_MYAFS_H

#include "vfs.h"
#include "blockio.h"

#define MYAFS_MAX_NODES 64u
#define MYAFS_FILE_DATA_MAX 2048u

#define MYAFS_BACKING_MEM 0u
#define MYAFS_BACKING_LEGACY 1u
#define MYAFS_BACKING_V2 2u

typedef struct {
    uint8_t used;
    uint8_t is_dir;
    char path[MYAOS_PATH_MAX];
    uint32_t size;
    uint8_t data[MYAFS_FILE_DATA_MAX];
} myafs_node_t;

typedef struct {
    char label[MYAOS_NAME_MAX];
    myafs_node_t nodes[MYAFS_MAX_NODES];
    const blockio_disk_t* disk;
    uint64_t payload_offset;
    uint32_t payload_bytes;
    uint8_t backing;
    uint8_t dirty;
} vfs_myafs_t;

void vfs_myafs_init(vfs_myafs_t* mount, const char* label);
int vfs_myafs_probe_disk(const blockio_disk_t* disk);
int vfs_myafs_mount_disk(vfs_myafs_t* mount, const blockio_disk_t* disk);
const vfs_ops_t* vfs_myafs_ops(void);

#endif
