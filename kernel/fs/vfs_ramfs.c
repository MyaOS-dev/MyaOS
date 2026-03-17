#include "vfs_ramfs.h"

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

static const char* basename_from_path(const char* path) {
    const char* last = path;

    for (size_t i = 0; path && path[i]; i++) {
        if (path[i] == '/') {
            last = &path[i + 1];
        }
    }
    return last;
}

static int ramfs_ops_list(void* ctx, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count) {
    vfs_ramfs_t* mount = (vfs_ramfs_t*)ctx;
    uint32_t count;

    if (!str_eq(path, "/")) {
        return -1;
    }

    count = ramfs_count(&mount->fs);
    if (count > max_entries) {
        count = (uint32_t)max_entries;
    }

    for (uint32_t i = 0; i < count; i++) {
        const ramfs_file_t* file = ramfs_file_at(&mount->fs, i);
        if (!file) {
            continue;
        }
        str_copy(out[i].name, file->name, sizeof(out[i].name));
        out[i].type = MYAOS_NODE_FILE;
        out[i].size = file->size;
    }

    *out_count = count;
    return 0;
}

static int ramfs_ops_read_file(void* ctx, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    vfs_ramfs_t* mount = (vfs_ramfs_t*)ctx;
    const char* data = NULL;
    uint32_t size = 0;
    const char* name;

    if (str_eq(path, "/")) {
        return -1;
    }

    name = basename_from_path(path);
    if (ramfs_read(&mount->fs, name, &data, &size) != 0 || size > out_buf_size) {
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        out_buf[i] = (uint8_t)data[i];
    }
    *out_size = size;
    return 0;
}

static int ramfs_ops_write_file(void* ctx, const char* path, const uint8_t* data, uint32_t size) {
    vfs_ramfs_t* mount = (vfs_ramfs_t*)ctx;
    static char tmp[RAMFS_DATA_MAX];
    const char* name;

    if (size >= RAMFS_DATA_MAX || str_eq(path, "/")) {
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        tmp[i] = (char)data[i];
    }
    tmp[size] = '\0';

    name = basename_from_path(path);
    return ramfs_write(&mount->fs, name, tmp);
}

static int ramfs_ops_mkdir(void* ctx, const char* path) {
    (void)ctx;
    (void)path;
    return -1;
}

static int ramfs_ops_touch(void* ctx, const char* path) {
    static const uint8_t empty = 0;
    return ramfs_ops_write_file(ctx, path, &empty, 0);
}

static int ramfs_ops_remove(void* ctx, const char* path) {
    vfs_ramfs_t* mount = (vfs_ramfs_t*)ctx;
    const char* name;

    if (str_eq(path, "/")) {
        return -1;
    }

    name = basename_from_path(path);
    return ramfs_remove(&mount->fs, name);
}

static int ramfs_ops_is_dir(void* ctx, const char* path) {
    (void)ctx;
    return str_eq(path, "/") ? 1 : 0;
}

static int ramfs_ops_sync(void* ctx) {
    (void)ctx;
    return 0;
}

static const vfs_ops_t g_ramfs_ops = {
    .list = ramfs_ops_list,
    .read_file = ramfs_ops_read_file,
    .write_file = ramfs_ops_write_file,
    .mkdir = ramfs_ops_mkdir,
    .touch = ramfs_ops_touch,
    .remove = ramfs_ops_remove,
    .is_dir = ramfs_ops_is_dir,
    .sync = ramfs_ops_sync,
};

void vfs_ramfs_init(vfs_ramfs_t* mount) {
    if (!mount) {
        return;
    }
    ramfs_init(&mount->fs);
}

const vfs_ops_t* vfs_ramfs_ops(void) {
    return &g_ramfs_ops;
}
