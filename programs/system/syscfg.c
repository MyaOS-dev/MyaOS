#include "../lib/myaos.h"

#define SYS_LIST_MAX 64u
#define SYS_VALUE_MAX 128u

static int build_sys_path(const char* key, char* out, uint32_t out_size) {
    uint32_t pos = 0;

    if (!key || !out || out_size < 6u) {
        return -1;
    }

    out[pos++] = '/';
    out[pos++] = 's';
    out[pos++] = 'y';
    out[pos++] = 's';
    out[pos++] = '/';

    for (uint32_t i = 0; key[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = key[i];
    }
    out[pos] = '\0';
    return 0;
}

static int print_sys_value(const char* key) {
    char path[MYAOS_PATH_MAX];
    uint8_t value[SYS_VALUE_MAX];
    uint32_t size = 0;

    if (build_sys_path(key, path, sizeof(path)) != 0) {
        return -1;
    }
    if (mya_fs_read(path, value, sizeof(value) - 1u, &size) != 0) {
        return -1;
    }

    value[size] = '\0';
    mya_puts(key);
    mya_puts("=");
    mya_puts((const char*)value);
    mya_puts("\n");
    return 0;
}

int program_main(int argc, char** argv) {
    myaos_dirent_t entries[SYS_LIST_MAX];
    uint32_t count = 0;

    if (argc > 1) {
        if (print_sys_value(argv[1]) != 0) {
            mya_putln("syscfg: failed to read key");
            return 1;
        }
        return 0;
    }

    if (mya_fs_list("/sys", entries, SYS_LIST_MAX, &count) != 0) {
        mya_putln("syscfg: failed to list /sys");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != MYAOS_NODE_FILE) {
            continue;
        }
        if (mya_streq(entries[i].name, "count")) {
            continue;
        }
        if (print_sys_value(entries[i].name) != 0) {
            mya_puts(entries[i].name);
            mya_putln("=<error>");
        }
    }

    return 0;
}
