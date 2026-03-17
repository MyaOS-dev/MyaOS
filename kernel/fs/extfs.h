#ifndef EXTFS_H
#define EXTFS_H

#include <stddef.h>
#include <stdint.h>

#define EXTFS_ROOT_INODE 2u
#define EXTFS_NAME_MAX 64u

typedef struct {
    char name[EXTFS_NAME_MAX];
    uint32_t inode;
    uint32_t size;
    uint8_t is_dir;
} extfs_dirent_t;

typedef struct {
    uint8_t* image;
    uint64_t image_size;
    uint64_t blocks_count;
    uint32_t inodes_count;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t first_data_block;
    uint32_t inode_size;
    uint32_t first_inode;
    uint32_t group_count;
    uint32_t gdt_block;
    uint16_t desc_size;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t fs_kind;
    uint8_t write_level;
    uint8_t mounted;
    uint8_t dirty;
} extfs_t;

int extfs_probe_image(const uint8_t* image, uint64_t image_size, char* out_name, size_t out_size);
int extfs_mount(extfs_t* fs, uint8_t* image, uint64_t image_size);
int extfs_kind_name(const extfs_t* fs, char* out_name, size_t out_size);
uint8_t extfs_write_level(const extfs_t* fs);

int extfs_list_dir(
    const extfs_t* fs,
    uint32_t dir_inode,
    extfs_dirent_t* entries,
    size_t max_entries,
    size_t* out_count
);
int extfs_lookup(const extfs_t* fs, uint32_t dir_inode, const char* name, extfs_dirent_t* out_entry);
int extfs_read_file(
    const extfs_t* fs,
    uint32_t inode,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
);
int extfs_is_dir(const extfs_t* fs, uint32_t inode, int* out_is_dir);
int extfs_create_file(extfs_t* fs, uint32_t dir_inode, const char* name);
int extfs_mkdir(extfs_t* fs, uint32_t dir_inode, const char* name);
int extfs_write_file(extfs_t* fs, uint32_t dir_inode, const char* name, const uint8_t* data, uint32_t size);
int extfs_delete_file(extfs_t* fs, uint32_t dir_inode, const char* name);

#endif
