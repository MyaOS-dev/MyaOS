#include "vfs_fat32.h"
#include "blockio.h"
#include "heap.h"

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

static int fat_walk_dir(vfs_fat32_t* mount, const char* dir_path, uint32_t* out_cluster) {
    uint32_t cluster;
    size_t i = 1;

    if (!mount || !out_cluster || !dir_path || dir_path[0] != '/') {
        return -1;
    }

    cluster = fat32_root_cluster(&mount->fs);
    if (str_eq(dir_path, "/")) {
        *out_cluster = cluster;
        return 0;
    }

    while (dir_path[i]) {
        char part[FAT32_NAME_MAX];
        size_t start = i;
        size_t pos = 0;
        fat32_dirent_t entry;

        while (dir_path[i] && dir_path[i] != '/') {
            i++;
        }
        if (i == start) {
            i++;
            continue;
        }
        while (start < i && pos + 1 < sizeof(part)) {
            part[pos++] = dir_path[start++];
        }
        part[pos] = '\0';

        if (fat32_lookup(&mount->fs, cluster, part, &entry) != 0 || !entry.is_dir) {
            return -1;
        }
        cluster = entry.first_cluster;

        while (dir_path[i] == '/') {
            i++;
        }
    }

    *out_cluster = cluster;
    return 0;
}

static int fat_lookup_path(vfs_fat32_t* mount, const char* path, fat32_dirent_t* out, uint32_t* out_parent) {
    char parent[MYAOS_PATH_MAX];
    char leaf[FAT32_NAME_MAX];
    uint32_t parent_cluster;

    if (str_eq(path, "/")) {
        if (out_parent) {
            *out_parent = fat32_root_cluster(&mount->fs);
        }
        if (out) {
            out->name[0] = '/';
            out->name[1] = '\0';
            out->is_dir = 1;
            out->first_cluster = fat32_root_cluster(&mount->fs);
            out->size = 0;
        }
        return 0;
    }

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (fat_walk_dir(mount, parent, &parent_cluster) != 0) {
        return -1;
    }
    if (out_parent) {
        *out_parent = parent_cluster;
    }
    return fat32_lookup(&mount->fs, parent_cluster, leaf, out);
}

static int fat_ops_list(void* ctx, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;
    fat32_dirent_t stack_entries[64];
    fat32_dirent_t* entries = stack_entries;
    uint32_t cluster;
    size_t list_cap = max_entries;
    size_t count = 0;
    int list_rc;

    if (!out || !out_count) {
        return -1;
    }
    if (max_entries == 0u) {
        *out_count = 0u;
        return 0;
    }
    if (fat_walk_dir(mount, path, &cluster) != 0) {
        return -1;
    }

    if (list_cap > 64u) {
        entries = (fat32_dirent_t*)kmalloc(list_cap * sizeof(fat32_dirent_t));
        if (!entries) {
            entries = stack_entries;
            list_cap = 64u;
        }
    }

    list_rc = fat32_list_dir(&mount->fs, cluster, entries, list_cap, &count);
    if (list_rc != 0 && list_rc != -2) {
        if (entries != stack_entries) {
            kfree(entries);
        }
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

    if (entries != stack_entries) {
        kfree(entries);
    }
    *out_count = count;
    return 0;
}

static int fat_ops_read_file(void* ctx, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[FAT32_NAME_MAX];
    uint32_t parent_cluster;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (fat_walk_dir(mount, parent, &parent_cluster) != 0) {
        return -1;
    }
    return fat32_read_file(&mount->fs, parent_cluster, leaf, out_buf, out_buf_size, out_size);
}

static int fat_ops_write_file(void* ctx, const char* path, const uint8_t* data, uint32_t size) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[FAT32_NAME_MAX];
    uint32_t parent_cluster;
    int rc;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (fat_walk_dir(mount, parent, &parent_cluster) != 0) {
        return -1;
    }

    rc = fat32_write_file(&mount->fs, parent_cluster, leaf, data, size);
    if (rc == 0) {
        mount->dirty = 1;
    }
    return rc;
}

