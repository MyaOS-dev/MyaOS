#include "vfs_extfs.h"

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

    if (dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

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

static int path_split_last(const char* path, char* parent_out, size_t parent_size, char* leaf_out, size_t leaf_size) {
    size_t len;
    size_t last = 0;

    if (!path || path[0] != '/') {
        return -1;
    }

    len = str_len(path);
    if (len <= 1) {
        return -1;
    }

    for (size_t i = 1; i < len; i++) {
        if (path[i] == '/') {
            last = i;
        }
    }

    if (last == 0) {
        str_copy(parent_out, "/", parent_size);
        str_copy(leaf_out, path + 1, leaf_size);
        return 0;
    }

    if (last >= parent_size) {
        return -1;
    }

    for (size_t i = 0; i < last && i + 1 < parent_size; i++) {
        parent_out[i] = path[i];
        parent_out[i + 1] = '\0';
    }
    str_copy(leaf_out, path + last + 1, leaf_size);
    return leaf_out[0] ? 0 : -1;
}

static int ext_walk_dir(vfs_extfs_t* mount, const char* path, uint32_t* out_inode) {
    uint32_t inode = EXTFS_ROOT_INODE;
    size_t i = 1;

    if (!mount || !path || !out_inode || path[0] != '/') {
        return -1;
    }
    if (str_eq(path, "/")) {
        *out_inode = inode;
        return 0;
    }

    while (path[i]) {
        char part[EXTFS_NAME_MAX];
        size_t start = i;
        size_t pos = 0;
        extfs_dirent_t entry;

        while (path[i] && path[i] != '/') {
            i++;
        }
        if (i == start) {
            i++;
            continue;
        }
        while (start < i && pos + 1 < sizeof(part)) {
            part[pos++] = path[start++];
        }
        part[pos] = '\0';

        if (extfs_lookup(&mount->fs, inode, part, &entry) != 0 || !entry.is_dir) {
            return -1;
        }
        inode = entry.inode;

        while (path[i] == '/') {
            i++;
        }
    }

    *out_inode = inode;
    return 0;
}

static int ext_lookup_path(vfs_extfs_t* mount, const char* path, extfs_dirent_t* out, uint32_t* out_parent) {
    char parent[MYAOS_PATH_MAX];
    char leaf[EXTFS_NAME_MAX];
    uint32_t parent_inode;

    if (str_eq(path, "/")) {
        if (out_parent) {
            *out_parent = EXTFS_ROOT_INODE;
        }
        if (out) {
            out->name[0] = '/';
            out->name[1] = '\0';
            out->inode = EXTFS_ROOT_INODE;
            out->is_dir = 1;
            out->size = 0;
        }
        return 0;
    }

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (ext_walk_dir(mount, parent, &parent_inode) != 0) {
        return -1;
    }
    if (out_parent) {
        *out_parent = parent_inode;
    }
    return extfs_lookup(&mount->fs, parent_inode, leaf, out);
}

static int ext_ops_list(void* ctx, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;
    extfs_dirent_t entries[64];
    uint32_t inode;
    size_t count = 0;

    if (ext_walk_dir(mount, path, &inode) != 0) {
        return -1;
    }
    if (extfs_list_dir(&mount->fs, inode, entries, 64u, &count) != 0) {
        return -1;
    }

    if (count > max_entries) {
        count = max_entries;
    }
    for (size_t i = 0; i < count; i++) {
        str_copy(out[i].name, entries[i].name, sizeof(out[i].name));
        out[i].type = entries[i].is_dir ? MYAOS_NODE_DIR : MYAOS_NODE_FILE;
        out[i].size = entries[i].size;
    }
    *out_count = count;
    return 0;
}

static int ext_ops_read_file(void* ctx, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;
    extfs_dirent_t entry;

    if (ext_lookup_path(mount, path, &entry, NULL) != 0 || entry.is_dir) {
        return -1;
    }
    return extfs_read_file(&mount->fs, entry.inode, out_buf, out_buf_size, out_size);
}

static int ext_ops_write_file(void* ctx, const char* path, const uint8_t* data, uint32_t size) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[EXTFS_NAME_MAX];
    uint32_t parent_inode;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (ext_walk_dir(mount, parent, &parent_inode) != 0) {
        return -1;
    }
    return extfs_write_file(&mount->fs, parent_inode, leaf, data, size);
}

static int ext_ops_mkdir(void* ctx, const char* path) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[EXTFS_NAME_MAX];
    uint32_t parent_inode;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (ext_walk_dir(mount, parent, &parent_inode) != 0) {
        return -1;
    }
    return extfs_mkdir(&mount->fs, parent_inode, leaf);
}

static int ext_ops_touch(void* ctx, const char* path) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[EXTFS_NAME_MAX];
    uint32_t parent_inode;
    extfs_dirent_t existing;
    int rc;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (ext_walk_dir(mount, parent, &parent_inode) != 0) {
        return -1;
    }

    rc = extfs_lookup(&mount->fs, parent_inode, leaf, &existing);
    if (rc == 0) {
        return existing.is_dir ? -1 : 0;
    }
    return extfs_create_file(&mount->fs, parent_inode, leaf);
}

static int ext_ops_remove(void* ctx, const char* path) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[EXTFS_NAME_MAX];
    uint32_t parent_inode;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (ext_walk_dir(mount, parent, &parent_inode) != 0) {
        return -1;
    }
    return extfs_delete_file(&mount->fs, parent_inode, leaf);
}

static int ext_ops_is_dir(void* ctx, const char* path) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;
    extfs_dirent_t entry;

    if (str_eq(path, "/")) {
        return 1;
    }
    if (ext_lookup_path(mount, path, &entry, NULL) != 0) {
        return -1;
    }
    return entry.is_dir ? 1 : 0;
}

static int ext_ops_sync(void* ctx) {
    vfs_extfs_t* mount = (vfs_extfs_t*)ctx;

    if (!mount || !mount->fs.dirty) {
        return 0;
    }
    if (!mount->disk) {
        return -1;
    }
    if (blockio_writeback_disk(mount->disk) != 0) {
        return -1;
    }
    mount->fs.dirty = 0;
    return 0;
}

static const vfs_ops_t g_ext_ops = {
    .list = ext_ops_list,
    .read_file = ext_ops_read_file,
    .write_file = ext_ops_write_file,
    .mkdir = ext_ops_mkdir,
    .touch = ext_ops_touch,
    .remove = ext_ops_remove,
    .is_dir = ext_ops_is_dir,
    .sync = ext_ops_sync,
};

int vfs_extfs_mount_disk(vfs_extfs_t* mount, const blockio_disk_t* disk) {
    if (!mount || !disk || !disk->image_base || disk->image_size < 2048u) {
        return -1;
    }

    mount->disk = disk;
    if (extfs_mount(&mount->fs, disk->image_base, disk->image_size) != 0) {
        return -1;
    }
    return 0;
}

const vfs_ops_t* vfs_extfs_ops(void) {
    return &g_ext_ops;
}

int vfs_extfs_kind_name(const vfs_extfs_t* mount, char* out_name, size_t out_size) {
    if (!mount) {
        return -1;
    }
    return extfs_kind_name(&mount->fs, out_name, out_size);
}

uint8_t vfs_extfs_write_level(const vfs_extfs_t* mount) {
    if (!mount) {
        return 0u;
    }
    return extfs_write_level(&mount->fs);
}
