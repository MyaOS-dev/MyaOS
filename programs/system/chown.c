#include "../lib/myaos.h"
#include <stdint.h>

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
    uint64_t uid = 0;

    if (argc < 3 || mya_strto_u64(argv[1], &uid) != 0 || uid > 0xFFFFFFFFu) {
        mya_putln("usage: chown <uid> <path>");
        return 1;
    }

    if (mya_fs_chown(argv[2], (uint32_t)uid) != 0) {
        mya_puts("chown: ");
        mya_puts(argv[2]);
        mya_puts(": ");
        if (!path_exists(argv[2])) {
            mya_putln("path not found");
            mya_putln("hint: verify path with ls");
        } else if (mya_sec_whoami() != 0u) {
            mya_putln("permission denied");
            mya_putln("hint: chown requires root user (uid 0)");
        } else {
            mya_putln("ownership update failed");
            mya_putln("hint: retry with a valid uid and existing path");
        }
        return 1;
    }

    return 0;
}
