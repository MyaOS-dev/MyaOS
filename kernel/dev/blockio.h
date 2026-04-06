#ifndef BLOCKIO_H
#define BLOCKIO_H

#include "boot.h"
#include <myaos/syscall.h>
#include <stdint.h>

typedef struct {
    boot_info_t* boot;
    uint32_t disk_id;
    uint8_t* image_base;
    uint64_t image_size;
    uint64_t efi_block_io;
    uint64_t lba_start;
    uint64_t block_count;
    uint32_t media_id;
    uint32_t block_size;
    uint32_t read_only;
    uint32_t is_boot;
    char name[MYAOS_DEVICE_NAME_MAX];
} blockio_disk_t;

int blockio_init(boot_info_t* boot);
uint32_t blockio_disk_count(void);
const blockio_disk_t* blockio_get_disk(uint32_t disk_id);
int blockio_disk_info(uint32_t disk_id, myaos_disk_info_t* out);
int blockio_list_disks(myaos_disk_info_t* out, uint32_t max_entries, uint32_t* out_count);
int blockio_hotplug_ramdisk(uint64_t size_bytes, uint32_t block_size, uint32_t* out_disk_id);
int blockio_writeback_disk(const blockio_disk_t* disk);
int blockio_writeback(boot_info_t* boot);

#endif
