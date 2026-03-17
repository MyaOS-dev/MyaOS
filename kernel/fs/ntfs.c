#include "ntfs.h"

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint64_t rd64(const uint8_t* p) {
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static int is_power_of_two(uint32_t value) {
    return value != 0 && (value & (value - 1u)) == 0;
}

int ntfs_probe_image(const uint8_t* image, uint64_t image_size) {
    static const char ntfs_oem[8] = {'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;

    if (!image || image_size < 512u) {
        return 0;
    }

    for (uint32_t i = 0; i < sizeof(ntfs_oem); i++) {
        if (image[3u + i] != (uint8_t)ntfs_oem[i]) {
            return 0;
        }
    }

    bytes_per_sector = rd16(image + 11u);
    sectors_per_cluster = image[13u];
    if (!is_power_of_two(bytes_per_sector) || bytes_per_sector < 256u || sectors_per_cluster == 0) {
        return 0;
    }
    if (rd64(image + 40u) == 0) {
        return 0;
    }

    return 1;
}
