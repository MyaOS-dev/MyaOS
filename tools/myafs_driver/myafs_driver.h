#ifndef MYAFS_DRIVER_H
#define MYAFS_DRIVER_H

#include <stdint.h>

#define MYAFS_DRIVER_MAX_NODES 256u
#define MYAFS_DRIVER_PATH_MAX 256u
#define MYAFS_DRIVER_LABEL_MAX 32u
#define MYAFS_DRIVER_FILE_MAX 16384u

typedef struct {
    uint8_t used;
    uint8_t is_dir;
    char path[MYAFS_DRIVER_PATH_MAX];
    uint32_t size;
    uint8_t data[MYAFS_DRIVER_FILE_MAX];
} myafs_driver_node_t;

typedef struct {
    char name[MYAFS_DRIVER_PATH_MAX];
    uint8_t is_dir;
    uint32_t size;
} myafs_driver_dirent_t;

typedef struct {
    char label[MYAFS_DRIVER_LABEL_MAX];
    myafs_driver_node_t nodes[MYAFS_DRIVER_MAX_NODES];
} myafs_driver_t;

int myafs_driver_init(myafs_driver_t* fs, const char* label);
int myafs_driver_load(myafs_driver_t* fs, const char* image_path);
int myafs_driver_save(const myafs_driver_t* fs, const char* image_path);

int myafs_driver_list(
    const myafs_driver_t* fs,
    const char* path,
    myafs_driver_dirent_t* out,
    uint32_t max_entries,
    uint32_t* out_count
);

int myafs_driver_read(
    const myafs_driver_t* fs,
    const char* path,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
);

int myafs_driver_write(myafs_driver_t* fs, const char* path, const uint8_t* data, uint32_t size);
int myafs_driver_mkdir(myafs_driver_t* fs, const char* path);
int myafs_driver_touch(myafs_driver_t* fs, const char* path);
int myafs_driver_remove(myafs_driver_t* fs, const char* path);

#endif
