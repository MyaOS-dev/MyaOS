#ifndef FAT32_H
#define FAT32_H

#include <stddef.h>
#include <stdint.h>

#define FAT32_NAME_MAX 13

typedef struct {
    char name[FAT32_NAME_MAX];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t is_dir;
} fat32_dirent_t;

typedef struct {
    uint8_t* image;
    uint64_t image_size;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t root_cluster;
    uint32_t first_data_sector;
    uint8_t mounted;
} fat32_fs_t;

int fat32_mount(fat32_fs_t* fs, uint8_t* image, uint64_t image_size);
int fat32_mount_demo(fat32_fs_t* fs);
uint32_t fat32_root_cluster(const fat32_fs_t* fs);
int fat32_list_dir(
    const fat32_fs_t* fs,
    uint32_t dir_cluster,
    fat32_dirent_t* entries,
    size_t max_entries,
    size_t* out_count
);
int fat32_lookup(
    const fat32_fs_t* fs,
    uint32_t dir_cluster,
    const char* name,
    fat32_dirent_t* out
);
int fat32_read_file(
    const fat32_fs_t* fs,
    uint32_t dir_cluster,
    const char* name,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
);
int fat32_mkdir(fat32_fs_t* fs, uint32_t dir_cluster, const char* name);
int fat32_create_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* name);
int fat32_write_file(
    fat32_fs_t* fs,
    uint32_t dir_cluster,
    const char* name,
    const uint8_t* data,
    uint32_t size
);

/* Backward-compatible wrappers for root directory commands. */
int fat32_list_root(const fat32_fs_t* fs, fat32_dirent_t* entries, size_t max_entries, size_t* out_count);
int fat32_read_root_file(
    const fat32_fs_t* fs,
    const char* name,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
);

#endif
