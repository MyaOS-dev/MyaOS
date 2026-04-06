#include "myafs_driver.h"

#include <stdio.h>
#include <string.h>

#define MYAFS_DRIVER_MAGIC "MYAFS1"
#define MYAFS_DRIVER_LINE_MAX (MYAFS_DRIVER_FILE_MAX * 2u + MYAFS_DRIVER_PATH_MAX + 64u)

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

static int str_starts_with(const char* text, const char* prefix) {
    size_t i = 0;

    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int str_eq_ci(const char* a, const char* b) {
    size_t i = 0;

    while (a[i] && b[i]) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static void trim_line_end(char* text) {
    size_t len = str_len(text);

    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        len--;
    }
    text[len] = '\0';
}

static int path_is_valid_char(char c) {
    return c >= 32 && c <= 126;
}

static int normalize_path(const char* in, char* out, size_t out_size) {
    size_t in_i = 0;
    size_t out_i = 0;

    if (!in || !out || out_size < 2 || in[0] != '/') {
        return -1;
    }

    out[out_i++] = '/';
    while (in[in_i] == '/') {
        in_i++;
    }

    while (in[in_i]) {
        size_t seg_begin = in_i;
        size_t seg_len;

        while (in[in_i] && in[in_i] != '/') {
            if (!path_is_valid_char(in[in_i])) {
                return -1;
            }
            in_i++;
        }
        seg_len = in_i - seg_begin;
        if (seg_len == 0) {
            while (in[in_i] == '/') {
                in_i++;
            }
            continue;
        }

        if (seg_len == 1 && in[seg_begin] == '.') {
            while (in[in_i] == '/') {
                in_i++;
            }
            continue;
        }
        if (seg_len == 2 && in[seg_begin] == '.' && in[seg_begin + 1] == '.') {
            return -1;
        }

        if (out_i > 1) {
            if (out_i + 1 >= out_size) {
                return -1;
            }
            out[out_i++] = '/';
        }

        if (out_i + seg_len >= out_size) {
            return -1;
        }
        for (size_t j = 0; j < seg_len; j++) {
            out[out_i++] = in[seg_begin + j];
        }

        while (in[in_i] == '/') {
            in_i++;
        }
    }

    out[out_i] = '\0';
    return 0;
}

static int path_is_child_of(const char* child, const char* parent) {
    size_t p_len = str_len(parent);

    if (str_eq(parent, "/")) {
        return child[0] == '/' && child[1] != '\0';
    }
    if (str_eq(child, parent)) {
        return 0;
    }
    if (child[0] == '\0' || parent[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; i < p_len; i++) {
        if (child[i] != parent[i]) {
            return 0;
        }
    }
    return child[p_len] == '/';
}

static int get_parent_path(const char* path, char* out_parent, size_t out_size) {
    size_t len;
    size_t cut;

    if (!path || path[0] != '/' || !out_parent || out_size < 2 || str_eq(path, "/")) {
        return -1;
    }

    len = str_len(path);
    cut = len;
    while (cut > 0 && path[cut - 1] != '/') {
        cut--;
    }

    if (cut == 0) {
        return -1;
    }
    if (cut == 1) {
        str_copy(out_parent, "/", out_size);
        return 0;
    }

    if (cut >= out_size) {
        return -1;
    }
    for (size_t i = 0; i < cut - 1; i++) {
        out_parent[i] = path[i];
    }
    out_parent[cut - 1] = '\0';
    return 0;
}

static int extract_child_name(const char* dir, const char* full, char* out_name, size_t out_size, uint8_t* out_is_direct) {
    const char* rest;
    size_t i = 0;
    uint8_t has_more = 0;

    if (!dir || !full || !out_name || out_size == 0 || !out_is_direct) {
        return -1;
    }

    if (str_eq(dir, "/")) {
        if (full[0] != '/' || full[1] == '\0') {
            return -1;
        }
        rest = full + 1;
    } else {
        size_t d_len = str_len(dir);
        if (!path_is_child_of(full, dir)) {
            return -1;
        }
        rest = full + d_len + 1;
    }

    while (rest[i] && rest[i] != '/') {
        if (i + 1 >= out_size) {
            return -1;
        }
        out_name[i] = rest[i];
        i++;
    }
    out_name[i] = '\0';
    if (i == 0) {
        return -1;
    }

    has_more = (rest[i] == '/');
    *out_is_direct = (uint8_t)(has_more ? 0u : 1u);
    return 0;
}

static int find_node(const myafs_driver_t* fs, const char* path) {
    for (uint32_t i = 0; i < MYAFS_DRIVER_MAX_NODES; i++) {
        if (fs->nodes[i].used && str_eq(fs->nodes[i].path, path)) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_node(myafs_driver_t* fs) {
    for (uint32_t i = 0; i < MYAFS_DRIVER_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int has_children(const myafs_driver_t* fs, const char* dir_path) {
    for (uint32_t i = 0; i < MYAFS_DRIVER_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            continue;
        }
        if (path_is_child_of(fs->nodes[i].path, dir_path)) {
            return 1;
        }
    }
    return 0;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int hex_decode(const char* hex, uint8_t* out, uint32_t out_max, uint32_t* out_size) {
    size_t len = str_len(hex);
    uint32_t pos = 0;

    if ((len % 2u) != 0u) {
        return -1;
    }

    for (size_t i = 0; i < len; i += 2u) {
        int hi = hex_value(hex[i]);
        int lo = hex_value(hex[i + 1u]);
        if (hi < 0 || lo < 0 || pos >= out_max) {
            return -1;
        }
        out[pos++] = (uint8_t)((hi << 4) | lo);
    }

    *out_size = pos;
    return 0;
}

int myafs_driver_init(myafs_driver_t* fs, const char* label) {
    if (!fs) {
        return -1;
    }

    str_copy(fs->label, label ? label : "myafs", sizeof(fs->label));
    for (uint32_t i = 0; i < MYAFS_DRIVER_MAX_NODES; i++) {
        fs->nodes[i].used = 0u;
        fs->nodes[i].is_dir = 0u;
        fs->nodes[i].path[0] = '\0';
        fs->nodes[i].size = 0u;
    }

    fs->nodes[0].used = 1u;
    fs->nodes[0].is_dir = 1u;
    str_copy(fs->nodes[0].path, "/", sizeof(fs->nodes[0].path));
    return 0;
}

int myafs_driver_load(myafs_driver_t* fs, const char* image_path) {
    FILE* fp;
    char line[MYAFS_DRIVER_LINE_MAX];

    if (!fs || !image_path) {
        return -1;
    }

    fp = fopen(image_path, "rb");
    if (!fp) {
        return -1;
    }

    if (!fgets(line, (int)sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    trim_line_end(line);
    if (!str_eq(line, MYAFS_DRIVER_MAGIC)) {
        fclose(fp);
        return -1;
    }

    if (myafs_driver_init(fs, "myafs") != 0) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, (int)sizeof(line), fp)) {
        char* p;
        char* type;
        char* path;
        char* payload;
        char norm[MYAFS_DRIVER_PATH_MAX];
        int idx;

        trim_line_end(line);
        if (!line[0]) {
            continue;
        }

        if (str_starts_with(line, "LABEL|")) {
            str_copy(fs->label, line + 6, sizeof(fs->label));
            continue;
        }
        if (!str_starts_with(line, "NODE|")) {
            fclose(fp);
            return -1;
        }

        p = line + 5;
        type = p;
        p = strchr(p, '|');
        if (!p) {
            fclose(fp);
            return -1;
        }
        *p++ = '\0';

        path = p;
        p = strchr(p, '|');
        if (!p) {
            fclose(fp);
            return -1;
        }
        *p++ = '\0';
        payload = p;

        if (normalize_path(path, norm, sizeof(norm)) != 0) {
            fclose(fp);
            return -1;
        }

        idx = find_node(fs, norm);
        if (idx < 0) {
            if (str_eq(norm, "/")) {
                idx = 0;
            } else {
                idx = alloc_node(fs);
            }
            if (idx < 0) {
                fclose(fp);
                return -1;
            }
        }

        fs->nodes[idx].used = 1u;
        str_copy(fs->nodes[idx].path, norm, sizeof(fs->nodes[idx].path));

        if (type[0] == 'D' && type[1] == '\0') {
            fs->nodes[idx].is_dir = 1u;
            fs->nodes[idx].size = 0u;
        } else if (type[0] == 'F' && type[1] == '\0') {
            uint32_t file_size = 0;
            fs->nodes[idx].is_dir = 0u;
            if (hex_decode(payload, fs->nodes[idx].data, MYAFS_DRIVER_FILE_MAX, &file_size) != 0) {
                fclose(fp);
                return -1;
            }
            fs->nodes[idx].size = file_size;
        } else {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int myafs_driver_save(const myafs_driver_t* fs, const char* image_path) {
    FILE* fp;

    if (!fs || !image_path) {
        return -1;
    }

    fp = fopen(image_path, "wb");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "%s\n", MYAFS_DRIVER_MAGIC);
    fprintf(fp, "LABEL|%s\n", fs->label);

    for (uint32_t i = 0; i < MYAFS_DRIVER_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            continue;
        }

        if (fs->nodes[i].is_dir) {
            fprintf(fp, "NODE|D|%s|\n", fs->nodes[i].path);
        } else {
            fprintf(fp, "NODE|F|%s|", fs->nodes[i].path);
            for (uint32_t j = 0; j < fs->nodes[i].size; j++) {
                fprintf(fp, "%02x", fs->nodes[i].data[j]);
            }
            fputc('\n', fp);
        }
    }

    fclose(fp);
    return 0;
}

int myafs_driver_list(
    const myafs_driver_t* fs,
    const char* path,
    myafs_driver_dirent_t* out,
    uint32_t max_entries,
    uint32_t* out_count
) {
    char norm[MYAFS_DRIVER_PATH_MAX];
    uint32_t count = 0;
    int dir_idx;

    if (!fs || !path || !out || !out_count || normalize_path(path, norm, sizeof(norm)) != 0) {
        return -1;
    }

    dir_idx = find_node(fs, norm);
    if (dir_idx < 0 || !fs->nodes[dir_idx].is_dir) {
        return -1;
    }

    for (uint32_t i = 0; i < MYAFS_DRIVER_MAX_NODES; i++) {
        char child_name[MYAFS_DRIVER_PATH_MAX];
        uint8_t is_direct = 0;
        uint8_t duplicate = 0;

        if (!fs->nodes[i].used || str_eq(fs->nodes[i].path, norm)) {
            continue;
        }
        if (extract_child_name(norm, fs->nodes[i].path, child_name, sizeof(child_name), &is_direct) != 0) {
            continue;
        }

        for (uint32_t j = 0; j < count; j++) {
            if (str_eq_ci(out[j].name, child_name)) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate || count >= max_entries) {
            continue;
        }

        str_copy(out[count].name, child_name, sizeof(out[count].name));
        if (!is_direct) {
            out[count].is_dir = 1u;
            out[count].size = 0u;
        } else {
            out[count].is_dir = fs->nodes[i].is_dir;
            out[count].size = fs->nodes[i].is_dir ? 0u : fs->nodes[i].size;
        }
        count++;
    }

    *out_count = count;
    return 0;
}

int myafs_driver_read(
    const myafs_driver_t* fs,
    const char* path,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
) {
    char norm[MYAFS_DRIVER_PATH_MAX];
    int idx;

    if (!fs || !path || !out_buf || !out_size || normalize_path(path, norm, sizeof(norm)) != 0) {
        return -1;
    }

    idx = find_node(fs, norm);
    if (idx < 0 || fs->nodes[idx].is_dir || fs->nodes[idx].size > out_buf_size) {
        return -1;
    }

    for (uint32_t i = 0; i < fs->nodes[idx].size; i++) {
        out_buf[i] = fs->nodes[idx].data[i];
    }
    *out_size = fs->nodes[idx].size;
    return 0;
}

int myafs_driver_write(myafs_driver_t* fs, const char* path, const uint8_t* data, uint32_t size) {
    char norm[MYAFS_DRIVER_PATH_MAX];
    char parent[MYAFS_DRIVER_PATH_MAX];
    int parent_idx;
    int idx;

    if (!fs || !path || (size > 0 && !data) || size > MYAFS_DRIVER_FILE_MAX || normalize_path(path, norm, sizeof(norm)) != 0) {
        return -1;
    }
    if (str_eq(norm, "/") || get_parent_path(norm, parent, sizeof(parent)) != 0) {
        return -1;
    }

    parent_idx = find_node(fs, parent);
    if (parent_idx < 0 || !fs->nodes[parent_idx].is_dir) {
        return -1;
    }

    idx = find_node(fs, norm);
    if (idx < 0) {
        idx = alloc_node(fs);
        if (idx < 0) {
            return -1;
        }
        fs->nodes[idx].used = 1u;
        fs->nodes[idx].is_dir = 0u;
        str_copy(fs->nodes[idx].path, norm, sizeof(fs->nodes[idx].path));
    } else if (fs->nodes[idx].is_dir) {
        return -1;
    }

    fs->nodes[idx].size = size;
    for (uint32_t i = 0; i < size; i++) {
        fs->nodes[idx].data[i] = data[i];
    }
    return 0;
}

int myafs_driver_mkdir(myafs_driver_t* fs, const char* path) {
    char norm[MYAFS_DRIVER_PATH_MAX];
    char parent[MYAFS_DRIVER_PATH_MAX];
    int parent_idx;
    int idx;

    if (!fs || !path || normalize_path(path, norm, sizeof(norm)) != 0 || str_eq(norm, "/")) {
        return -1;
    }

    idx = find_node(fs, norm);
    if (idx >= 0) {
        return fs->nodes[idx].is_dir ? 0 : -1;
    }

    if (get_parent_path(norm, parent, sizeof(parent)) != 0) {
        return -1;
    }
    parent_idx = find_node(fs, parent);
    if (parent_idx < 0 || !fs->nodes[parent_idx].is_dir) {
        return -1;
    }

    idx = alloc_node(fs);
    if (idx < 0) {
        return -1;
    }

    fs->nodes[idx].used = 1u;
    fs->nodes[idx].is_dir = 1u;
    fs->nodes[idx].size = 0u;
    str_copy(fs->nodes[idx].path, norm, sizeof(fs->nodes[idx].path));
    return 0;
}

int myafs_driver_touch(myafs_driver_t* fs, const char* path) {
    static const uint8_t empty = 0;
    return myafs_driver_write(fs, path, &empty, 0u);
}

int myafs_driver_remove(myafs_driver_t* fs, const char* path) {
    char norm[MYAFS_DRIVER_PATH_MAX];
    int idx;

    if (!fs || !path || normalize_path(path, norm, sizeof(norm)) != 0 || str_eq(norm, "/")) {
        return -1;
    }

    idx = find_node(fs, norm);
    if (idx < 0) {
        return -1;
    }

    if (fs->nodes[idx].is_dir && has_children(fs, norm)) {
        return -1;
    }

    fs->nodes[idx].used = 0u;
    fs->nodes[idx].is_dir = 0u;
    fs->nodes[idx].path[0] = '\0';
    fs->nodes[idx].size = 0u;
    return 0;
}
