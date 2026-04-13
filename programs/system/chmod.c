#include "../lib/myaos.h"
#include <stdint.h>

static int parse_octal_mode(const char* text, uint32_t* out_mode) {
    uint32_t value = 0;

    if (!text || !text[0] || !out_mode) {
        return -1;
    }

    for (uint32_t i = 0; text[i]; i++) {
        char c = text[i];
        if (c < '0' || c > '7') {
            return -1;
        }
        value = (value << 3) + (uint32_t)(c - '0');
        if (value > 0777u) {
            return -1;
        }
    }

    *out_mode = value;
    return 0;
}

static int path_exists(const char* path) {
    uint8_t probe = 0u;
    uint32_t read_size = 0u;
    myaos_dirent_t entries[1];
    uint32_t count = 0u;

    if (!path || !path[0]) {
        return 0;
    }
    if (mya_fs_read(path, &probe, sizeof(probe), &read_size) == 0) {
        return 1;
    }
    if (mya_fs_list(path, entries, 1u, &count) == 0) {
        return 1;
    }
    return 0;
}

int program_main(int argc, char** argv) {
    uint32_t mode = 0;

    if (argc < 3 || parse_octal_mode(argv[1], &mode) != 0) {
        mya_putln("usage: chmod <octal_mode> <path>");
        return 1;
    }

    if (mya_fs_chmod(argv[2], mode) != 0) {
        mya_puts("chmod: ");
        mya_puts(argv[2]);
        mya_puts(": ");
        if (!path_exists(argv[2])) {
            mya_putln("path not found");
            mya_putln("hint: verify path with ls");
        } else {
            mya_putln("permission denied");
            mya_putln("hint: only file owner or root can change mode");
        }
        return 1;
    }

    return 0;
}
