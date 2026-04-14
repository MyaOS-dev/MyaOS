#include "../lib/myaos.h"
#include <stdint.h>

#define FSCK_LIST_MAX 96u
#define FSCK_MAX_DEPTH 24u

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0u;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int build_child_path(const char* base, const char* name, char* out, uint32_t out_size) {
    uint32_t pos = 0u;

    if (!base || !name || !out || out_size == 0u) {
        return -1;
    }
    for (uint32_t i = 0u; base[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = base[i];
    }
    if (pos == 0u) {
        return -1;
    }
    if (pos > 1u && out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = '/';
    }
    for (uint32_t i = 0u; name[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = name[i];
    }
    out[pos] = '\0';
    return 0;
}

typedef struct {
    uint32_t dirs;
    uint32_t files;
    uint32_t errors;
} fsck_stats_t;

static void fsck_scan_file(const char* path, fsck_stats_t* stats) {
    uint8_t probe[1];
    uint32_t read = 0u;

    if (!path || !stats) {
        return;
    }
    stats->files++;
    if (mya_fs_read(path, probe, sizeof(probe), &read) != 0) {
        stats->errors++;
        mya_puts("fsck: read error ");
        mya_putln(path);
    }
}

static void fsck_scan_path(const char* path, uint32_t depth, fsck_stats_t* stats) {
    myaos_dirent_t entries[FSCK_LIST_MAX];
    uint32_t count = 0u;

    if (!path || !stats) {
        return;
    }
    if (depth == 0u) {
        stats->errors++;
        mya_puts("fsck: depth limit reached at ");
        mya_putln(path);
        return;
    }

    if (mya_fs_list(path, entries, FSCK_LIST_MAX, &count) != 0) {
        fsck_scan_file(path, stats);
        return;
    }

    stats->dirs++;
    for (uint32_t i = 0u; i < count; i++) {
        char child[MYAOS_PATH_MAX];
        int is_dir = (entries[i].type == MYAOS_NODE_DIR || entries[i].type == MYAOS_NODE_MOUNT) ? 1 : 0;

        if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) {
            continue;
        }
        if (build_child_path(path, entries[i].name, child, sizeof(child)) != 0) {
            stats->errors++;
            continue;
        }

        if (is_dir) {
            fsck_scan_path(child, depth - 1u, stats);
        } else {
            fsck_scan_file(child, stats);
        }
    }
}

int program_main(int argc, char** argv) {
    const char* path = "/";
    fsck_stats_t stats;

    stats.dirs = 0u;
    stats.files = 0u;
    stats.errors = 0u;

    if (argc >= 2) {
        path = argv[1];
    }

    mya_puts("fsck: scanning ");
    mya_putln(path);
    fsck_scan_path(path, FSCK_MAX_DEPTH, &stats);

    mya_puts("fsck: dirs=");
    mya_put_u32(stats.dirs);
    mya_puts(" files=");
    mya_put_u32(stats.files);
    mya_puts(" errors=");
    mya_put_u32(stats.errors);
    mya_puts("\n");

    return stats.errors == 0u ? 0 : 2;
}
