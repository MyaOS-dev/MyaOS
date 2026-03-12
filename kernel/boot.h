#ifndef BOOT_H
#define BOOT_H
#include <stdint.h>
typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t format;
} framebuffer_t;
typedef struct {
    framebuffer_t fb;
    uint64_t mmap;
    uint64_t mmap_size;
    uint64_t desc_size;
    uint64_t boot_disk_base;
    uint64_t boot_disk_size;
    uint64_t efi_reset_system;
    uint64_t efi_block_io;
    uint64_t boot_disk_lba_start;
    uint64_t boot_disk_block_count;
    uint32_t boot_disk_media_id;
    uint32_t boot_disk_block_size;
    uint32_t boot_disk_read_only;
    uint32_t boot_services_active;
} boot_info_t;
#endif
