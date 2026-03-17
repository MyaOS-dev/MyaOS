#ifndef MYAFS_ONDISK_H
#define MYAFS_ONDISK_H

#include <stdint.h>

#define MYAFS_ONDISK_VERSION_MAJOR 2u
#define MYAFS_ONDISK_VERSION_MINOR 0u

#define MYAFS_SUPERBLOCK_MAGIC "MYAFSB2"
#define MYAFS_CHECKPOINT_MAGIC "MYACKP2"

#define MYAFS_V2_DEFAULT_BLOCK_SIZE 4096u
#define MYAFS_V2_DEFAULT_IMAGE_MIB 64u

#define MYAFS_V2_BOOT_BLOCKS 8u
#define MYAFS_V2_SUPERBLOCK_RING_BLOCKS 4u
#define MYAFS_V2_CHECKPOINT_RING_BLOCKS 32u
#define MYAFS_V2_ALLOCATOR_AREA_BLOCKS 64u
#define MYAFS_V2_TREE_AREA_BLOCKS 32u
#define MYAFS_V2_RECOVERY_LOG_BLOCKS 16u

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define MYAFS_PACKED
#else
#define MYAFS_PACKED __attribute__((packed))
#endif

typedef struct MYAFS_PACKED {
    uint64_t block;
    uint32_t level;
    uint32_t item_count;
    uint64_t block_checksum;
} myafs_tree_root_v2_t;

typedef struct MYAFS_PACKED {
    uint8_t magic[8];
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t header_bytes;

    uint64_t transaction_id;
    uint64_t generation;
    uint64_t timestamp_unix_ns;

    uint8_t fs_uuid[16];
    uint64_t feature_compat;
    uint64_t feature_ro_compat;
    uint64_t feature_incompat;

    myafs_tree_root_v2_t object_root;
    myafs_tree_root_v2_t extent_root;
    myafs_tree_root_v2_t directory_root;
    myafs_tree_root_v2_t allocator_root;
    myafs_tree_root_v2_t snapshot_root;

    uint64_t flags;
    uint64_t reserved[8];
    uint32_t checksum;
} myafs_checkpoint_v2_t;

typedef struct MYAFS_PACKED {
    uint8_t magic[8];
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t header_bytes;

    uint32_t block_size;
    uint32_t super_index;
    uint64_t total_blocks;

    uint64_t boot_start;
    uint64_t boot_blocks;

    uint64_t super_start;
    uint32_t super_count;
    uint32_t active_checkpoint;

    uint64_t checkpoint_start;
    uint32_t checkpoint_count;
    uint32_t checkpoint_blocks_per_entry;

    uint64_t allocator_start;
    uint64_t allocator_blocks;

    uint64_t object_tree_start;
    uint64_t extent_tree_start;
    uint64_t directory_tree_start;

    uint64_t data_start;

    uint64_t recovery_log_start;
    uint64_t recovery_log_blocks;

    uint64_t snapshot_meta_start;
    uint64_t snapshot_meta_blocks;

    uint8_t fs_uuid[16];
    char label[32];

    uint64_t feature_compat;
    uint64_t feature_ro_compat;
    uint64_t feature_incompat;

    uint64_t generation;
    uint64_t transaction_id;
    uint64_t checkpoint_timestamp_unix_ns;

    uint32_t flags;
    uint32_t checksum;
} myafs_superblock_v2_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

#undef MYAFS_PACKED

#endif

