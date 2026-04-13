#ifndef NTFS_H
#define NTFS_H

#include <myaos/syscall.h>
#include <stddef.h>
#include <stdint.h>

#define NTFS_ROOT_RECORD 5ull
#define NTFS_MAX_RUNS 128u
#define NTFS_MAX_RECORD_SIZE 4096u
#define NTFS_MAX_INDEX_RECORD_SIZE 4096u

typedef struct {
    uint64_t vcn;
    int64_t lcn;
    uint64_t len_clusters;
} ntfs_run_t;

typedef struct {
    uint8_t* image;
    uint64_t image_size;
    uint64_t total_sectors;
    uint64_t mft_lcn;
    uint64_t mft_data_size;
    uint32_t cluster_size;
    uint32_t record_size;
    uint32_t index_record_size;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    ntfs_run_t mft_runs[NTFS_MAX_RUNS];
    uint32_t mft_run_count;
} ntfs_fs_t;

typedef struct {
    uint64_t record_no;
    uint64_t size;
    uint8_t is_dir;
    uint8_t name_namespace;
    char name[MYAOS_NAME_MAX];
} ntfs_dirent_t;

int ntfs_probe_image(const uint8_t* image, uint64_t image_size);
int ntfs_mount(ntfs_fs_t* fs, const uint8_t* image, uint64_t image_size);
int ntfs_list_dir(const ntfs_fs_t* fs, uint64_t dir_record, ntfs_dirent_t* out, uint32_t max_entries, uint32_t* out_count);
int ntfs_lookup(const ntfs_fs_t* fs, uint64_t dir_record, const char* name, ntfs_dirent_t* out);
int ntfs_read_file(const ntfs_fs_t* fs, uint64_t record_no, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size);
int ntfs_write_file(ntfs_fs_t* fs, uint64_t record_no, const uint8_t* data, uint32_t size);
int ntfs_record_is_dir(const ntfs_fs_t* fs, uint64_t record_no, int* out_is_dir);

#endif
