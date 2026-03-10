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
} boot_info_t;
#endif