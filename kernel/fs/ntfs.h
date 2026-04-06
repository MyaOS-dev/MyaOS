#ifndef NTFS_H
#define NTFS_H

#include <stdint.h>

int ntfs_probe_image(const uint8_t* image, uint64_t image_size);

#endif
