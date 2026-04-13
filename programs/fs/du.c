#include "../lib/myaos.h"

#define DU_LIST_MAX 64u
#define DU_MAX_DEPTH 32u

static int str_eq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int join_path(const char* base, const char* name, char* out, uint32_t out_size) {
    uint32_t pos = 0;

    if (!base || !name || !out || out_size == 0u) {
        return -1;
    }

    for (uint32_t i = 0; base[i]; i++) {
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

    for (uint32_t i = 0; name[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = name[i];
    }

    out[pos] = '\0';
    return 0;
}

static int split_parent_name(const char* path, char* out_parent, uint32_t parent_size, char* out_name, uint32_t name_size) {
    uint32_t len = 0;
    uint32_t last_slash = 0;

    if (!path || !path[0] || !out_parent || !out_name || parent_size == 0u || name_size == 0u) {
        return -1;
    }

    while (path[len]) {
        if (path[len] == '/') {
            last_slash = len;
        }
        len++;
    }

    if (last_slash == 0u) {
        out_parent[0] = '.';
        out_parent[1] = '\0';
    } else {
        uint32_t p = 0;
        for (uint32_t i = 0; i < last_slash && p + 1u < parent_size; i++) {
            out_parent[p++] = path[i];
        }
        out_parent[p] = '\0';
    }

    {
        uint32_t n = 0;
        for (uint32_t i = last_slash + 1u; path[i] && n + 1u < name_size; i++) {
            out_name[n++] = path[i];
        }
        out_name[n] = '\0';
    }

    return out_name[0] ? 0 : -1;
}

static int lookup_size_from_parent(const char* path, uint64_t* out_size, uint8_t* out_is_dir) {
    char parent[MYAOS_PATH_MAX];
    char name[MYAOS_NAME_MAX];
    myaos_dirent_t entries[DU_LIST_MAX];
    uint32_t count = 0;

    if (!out_size || !out_is_dir) {
        return -1;
    }
    *out_size = 0u;
    *out_is_dir = 0u;

    if (split_parent_name(path, parent, sizeof(parent), name, sizeof(name)) != 0) {
        return -1;
    }
    if (mya_fs_list(parent, entries, DU_LIST_MAX, &count) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!str_eq(entries[i].name, name)) {
            continue;
        }
        *out_size = entries[i].size;
        *out_is_dir = (entries[i].type == MYAOS_NODE_DIR || entries[i].type == MYAOS_NODE_MOUNT) ? 1u : 0u;
        return 0;
    }

    return -1;
}

static uint64_t walk_size(const char* path, uint32_t depth) {
    myaos_dirent_t entries[DU_LIST_MAX];
    uint32_t count = 0;
    uint64_t total = 0;

    if (depth == 0u) {
        return 0u;
    }

    if (mya_fs_list(path, entries, DU_LIST_MAX, &count) != 0) {
        uint8_t is_dir = 0u;
        if (lookup_size_from_parent(path, &total, &is_dir) == 0 && !is_dir) {
            mya_put_u64(total);
            mya_puts("\t");
            mya_putln(path);
            return total;
        }
        return 0u;
    }

    for (uint32_t i = 0; i < count; i++) {
        char child[MYAOS_PATH_MAX];

        if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) {
            continue;
        }
        if (join_path(path, entries[i].name, child, sizeof(child)) != 0) {
            continue;
        }

        if (entries[i].type == MYAOS_NODE_DIR || entries[i].type == MYAOS_NODE_MOUNT) {
            total += walk_size(child, depth - 1u);
        } else {
            total += entries[i].size;
        }
    }

    mya_put_u64(total);
    mya_puts("\t");
    mya_putln(path);
    return total;
}

int program_main(int argc, char** argv) {
    const char* path = (argc >= 2) ? argv[1] : ".";

    (void)walk_size(path, DU_MAX_DEPTH);
    return 0;
}
