#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>

#define RAMFS_MAX_FILES 32
#define RAMFS_NAME_MAX 24
#define RAMFS_DATA_MAX 512

typedef struct {
    uint8_t used;
    char name[RAMFS_NAME_MAX];
    uint32_t size;
    char data[RAMFS_DATA_MAX];
} ramfs_file_t;

typedef struct {
    ramfs_file_t files[RAMFS_MAX_FILES];
} ramfs_t;

void ramfs_init(ramfs_t* fs);
int ramfs_write(ramfs_t* fs, const char* name, const char* data);
int ramfs_read(const ramfs_t* fs, const char* name, const char** out_data, uint32_t* out_size);
int ramfs_remove(ramfs_t* fs, const char* name);
void ramfs_clear(ramfs_t* fs);
uint32_t ramfs_count(const ramfs_t* fs);
const ramfs_file_t* ramfs_file_at(const ramfs_t* fs, uint32_t index);

#endif
