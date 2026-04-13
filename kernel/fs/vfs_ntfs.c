#include "vfs_ntfs.h"
#include "heap.h"
#include "blockio.h"

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
        if (str_len(path + 1) >= leaf_size) {
            return -1;
        }
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
    if (str_len(path + last + 1) >= leaf_size) {
        return -1;
    }
    str_copy(leaf_out, path + last + 1, leaf_size);
    return leaf_out[0] ? 0 : -1;
}

static int ntfs_walk_dir(vfs_ntfs_t* mount, const char* dir_path, uint64_t* out_record) {
    uint64_t record_no = NTFS_ROOT_RECORD;
    size_t i = 1;

    if (!mount || !out_record || !dir_path || dir_path[0] != '/') {
        return -1;
    }
    if (str_eq(dir_path, "/")) {
        *out_record = record_no;
        return 0;
    }

    while (dir_path[i]) {
        char part[MYAOS_NAME_MAX];
        size_t start = i;
        size_t pos = 0;
        ntfs_dirent_t entry;

        while (dir_path[i] && dir_path[i] != '/') {
            i++;
        }
        if (i == start) {
            i++;
            continue;
        }
        if (i - start >= sizeof(part)) {
            return -1;
        }
        while (start < i && pos + 1 < sizeof(part)) {
            part[pos++] = dir_path[start++];
        }
        part[pos] = '\0';

        if (ntfs_lookup(&mount->fs, record_no, part, &entry) != 0 || !entry.is_dir) {
            return -1;
        }
        record_no = entry.record_no;

        while (dir_path[i] == '/') {
            i++;
        }
    }

    *out_record = record_no;
    return 0;
}

static int ntfs_lookup_path(vfs_ntfs_t* mount, const char* path, ntfs_dirent_t* out_entry) {
    char parent[MYAOS_PATH_MAX];
    char leaf[MYAOS_NAME_MAX];
    uint64_t parent_record;

    if (!mount || !path || !out_entry) {
        return -1;
    }
    if (str_eq(path, "/")) {
        out_entry->record_no = NTFS_ROOT_RECORD;
        out_entry->size = 0u;
        out_entry->is_dir = 1u;
        out_entry->name_namespace = 3u;
        out_entry->name[0] = '/';
        out_entry->name[1] = '\0';
        return 0;
    }

    if (path_split_last(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) {
        return -1;
    }
    if (ntfs_walk_dir(mount, parent, &parent_record) != 0) {
        return -1;
    }
    return ntfs_lookup(&mount->fs, parent_record, leaf, out_entry);
}

static int ntfs_mount_is_runtime_volatile(const vfs_ntfs_t* mount) {
    if (!mount || !mount->disk || !mount->disk->boot) {
        return 0;
    }
    return mount->disk->boot->boot_services_active == 0u;
}

static int ntfs_ops_list(void* ctx, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count) {
    vfs_ntfs_t* mount = (vfs_ntfs_t*)ctx;
    ntfs_dirent_t stack_entries[64];
    ntfs_dirent_t* entries = stack_entries;
    uint64_t dir_record;
    size_t list_cap = max_entries;
    uint32_t count = 0u;
    int list_rc;

    if (!mount || !out || !out_count) {
        return -1;
    }
    if (max_entries == 0u) {
        *out_count = 0u;
        return 0;
    }
    if (ntfs_walk_dir(mount, path, &dir_record) != 0) {
        return -1;
    }

    if (list_cap > 64u) {
        entries = (ntfs_dirent_t*)kmalloc(list_cap * sizeof(ntfs_dirent_t));
        if (!entries) {
            entries = stack_entries;
            list_cap = 64u;
        }
    }

    list_rc = ntfs_list_dir(&mount->fs, dir_record, entries, (uint32_t)list_cap, &count);
    if (list_rc != 0) {
        if (entries != stack_entries) {
            kfree(entries);
        }
        return -1;
    }
    if (count > max_entries) {
        count = (uint32_t)max_entries;
    }

    for (uint32_t i = 0u; i < count; i++) {
        str_copy(out[i].name, entries[i].name, sizeof(out[i].name));
        out[i].type = entries[i].is_dir ? MYAOS_NODE_DIR : MYAOS_NODE_FILE;
        out[i].size = (uint32_t)((entries[i].size > 0xFFFFFFFFull) ? 0xFFFFFFFFu : entries[i].size);
    }

    if (entries != stack_entries) {
        kfree(entries);
    }
    *out_count = count;
    return 0;
}

static int ntfs_ops_read_file(void* ctx, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    vfs_ntfs_t* mount = (vfs_ntfs_t*)ctx;
    ntfs_dirent_t entry;

    if (!mount || !out_size) {
        return -1;
    }
    if (ntfs_lookup_path(mount, path, &entry) != 0 || entry.is_dir) {
        return -1;
    }
    return ntfs_read_file(&mount->fs, entry.record_no, out_buf, out_buf_size, out_size);
}

static int ntfs_ops_write_file(void* ctx, const char* path, const uint8_t* data, uint32_t size) {
    vfs_ntfs_t* mount = (vfs_ntfs_t*)ctx;
    ntfs_dirent_t entry;

    if (!mount || (!data && size != 0u)) {
        return -1;
    }
    if (ntfs_lookup_path(mount, path, &entry) != 0 || entry.is_dir) {
        return -1;
    }
    if (ntfs_write_file(&mount->fs, entry.record_no, data, size) != 0) {
        return -1;
    }
    mount->dirty = 1u;
    return 0;
}

static int ntfs_ops_is_dir(void* ctx, const char* path) {
    vfs_ntfs_t* mount = (vfs_ntfs_t*)ctx;
    ntfs_dirent_t entry;

    if (!mount) {
        return -1;
    }
    if (str_eq(path, "/")) {
        return 1;
    }
    if (ntfs_lookup_path(mount, path, &entry) != 0) {
        return -1;
    }
    return entry.is_dir ? 1 : 0;
}

static int ntfs_ops_sync(void* ctx) {
    vfs_ntfs_t* mount = (vfs_ntfs_t*)ctx;

    if (!mount || !mount->dirty) {
        return 0;
    }
    if (mount->volatile_mode) {
        mount->dirty = 0u;
        return 0;
    }
    if (!mount->disk) {
        return -1;
    }
    if (blockio_writeback_disk(mount->disk) != 0) {
        return -1;
    }
    mount->dirty = 0u;
    return 0;
}

static const vfs_ops_t g_ntfs_ops = {
    .list = ntfs_ops_list,
    .read_file = ntfs_ops_read_file,
    .write_file = ntfs_ops_write_file,
    .mkdir = NULL,
    .touch = NULL,
    .remove = NULL,
    .is_dir = ntfs_ops_is_dir,
    .sync = ntfs_ops_sync,
};

int vfs_ntfs_mount_disk(vfs_ntfs_t* mount, const blockio_disk_t* disk) {
    if (!mount || !disk || !disk->image_base || disk->image_size < 512u) {
        return -1;
    }

    mount->disk = disk;
    mount->dirty = 0u;
    mount->volatile_mode = (uint8_t)ntfs_mount_is_runtime_volatile(mount);
    return ntfs_mount(&mount->fs, disk->image_base, disk->image_size);
}

const vfs_ops_t* vfs_ntfs_ops(void) {
    return &g_ntfs_ops;
}
