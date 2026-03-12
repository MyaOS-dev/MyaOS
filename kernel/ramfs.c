#include "ramfs.h"
#include <stddef.h>

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void str_copy(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0) {
        return;
    }

    size_t i = 0;
    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int str_eq_ci(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (to_lower_ascii(a[i]) != to_lower_ascii(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int name_is_valid(const char* name) {
    if (name[0] == '\0') {
        return 0;
    }

    size_t n = str_len(name);
    if (n >= RAMFS_NAME_MAX) {
        return 0;
    }

    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (c <= 32 || c > 126) {
            return 0;
        }
    }

    return 1;
}

void ramfs_init(ramfs_t* fs) {
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        fs->files[i].used = 0;
        fs->files[i].name[0] = '\0';
        fs->files[i].size = 0;
        fs->files[i].data[0] = '\0';
    }
}

int ramfs_write(ramfs_t* fs, const char* name, const char* data) {
    if (!name_is_valid(name)) {
        return -1;
    }

    size_t data_len = str_len(data);
    if (data_len >= RAMFS_DATA_MAX) {
        return -2;
    }

    int free_slot = -1;

    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        ramfs_file_t* f = &fs->files[i];
        if (!f->used) {
            if (free_slot < 0) {
                free_slot = (int)i;
            }
            continue;
        }

        if (str_eq_ci(f->name, name)) {
            str_copy(f->data, data, sizeof(f->data));
            f->size = (uint32_t)data_len;
            return 0;
        }
    }

    if (free_slot < 0) {
        return -3;
    }

    ramfs_file_t* f = &fs->files[(uint32_t)free_slot];
    f->used = 1;
    str_copy(f->name, name, sizeof(f->name));
    str_copy(f->data, data, sizeof(f->data));
    f->size = (uint32_t)data_len;
    return 0;
}

int ramfs_read(const ramfs_t* fs, const char* name, const char** out_data, uint32_t* out_size) {
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        const ramfs_file_t* f = &fs->files[i];
        if (!f->used) {
            continue;
        }

        if (str_eq_ci(f->name, name)) {
            *out_data = f->data;
            *out_size = f->size;
            return 0;
        }
    }

    return -1;
}

int ramfs_remove(ramfs_t* fs, const char* name) {
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        ramfs_file_t* f = &fs->files[i];
        if (!f->used) {
            continue;
        }

        if (str_eq_ci(f->name, name)) {
            f->used = 0;
            f->name[0] = '\0';
            f->size = 0;
            f->data[0] = '\0';
            return 0;
        }
    }

    return -1;
}

void ramfs_clear(ramfs_t* fs) {
    ramfs_init(fs);
}

uint32_t ramfs_count(const ramfs_t* fs) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (fs->files[i].used) {
            count++;
        }
    }
    return count;
}

const ramfs_file_t* ramfs_file_at(const ramfs_t* fs, uint32_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!fs->files[i].used) {
            continue;
        }

        if (seen == index) {
            return &fs->files[i];
        }
        seen++;
    }

    return NULL;
}