static int fat_ops_mkdir(void* ctx, const char* path) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[FAT32_NAME_MAX];
    uint32_t parent_cluster;
    int rc;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (fat_walk_dir(mount, parent, &parent_cluster) != 0) {
        return -1;
    }

    rc = fat32_mkdir(&mount->fs, parent_cluster, leaf);
    if (rc == 0) {
        mount->dirty = 1;
    }
    return rc;
}

static int fat_ops_touch(void* ctx, const char* path) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[FAT32_NAME_MAX];
    uint32_t parent_cluster;
    int rc;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (fat_walk_dir(mount, parent, &parent_cluster) != 0) {
        return -1;
    }

    rc = fat32_create_file(&mount->fs, parent_cluster, leaf);
    if (rc == 0) {
        mount->dirty = 1;
    }
    return rc;
}

static int fat_ops_remove(void* ctx, const char* path) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;
    char parent[MYAOS_PATH_MAX];
    char leaf[FAT32_NAME_MAX];
    uint32_t parent_cluster;
    int rc;

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (fat_walk_dir(mount, parent, &parent_cluster) != 0) {
        return -1;
    }

    rc = fat32_delete_file(&mount->fs, parent_cluster, leaf);
    if (rc == 0) {
        mount->dirty = 1;
    }
    return rc;
}

static int fat_ops_is_dir(void* ctx, const char* path) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;
    fat32_dirent_t entry;

    if (str_eq(path, "/")) {
        return 1;
    }
    if (fat_lookup_path(mount, path, &entry, NULL) != 0) {
        return -1;
    }
    return entry.is_dir ? 1 : 0;
}

static int fat_ops_sync(void* ctx) {
    vfs_fat32_t* mount = (vfs_fat32_t*)ctx;

    if (!mount->dirty || mount->demo_mode) {
        return 0;
    }

    if (mount->disk) {
        if (blockio_writeback_disk(mount->disk) != 0) {
            return -1;
        }
    } else if (blockio_writeback(mount->boot) != 0) {
        return -1;
    }

    mount->dirty = 0;
    return 0;
}

static const vfs_ops_t g_fat_ops = {
    .list = fat_ops_list,
    .read_file = fat_ops_read_file,
    .write_file = fat_ops_write_file,
    .mkdir = fat_ops_mkdir,
    .touch = fat_ops_touch,
    .remove = fat_ops_remove,
    .is_dir = fat_ops_is_dir,
    .sync = fat_ops_sync,
};

int vfs_fat32_mount_boot(vfs_fat32_t* mount, boot_info_t* boot) {
    int rc;

    if (!mount) {
        return -1;
    }

    mount->boot = boot;
    mount->disk = NULL;
    mount->demo_mode = 0;
    mount->dirty = 0;

    rc = -1;
    if (boot && boot->boot_disk_base != 0 && boot->boot_disk_size >= 512) {
        rc = fat32_mount(&mount->fs, (uint8_t*)(uintptr_t)boot->boot_disk_base, boot->boot_disk_size);
    }
    if (rc != 0) {
        rc = fat32_mount_demo(&mount->fs);
        if (rc == 0) {
            mount->demo_mode = 1;
        }
    }
    return rc;
}

int vfs_fat32_mount_disk(vfs_fat32_t* mount, const blockio_disk_t* disk) {
    if (!mount || !disk || !disk->image_base || disk->image_size < 512u) {
        return -1;
    }

    mount->boot = disk->boot;
    mount->disk = disk;
    mount->demo_mode = 0;
    mount->dirty = 0;
    return fat32_mount(&mount->fs, disk->image_base, disk->image_size);
}

const vfs_ops_t* vfs_fat32_ops(void) {
    return &g_fat_ops;
}

int vfs_fat32_demo_mode(const vfs_fat32_t* mount) {
    return mount ? mount->demo_mode : 0;
}
