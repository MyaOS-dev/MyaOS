#ifndef FS_DRIVER_H
#define FS_DRIVER_H

#include <stddef.h>
#include <stdint.h>

void fs_driver_init(void);
int fs_driver_probe_disk(uint32_t disk_id, char* out_name, size_t out_size);
int fs_driver_mount_source(const char* source, const char* mount_path, const char* fs_name);

#endif
