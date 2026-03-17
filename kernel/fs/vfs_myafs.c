#include "vfs_myafs.h"
#include "blockio.h"
#include <myafs/ondisk.h>

#define MYAFS_V2_SB_BLOCK 8u
#define MYAFS_SCAN_LIMIT (32u * 1024u * 1024u)
#define MYAFS_SERIALIZED_LINE_MAX (MYAFS_FILE_DATA_MAX * 2u + MYAOS_PATH_MAX + 32u)

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

static size_t str_nlen(const char* s, size_t max_len) {
    size_t n = 0;
    while (s && n < max_len && s[n]) {
        n++;
    }
    return n;
}

static int mem_eq(const uint8_t* a, const uint8_t* b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int str_starts_with(const char* text, const char* prefix) {
    size_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return (int)(c - 'A' + 10);
    }
    return -1;
}

static char hex_digit(uint8_t value) {
    value &= 0x0fu;
    if (value < 10u) {
        return (char)('0' + value);
    }
    return (char)('a' + (value - 10u));
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

static int find_node(const vfs_myafs_t* fs, const char* path) {
    for (uint32_t i = 0; i < MYAFS_MAX_NODES; i++) {
        if (fs->nodes[i].used && str_eq(fs->nodes[i].path, path)) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_node(vfs_myafs_t* fs) {
    for (uint32_t i = 0; i < MYAFS_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int has_children(const vfs_myafs_t* fs, const char* dir_path) {
    for (uint32_t i = 0; i < MYAFS_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            continue;
        }
        if (path_is_child_of(fs->nodes[i].path, dir_path)) {
            return 1;
        }
    }
    return 0;
}

static void myafs_reset_nodes(vfs_myafs_t* fs) {
    for (uint32_t i = 0; i < MYAFS_MAX_NODES; i++) {
        fs->nodes[i].used = 0u;
        fs->nodes[i].is_dir = 0u;
        fs->nodes[i].path[0] = '\0';
        fs->nodes[i].size = 0u;
    }

    fs->nodes[0].used = 1u;
    fs->nodes[0].is_dir = 1u;
    str_copy(fs->nodes[0].path, "/", sizeof(fs->nodes[0].path));
}

static int myafs_decode_hex_payload(const char* hex, uint8_t* out, uint32_t out_max, uint32_t* out_size) {
    size_t len;
    uint32_t pos = 0;

    if (!hex || !out || !out_size) {
        return -1;
    }

    len = str_len(hex);
    if ((len & 1u) != 0u || (len / 2u) > out_max) {
        return -1;
    }

    for (size_t i = 0; i < len; i += 2u) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[pos++] = (uint8_t)((hi << 4) | lo);
    }

    *out_size = pos;
    return 0;
}

static int myafs_parse_payload(vfs_myafs_t* fs, const uint8_t* payload, size_t payload_size) {
    char line[MYAFS_SERIALIZED_LINE_MAX];
    size_t size;
    size_t pos = 0;
    uint8_t saw_magic = 0u;

    if (!fs || !payload || payload_size == 0u) {
        return -1;
    }

    myafs_reset_nodes(fs);
    size = str_nlen((const char*)payload, payload_size);

    while (pos < size) {
        size_t line_len = 0;
        size_t line_start = pos;

        while (pos < size && payload[pos] != '\n') {
            pos++;
            line_len++;
            if (line_len + 1u >= sizeof(line)) {
                return -1;
            }
        }
        if (pos < size && payload[pos] == '\n') {
            pos++;
        }

        for (size_t i = 0; i < line_len; i++) {
            line[i] = (char)payload[line_start + i];
        }
        line[line_len] = '\0';
        if (line_len > 0u && line[line_len - 1u] == '\r') {
            line[line_len - 1u] = '\0';
            line_len--;
        }
        if (line_len == 0u) {
            continue;
        }

        if (!saw_magic) {
            if (!str_eq(line, "MYAFS1")) {
                return -1;
            }
            saw_magic = 1u;
            continue;
        }

        if (str_starts_with(line, "LABEL|")) {
            str_copy(fs->label, line + 6u, sizeof(fs->label));
            continue;
        }

        if (str_starts_with(line, "NODE|")) {
            char* p = line + 5u;
            char* type = p;
            char* path;
            char* payload_hex;
            char norm[MYAOS_PATH_MAX];
            int idx;

            while (*p && *p != '|') {
                p++;
            }
            if (*p != '|') {
                return -1;
            }
            *p++ = '\0';

            path = p;
            while (*p && *p != '|') {
                p++;
            }
            if (*p != '|') {
                return -1;
            }
            *p++ = '\0';
            payload_hex = p;

            if (normalize_path(path, norm, sizeof(norm)) != 0) {
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
                    return -1;
                }
            }

            fs->nodes[idx].used = 1u;
            str_copy(fs->nodes[idx].path, norm, sizeof(fs->nodes[idx].path));

            if (str_eq(type, "D")) {
                fs->nodes[idx].is_dir = 1u;
                fs->nodes[idx].size = 0u;
            } else if (str_eq(type, "F")) {
                uint32_t file_size = 0;
                fs->nodes[idx].is_dir = 0u;
                if (myafs_decode_hex_payload(payload_hex, fs->nodes[idx].data, MYAFS_FILE_DATA_MAX, &file_size) != 0) {
                    return -1;
                }
                fs->nodes[idx].size = file_size;
            } else {
                return -1;
            }
            continue;
        }

        return -1;
    }

    return saw_magic ? 0 : -1;
}

static int myafs_payload_append_char(uint8_t* out, uint32_t out_size, uint32_t* pos, char c) {
    if (!out || !pos || *pos + 1u >= out_size) {
        return -1;
    }
    out[*pos] = (uint8_t)c;
    (*pos)++;
    return 0;
}

static int myafs_payload_append_text(uint8_t* out, uint32_t out_size, uint32_t* pos, const char* text) {
    size_t i = 0;

    if (!out || !pos || !text) {
        return -1;
    }

    while (text[i]) {
        if (myafs_payload_append_char(out, out_size, pos, text[i]) != 0) {
            return -1;
        }
        i++;
    }
    return 0;
}

static int myafs_serialize_payload(const vfs_myafs_t* fs, uint8_t* out, uint32_t out_size, uint32_t* out_size_written) {
    uint32_t pos = 0;

    if (!fs || !out || out_size == 0u || !out_size_written) {
        return -1;
    }

    if (myafs_payload_append_text(out, out_size, &pos, "MYAFS1\n") != 0 ||
        myafs_payload_append_text(out, out_size, &pos, "LABEL|") != 0 ||
        myafs_payload_append_text(out, out_size, &pos, fs->label) != 0 ||
        myafs_payload_append_char(out, out_size, &pos, '\n') != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < MYAFS_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            continue;
        }

        if (myafs_payload_append_text(out, out_size, &pos, "NODE|") != 0 ||
            myafs_payload_append_char(out, out_size, &pos, fs->nodes[i].is_dir ? 'D' : 'F') != 0 ||
            myafs_payload_append_char(out, out_size, &pos, '|') != 0 ||
            myafs_payload_append_text(out, out_size, &pos, fs->nodes[i].path) != 0 ||
            myafs_payload_append_char(out, out_size, &pos, '|') != 0) {
            return -1;
        }

        if (!fs->nodes[i].is_dir) {
            for (uint32_t j = 0; j < fs->nodes[i].size; j++) {
                uint8_t b = fs->nodes[i].data[j];
                if (myafs_payload_append_char(out, out_size, &pos, hex_digit((uint8_t)(b >> 4u))) != 0 ||
                    myafs_payload_append_char(out, out_size, &pos, hex_digit(b)) != 0) {
                    return -1;
                }
            }
        }

        if (myafs_payload_append_char(out, out_size, &pos, '\n') != 0) {
            return -1;
        }
    }

    if (pos >= out_size) {
        return -1;
    }
    out[pos] = '\0';
    *out_size_written = pos;
    return 0;
}

int vfs_myafs_probe_disk(const blockio_disk_t* disk) {
    const uint8_t* image;
    uint64_t sb_offset;
    const myafs_superblock_v2_t* sb2;

    if (!disk || !disk->image_base || disk->image_size < 8u) {
        return 0;
    }

    image = disk->image_base;
    if (mem_eq(image, (const uint8_t*)"MYAFS1", 6u)) {
        return 1;
    }

    sb_offset = (uint64_t)MYAFS_V2_SB_BLOCK * (uint64_t)MYAFS_V2_DEFAULT_BLOCK_SIZE;
    if (disk->image_size < sb_offset + sizeof(*sb2)) {
        return 0;
    }

    sb2 = (const myafs_superblock_v2_t*)(const void*)(image + sb_offset);
    if (!mem_eq(sb2->magic, (const uint8_t*)MYAFS_SUPERBLOCK_MAGIC, 7u)) {
        return 0;
    }
    return sb2->version_major == MYAFS_ONDISK_VERSION_MAJOR;
}

int vfs_myafs_mount_disk(vfs_myafs_t* mount, const blockio_disk_t* disk) {
    const uint8_t* image;
    uint64_t sb_offset;
    const myafs_superblock_v2_t* sb2;

    if (!mount || !disk || !disk->image_base || disk->image_size < 8u) {
        return -1;
    }

    vfs_myafs_init(mount, "myafs");
    mount->disk = disk;

    image = disk->image_base;
    if (mem_eq(image, (const uint8_t*)"MYAFS1", 6u)) {
        size_t scan = (size_t)((disk->image_size < MYAFS_SCAN_LIMIT) ? disk->image_size : MYAFS_SCAN_LIMIT);
        if (myafs_parse_payload(mount, image, scan) != 0) {
            return -1;
        }
        mount->backing = MYAFS_BACKING_LEGACY;
        mount->payload_offset = 0u;
        mount->payload_bytes = (uint32_t)((disk->image_size > (uint64_t)0xFFFFFFFFu) ? 0xFFFFFFFFu : disk->image_size);
        return 0;
    }

    sb_offset = (uint64_t)MYAFS_V2_SB_BLOCK * (uint64_t)MYAFS_V2_DEFAULT_BLOCK_SIZE;
    if (disk->image_size < sb_offset + sizeof(*sb2)) {
        return -1;
    }

    sb2 = (const myafs_superblock_v2_t*)(const void*)(image + sb_offset);
    if (!mem_eq(sb2->magic, (const uint8_t*)MYAFS_SUPERBLOCK_MAGIC, 7u) ||
        sb2->version_major != MYAFS_ONDISK_VERSION_MAJOR ||
        sb2->block_size == 0u) {
        return -1;
    }

    {
        uint64_t data_offset = sb2->data_start * (uint64_t)sb2->block_size;
        uint64_t recovery_offset = sb2->recovery_log_start * (uint64_t)sb2->block_size;
        uint64_t payload_bytes_u64;
        size_t scan;
        const uint8_t* payload;
        size_t label_len;

        if (recovery_offset <= data_offset || recovery_offset > disk->image_size) {
            return -1;
        }

        payload_bytes_u64 = recovery_offset - data_offset;
        mount->payload_offset = data_offset;
        mount->payload_bytes = (uint32_t)((payload_bytes_u64 > (uint64_t)0xFFFFFFFFu) ? 0xFFFFFFFFu : payload_bytes_u64);
        mount->backing = MYAFS_BACKING_V2;

        label_len = str_nlen(sb2->label, sizeof(sb2->label));
        if (label_len > 0u) {
            for (size_t i = 0; i < label_len && i + 1u < sizeof(mount->label); i++) {
                mount->label[i] = sb2->label[i];
                mount->label[i + 1u] = '\0';
            }
        }

        if (payload_bytes_u64 == 0u) {
            return 0;
        }

        payload = image + data_offset;
        scan = (size_t)((payload_bytes_u64 < MYAFS_SCAN_LIMIT) ? payload_bytes_u64 : MYAFS_SCAN_LIMIT);
        if (scan >= 6u && mem_eq(payload, (const uint8_t*)"MYAFS1", 6u)) {
            if (myafs_parse_payload(mount, payload, scan) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int myafs_ops_list(void* ctx, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count) {
    vfs_myafs_t* fs = (vfs_myafs_t*)ctx;
    char norm[MYAOS_PATH_MAX];
    size_t count = 0;

    if (!fs || !out || !out_count || normalize_path(path, norm, sizeof(norm)) != 0) {
        return -1;
    }

    {
        int dir_idx = find_node(fs, norm);
        if (dir_idx < 0 || fs->nodes[dir_idx].is_dir == 0u) {
            return -1;
        }
    }

    for (uint32_t i = 0; i < MYAFS_MAX_NODES; i++) {
        char child_name[MYAOS_NAME_MAX];
        uint8_t is_direct = 0;
        uint8_t duplicate = 0;

        if (!fs->nodes[i].used || str_eq(fs->nodes[i].path, norm)) {
            continue;
        }
        if (extract_child_name(norm, fs->nodes[i].path, child_name, sizeof(child_name), &is_direct) != 0) {
            continue;
        }

        for (size_t j = 0; j < count; j++) {
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
            out[count].type = MYAOS_NODE_DIR;
            out[count].size = 0;
        } else {
            out[count].type = fs->nodes[i].is_dir ? MYAOS_NODE_DIR : MYAOS_NODE_FILE;
            out[count].size = fs->nodes[i].is_dir ? 0u : fs->nodes[i].size;
        }
        count++;
    }

    *out_count = count;
    return 0;
}

static int myafs_ops_read_file(void* ctx, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    vfs_myafs_t* fs = (vfs_myafs_t*)ctx;
    char norm[MYAOS_PATH_MAX];
    int idx;

    if (!fs || !out_buf || !out_size || normalize_path(path, norm, sizeof(norm)) != 0) {
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

static int myafs_ops_write_file(void* ctx, const char* path, const uint8_t* data, uint32_t size) {
    vfs_myafs_t* fs = (vfs_myafs_t*)ctx;
    char norm[MYAOS_PATH_MAX];
    char parent[MYAOS_PATH_MAX];
    int parent_idx;
    int idx;

    if (!fs || (size > 0 && !data) || size > MYAFS_FILE_DATA_MAX || normalize_path(path, norm, sizeof(norm)) != 0) {
        return -1;
    }
    if (str_eq(norm, "/") || get_parent_path(norm, parent, sizeof(parent)) != 0) {
        return -1;
    }

    parent_idx = find_node(fs, parent);
    if (parent_idx < 0 || fs->nodes[parent_idx].is_dir == 0u) {
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
    fs->dirty = 1u;
    return 0;
}

static int myafs_ops_mkdir(void* ctx, const char* path) {
    vfs_myafs_t* fs = (vfs_myafs_t*)ctx;
    char norm[MYAOS_PATH_MAX];
    char parent[MYAOS_PATH_MAX];
    int parent_idx;
    int idx;

    if (!fs || normalize_path(path, norm, sizeof(norm)) != 0 || str_eq(norm, "/")) {
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
    if (parent_idx < 0 || fs->nodes[parent_idx].is_dir == 0u) {
        return -1;
    }

    idx = alloc_node(fs);
    if (idx < 0) {
        return -1;
    }

    fs->nodes[idx].used = 1u;
    fs->nodes[idx].is_dir = 1u;
    fs->nodes[idx].size = 0;
    str_copy(fs->nodes[idx].path, norm, sizeof(fs->nodes[idx].path));
    fs->dirty = 1u;
    return 0;
}

static int myafs_ops_touch(void* ctx, const char* path) {
    static const uint8_t empty = 0;
    return myafs_ops_write_file(ctx, path, &empty, 0);
}

static int myafs_ops_remove(void* ctx, const char* path) {
    vfs_myafs_t* fs = (vfs_myafs_t*)ctx;
    char norm[MYAOS_PATH_MAX];
    int idx;

    if (!fs || normalize_path(path, norm, sizeof(norm)) != 0 || str_eq(norm, "/")) {
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
    fs->dirty = 1u;
    return 0;
}

static int myafs_ops_is_dir(void* ctx, const char* path) {
    vfs_myafs_t* fs = (vfs_myafs_t*)ctx;
    char norm[MYAOS_PATH_MAX];
    int idx;

    if (!fs || normalize_path(path, norm, sizeof(norm)) != 0) {
        return 0;
    }

    idx = find_node(fs, norm);
    if (idx < 0) {
        return 0;
    }
    return fs->nodes[idx].is_dir ? 1 : 0;
}

static int myafs_ops_sync(void* ctx) {
    vfs_myafs_t* fs = (vfs_myafs_t*)ctx;
    uint8_t* payload_base;
    uint32_t payload_size;
    uint32_t written = 0;

    if (!fs) {
        return -1;
    }
    if (!fs->dirty || fs->backing == MYAFS_BACKING_MEM) {
        return 0;
    }
    if (!fs->disk || !fs->disk->image_base || fs->disk->read_only) {
        return -1;
    }

    payload_base = fs->disk->image_base + fs->payload_offset;
    payload_size = fs->payload_bytes;
    if (payload_size == 0u || fs->payload_offset >= fs->disk->image_size) {
        return -1;
    }
    if (fs->payload_offset + (uint64_t)payload_size > fs->disk->image_size) {
        payload_size = (uint32_t)(fs->disk->image_size - fs->payload_offset);
    }

    if (myafs_serialize_payload(fs, payload_base, payload_size, &written) != 0) {
        return -1;
    }
    if (blockio_writeback_disk(fs->disk) != 0) {
        return -1;
    }

    fs->dirty = 0u;
    return 0;
}

static const vfs_ops_t g_myafs_ops = {
    .list = myafs_ops_list,
    .read_file = myafs_ops_read_file,
    .write_file = myafs_ops_write_file,
    .mkdir = myafs_ops_mkdir,
    .touch = myafs_ops_touch,
    .remove = myafs_ops_remove,
    .is_dir = myafs_ops_is_dir,
    .sync = myafs_ops_sync,
};

void vfs_myafs_init(vfs_myafs_t* mount, const char* label) {
    if (!mount) {
        return;
    }

    str_copy(mount->label, label ? label : "myafs", sizeof(mount->label));
    mount->disk = NULL;
    mount->payload_offset = 0u;
    mount->payload_bytes = 0u;
    mount->backing = MYAFS_BACKING_MEM;
    mount->dirty = 0u;
    myafs_reset_nodes(mount);
}

const vfs_ops_t* vfs_myafs_ops(void) {
    return &g_myafs_ops;
}
