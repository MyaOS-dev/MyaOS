#include "../lib/myaos.h"
#include <stdint.h>

#define PKG_FILE_MAX (512u * 1024u)
#define PKG_TEXT_MAX 16384u
#define PKG_META_MAX 4096u
#define PKG_VERSION_MAX 24u
#define PKG_REPO_MAX 64u
#define PKG_INSTALLED_MAX 64u
#define PKG_FILES_META_MAX 2048u

#define PKG_INDEX_DEFAULT "/repo/index.pkg"
#define PKG_DB_DIR "/var/pkg"
#define PKG_DB_FILE "/var/pkg/installed.db"

typedef struct {
    char name[MYAOS_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char path[MYAOS_PATH_MAX];
    uint32_t abi;
} pkg_repo_entry_t;

typedef struct {
    char name[MYAOS_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char source[MYAOS_PATH_MAX];
} pkg_installed_entry_t;

static uint8_t g_pkg_file_buf[PKG_FILE_MAX];
static uint8_t g_text_buf[PKG_TEXT_MAX];

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
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

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0;

    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1u < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int parse_u32(const char* text, uint32_t* out) {
    uint64_t value = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }

    for (uint32_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
        if (value > 0xFFFFFFFFull) {
            return -1;
        }
    }

    *out = (uint32_t)value;
    return 0;
}

static int parse_version(const char* text, uint32_t out_parts[3]) {
    uint32_t index = 0;
    uint32_t value = 0;
    uint8_t saw_digit = 0;

    if (!text || !text[0]) {
        return -1;
    }

    out_parts[0] = 0;
    out_parts[1] = 0;
    out_parts[2] = 0;

    for (uint32_t i = 0;; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            saw_digit = 1u;
            value = value * 10u + (uint32_t)(c - '0');
            if (value > 1000000u) {
                return -1;
            }
            continue;
        }

        if (c == '.' || c == '\0') {
            if (!saw_digit || index > 2u) {
                return -1;
            }
            out_parts[index++] = value;
            value = 0;
            saw_digit = 0u;
            if (c == '\0') {
                break;
            }
            continue;
        }

        return -1;
    }

    return 0;
}

static int compare_version(const char* a, const char* b) {
    uint32_t va[3];
    uint32_t vb[3];

    if (parse_version(a, va) != 0 || parse_version(b, vb) != 0) {
        return 0;
    }

    for (uint32_t i = 0; i < 3u; i++) {
        if (va[i] < vb[i]) {
            return -1;
        }
        if (va[i] > vb[i]) {
            return 1;
        }
    }
    return 0;
}

static char* next_line(char** cursor) {
    char* line;
    char* p;

    if (!cursor || !*cursor || !(*cursor)[0]) {
        return NULL;
    }

    line = *cursor;
    p = line;

    while (*p && *p != '\n' && *p != '\r') {
        p++;
    }
    while (*p == '\n' || *p == '\r') {
        *p = '\0';
        p++;
    }

    *cursor = p;
    return line;
}

static int append_text(char* out, uint32_t out_size, uint32_t* io_pos, const char* text) {
    uint32_t pos;

    if (!out || !io_pos || !text || out_size == 0u) {
        return -1;
    }

    pos = *io_pos;
    for (uint32_t i = 0; text[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = text[i];
    }

    out[pos] = '\0';
    *io_pos = pos;
    return 0;
}

static int is_valid_package_name(const char* name) {
    uint32_t n = 0;

    if (!name || !name[0]) {
        return 0;
    }

    for (n = 0; name[n]; n++) {
        char c = name[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return 0;
        }
    }

    return n < MYAOS_NAME_MAX;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (int)(c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (int)(c - 'A');
    }
    return -1;
}

static int decode_hex(const char* hex, uint8_t* out, uint32_t out_size, uint32_t* out_len) {
    uint32_t hex_len = (uint32_t)str_len(hex);
    uint32_t out_len_local = hex_len / 2u;

    if ((hex_len & 1u) != 0u || out_len_local > out_size) {
        return -1;
    }

    for (uint32_t i = 0; i < out_len_local; i++) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((uint8_t)(hi << 4) | (uint8_t)lo);
    }

    if (out_len) {
        *out_len = out_len_local;
    }
    return 0;
}

static void ensure_dir_tree(const char* path) {
    char temp[MYAOS_PATH_MAX];

    if (!path || path[0] != '/') {
        return;
    }

    str_copy(temp, path, sizeof(temp));
    for (uint32_t i = 1; temp[i]; i++) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            if (temp[1] != '\0') {
                (void)mya_fs_mkdir(temp);
            }
            temp[i] = '/';
        }
    }

    (void)mya_fs_mkdir(temp);
}

static void ensure_parent_dirs(const char* file_path) {
    char temp[MYAOS_PATH_MAX];

    if (!file_path || file_path[0] != '/') {
        return;
    }

    str_copy(temp, file_path, sizeof(temp));
    for (uint32_t i = 1; temp[i]; i++) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            if (temp[1] != '\0') {
                (void)mya_fs_mkdir(temp);
            }
            temp[i] = '/';
        }
    }
}

static int read_text_file(const char* path, uint8_t* buf, uint32_t cap, uint32_t* out_size) {
    uint32_t size = 0;

    if (!path || !buf || cap < 2u) {
        return -1;
    }
    if (mya_fs_read(path, buf, cap - 1u, &size) != 0) {
        return -1;
    }
    if (size + 1u >= cap) {
        return -1;
    }
    buf[size] = '\0';

    if (out_size) {
        *out_size = size;
    }
    return 0;
}

static int build_meta_path(const char* name, char* out, uint32_t out_size) {
    uint32_t pos = 0;

    if (!is_valid_package_name(name) || !out || out_size == 0u) {
        return -1;
    }

    if (append_text(out, out_size, &pos, PKG_DB_DIR "/") != 0) {
        return -1;
    }
    if (append_text(out, out_size, &pos, name) != 0) {
        return -1;
    }
    if (append_text(out, out_size, &pos, ".meta") != 0) {
        return -1;
    }

    return 0;
}

static int parse_repo_line(char* line, pkg_repo_entry_t* out) {
    char* p;
    char* name;
    char* version;
    char* path;
    char* abi_text;

    if (!line || !out || !line[0] || line[0] == '#') {
        return -1;
    }

    p = line;
    name = p;
    while (*p && *p != '|') {
        p++;
    }
    if (*p != '|') {
        return -1;
    }
    *p++ = '\0';

    version = p;
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

    abi_text = NULL;
    if (*p == '|') {
        *p++ = '\0';
        abi_text = p;
    }

    if (!is_valid_package_name(name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0) {
        return -1;
    }
    if (path[0] != '/') {
        return -1;
    }

    out->abi = MYAOS_ABI_VERSION;
    if (abi_text && abi_text[0]) {
        if (parse_u32(abi_text, &out->abi) != 0) {
            return -1;
        }
    }

    str_copy(out->name, name, sizeof(out->name));
    str_copy(out->version, version, sizeof(out->version));
    str_copy(out->path, path, sizeof(out->path));
    return 0;
}

static int parse_installed_line(char* line, pkg_installed_entry_t* out) {
    char* p;
    char* name;
    char* version;
    char* source;

    if (!line || !out || !line[0]) {
        return -1;
    }

    p = line;
    name = p;
    while (*p && *p != '|') {
        p++;
    }
    if (*p != '|') {
        return -1;
    }
    *p++ = '\0';

    version = p;
    while (*p && *p != '|') {
        p++;
    }
    if (*p != '|') {
        return -1;
    }
    *p++ = '\0';

    source = p;

    if (!is_valid_package_name(name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0) {
        return -1;
    }

    str_copy(out->name, name, sizeof(out->name));
    str_copy(out->version, version, sizeof(out->version));
    str_copy(out->source, source, sizeof(out->source));
    return 0;
}

static int load_repo_entries(const char* index_path, pkg_repo_entry_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t size = 0;
    char* cursor;
    uint32_t count = 0;

    if (read_text_file(index_path, g_text_buf, sizeof(g_text_buf), &size) != 0) {
        return -1;
    }

    cursor = (char*)g_text_buf;
    while (1) {
        char* line = next_line(&cursor);
        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (count >= max_entries) {
            break;
        }
        if (parse_repo_line(line, &out[count]) == 0) {
            count++;
        }
    }

    if (out_count) {
        *out_count = count;
    }
    return 0;
}

static int save_repo_entries(const char* index_path, const pkg_repo_entry_t* entries, uint32_t count) {
    char out[PKG_TEXT_MAX];
    uint32_t pos = 0;

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "# MYAOS_REPO1\n") != 0) {
        return -1;
    }
    if (append_text(out, sizeof(out), &pos, "# name|version|package_path|abi\n") != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        char abi_text[16];
        mya_u32_to_dec(entries[i].abi, abi_text, sizeof(abi_text));
        if (append_text(out, sizeof(out), &pos, entries[i].name) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].version) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].path) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, abi_text) != 0 ||
            append_text(out, sizeof(out), &pos, "\n") != 0) {
            return -1;
        }
    }

    ensure_dir_tree("/repo");
    return mya_fs_write(index_path, out, pos);
}

static int load_installed_entries(pkg_installed_entry_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t size = 0;
    uint32_t count = 0;
    char* cursor;

    if (read_text_file(PKG_DB_FILE, g_text_buf, sizeof(g_text_buf), &size) != 0) {
        *out_count = 0;
        return 0;
    }

    cursor = (char*)g_text_buf;
    while (1) {
        char* line = next_line(&cursor);
        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (count >= max_entries) {
            break;
        }
        if (parse_installed_line(line, &out[count]) == 0) {
            count++;
        }
    }

    if (out_count) {
        *out_count = count;
    }
    return 0;
}

static int save_installed_entries(const pkg_installed_entry_t* entries, uint32_t count) {
    char out[PKG_TEXT_MAX];
    uint32_t pos = 0;

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "# MYAOS_INSTALLED1\n") != 0) {
        return -1;
    }
    if (append_text(out, sizeof(out), &pos, "# name|version|source\n") != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (append_text(out, sizeof(out), &pos, entries[i].name) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].version) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].source) != 0 ||
            append_text(out, sizeof(out), &pos, "\n") != 0) {
            return -1;
        }
    }

    ensure_dir_tree("/var");
    ensure_dir_tree(PKG_DB_DIR);
    return mya_fs_write(PKG_DB_FILE, out, pos);
}

static int write_meta_file(
    const char* pkg_name,
    const char* version,
    uint32_t abi,
    const char* source,
    uint32_t file_count,
    const char* files_blob
) {
    char path[MYAOS_PATH_MAX];
    char out[PKG_META_MAX];
    char value[16];
    uint32_t pos = 0;

    if (build_meta_path(pkg_name, path, sizeof(path)) != 0) {
        return -1;
    }

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "name=") != 0 ||
        append_text(out, sizeof(out), &pos, pkg_name) != 0 ||
        append_text(out, sizeof(out), &pos, "\nversion=") != 0 ||
        append_text(out, sizeof(out), &pos, version) != 0 ||
        append_text(out, sizeof(out), &pos, "\n") != 0) {
        return -1;
    }

    mya_u32_to_dec(abi, value, sizeof(value));
    if (append_text(out, sizeof(out), &pos, "abi=") != 0 ||
        append_text(out, sizeof(out), &pos, value) != 0 ||
        append_text(out, sizeof(out), &pos, "\n") != 0) {
        return -1;
    }

    mya_u32_to_dec(file_count, value, sizeof(value));
    if (append_text(out, sizeof(out), &pos, "files=") != 0 ||
        append_text(out, sizeof(out), &pos, value) != 0 ||
        append_text(out, sizeof(out), &pos, "\nsource=") != 0 ||
        append_text(out, sizeof(out), &pos, source) != 0 ||
        append_text(out, sizeof(out), &pos, "\n") != 0) {
        return -1;
    }

    if (files_blob && files_blob[0]) {
        if (append_text(out, sizeof(out), &pos, files_blob) != 0) {
            return -1;
        }
    }

    return mya_fs_write(path, out, pos);
}

static int record_installed_package(const char* pkg_name, const char* version, const char* source) {
    pkg_installed_entry_t entries[PKG_INSTALLED_MAX];
    uint32_t count = 0;
    uint8_t replaced = 0;

    if (load_installed_entries(entries, PKG_INSTALLED_MAX, &count) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, pkg_name)) {
            str_copy(entries[i].version, version, sizeof(entries[i].version));
            str_copy(entries[i].source, source, sizeof(entries[i].source));
            replaced = 1u;
            break;
        }
    }

    if (!replaced) {
        if (count >= PKG_INSTALLED_MAX) {
            return -1;
        }
        str_copy(entries[count].name, pkg_name, sizeof(entries[count].name));
        str_copy(entries[count].version, version, sizeof(entries[count].version));
        str_copy(entries[count].source, source, sizeof(entries[count].source));
        count++;
    }

    return save_installed_entries(entries, count);
}

static int install_single_file(const char* path, const char* hex_payload) {
    uint32_t byte_len = (uint32_t)str_len(hex_payload) / 2u;
    uint8_t empty = 0u;

    if (!path || path[0] != '/') {
        return -1;
    }

    ensure_parent_dirs(path);

    if (byte_len == 0u) {
        (void)mya_fs_touch(path);
        return mya_fs_write(path, &empty, 0u);
    }

    {
        uint8_t* decoded = (uint8_t*)mya_mem_map(byte_len, MYAOS_MEM_MAP_WRITABLE);
        int rc;

        if (!decoded) {
            return -1;
        }

        rc = decode_hex(hex_payload, decoded, byte_len, NULL);
        if (rc == 0) {
            rc = mya_fs_write(path, decoded, byte_len);
            if (rc != 0) {
                (void)mya_fs_touch(path);
                rc = mya_fs_write(path, decoded, byte_len);
            }
        }

        (void)mya_mem_unmap(decoded);
        return rc;
    }
}

static int install_package_file(const char* package_path, const char* source_tag) {
    uint32_t size = 0;
    char* cursor;
    char pkg_name[MYAOS_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char pending_file[MYAOS_PATH_MAX];
    char files_blob[PKG_FILES_META_MAX];
    uint32_t files_blob_pos = 0;
    uint32_t abi = MYAOS_ABI_VERSION;
    uint8_t abi_set = 0;
    uint32_t file_count = 0;

    if (!package_path) {
        return -1;
    }

    if (read_text_file(package_path, g_pkg_file_buf, sizeof(g_pkg_file_buf), &size) != 0) {
        mya_putln("pkg: failed to read package");
        return -1;
    }

    pkg_name[0] = '\0';
    version[0] = '\0';
    pending_file[0] = '\0';
    files_blob[0] = '\0';

    cursor = (char*)g_pkg_file_buf;
    {
        char* line0 = next_line(&cursor);
        if (!line0 || !str_eq(line0, "MYAPKG1")) {
            mya_putln("pkg: bad package format");
            return -1;
        }
    }

    while (1) {
        char* line = next_line(&cursor);
        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }

        if (str_starts_with(line, "name=")) {
            str_copy(pkg_name, line + 5, sizeof(pkg_name));
            continue;
        }
        if (str_starts_with(line, "version=")) {
            str_copy(version, line + 8, sizeof(version));
            continue;
        }
        if (str_starts_with(line, "abi=")) {
            if (parse_u32(line + 4, &abi) != 0) {
                mya_putln("pkg: bad abi field");
                return -1;
            }
            abi_set = 1u;
            continue;
        }
        if (str_starts_with(line, "file=")) {
            str_copy(pending_file, line + 5, sizeof(pending_file));
            continue;
        }
        if (str_starts_with(line, "hex=")) {
            if (!pending_file[0]) {
                mya_putln("pkg: payload without file field");
                return -1;
            }
            if (install_single_file(pending_file, line + 4) != 0) {
                mya_puts("pkg: failed to install ");
                mya_putln(pending_file);
                return -1;
            }

            if (append_text(files_blob, sizeof(files_blob), &files_blob_pos, "file=") == 0 &&
                append_text(files_blob, sizeof(files_blob), &files_blob_pos, pending_file) == 0) {
                (void)append_text(files_blob, sizeof(files_blob), &files_blob_pos, "\n");
            }

            file_count++;
            pending_file[0] = '\0';
            continue;
        }
    }

    if (!is_valid_package_name(pkg_name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0 || file_count == 0u) {
        mya_putln("pkg: package metadata is incomplete");
        return -1;
    }

    if (!abi_set) {
        abi = MYAOS_ABI_VERSION;
    }
    if (abi > MYAOS_ABI_VERSION) {
        mya_puts("pkg: abi too new for system: ");
        mya_put_u32(abi);
        mya_puts(" > ");
        mya_put_u32(MYAOS_ABI_VERSION);
        mya_puts("\n");
        return -1;
    }

    ensure_dir_tree("/var");
    ensure_dir_tree(PKG_DB_DIR);

    if (write_meta_file(pkg_name, version, abi, source_tag ? source_tag : package_path, file_count, files_blob) != 0) {
        mya_putln("pkg: failed to write metadata");
        return -1;
    }
    if (record_installed_package(pkg_name, version, source_tag ? source_tag : package_path) != 0) {
        mya_putln("pkg: failed to update installed index");
        return -1;
    }

    mya_puts("installed ");
    mya_puts(pkg_name);
    mya_puts(" ");
    mya_puts(version);
    mya_puts("\n");
    return 0;
}

static int cmd_list(void) {
    pkg_installed_entry_t entries[PKG_INSTALLED_MAX];
    uint32_t count = 0;

    if (load_installed_entries(entries, PKG_INSTALLED_MAX, &count) != 0 || count == 0u) {
        mya_putln("pkg: no installed packages");
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(entries[i].name);
        mya_puts(" ");
        mya_puts(entries[i].version);
        mya_puts(" ");
        mya_puts(entries[i].source);
        mya_puts("\n");
    }
    return 0;
}

static int cmd_info(const char* name) {
    char path[MYAOS_PATH_MAX];
    uint32_t size = 0;

    if (build_meta_path(name, path, sizeof(path)) != 0) {
        mya_putln("pkg: bad package name");
        return 1;
    }
    if (read_text_file(path, g_text_buf, sizeof(g_text_buf), &size) != 0) {
        mya_putln("pkg: package not found");
        return 1;
    }

    mya_puts((const char*)g_text_buf);
    if (size > 0u && g_text_buf[size - 1u] != '\n') {
        mya_puts("\n");
    }
    return 0;
}

static int cmd_avail(const char* index_path) {
    pkg_repo_entry_t entries[PKG_REPO_MAX];
    uint32_t count = 0;

    if (load_repo_entries(index_path, entries, PKG_REPO_MAX, &count) != 0) {
        mya_putln("pkg: repo index read failed");
        return 1;
    }

    if (count == 0u) {
        mya_putln("pkg: repo is empty");
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(entries[i].name);
        mya_puts(" ");
        mya_puts(entries[i].version);
        mya_puts(" ");
        mya_puts(entries[i].path);
        mya_puts(" abi=");
        mya_put_u32(entries[i].abi);
        mya_puts("\n");
    }

    return 0;
}

static int cmd_install_from(const char* name, const char* index_path) {
    pkg_repo_entry_t entries[PKG_REPO_MAX];
    uint32_t count = 0;
    int32_t best_idx = -1;

    if (!is_valid_package_name(name)) {
        mya_putln("pkg: bad package name");
        return 1;
    }
    if (load_repo_entries(index_path, entries, PKG_REPO_MAX, &count) != 0) {
        mya_putln("pkg: repo index read failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!str_eq(entries[i].name, name)) {
            continue;
        }
        if (entries[i].abi > MYAOS_ABI_VERSION) {
            continue;
        }
        if (best_idx < 0 || compare_version(entries[best_idx].version, entries[i].version) < 0) {
            best_idx = (int32_t)i;
        }
    }

    if (best_idx < 0) {
        mya_putln("pkg: package not found in repo");
        return 1;
    }

    return install_package_file(entries[best_idx].path, entries[best_idx].path) == 0 ? 0 : 1;
}

static int cmd_upgrade(const char* index_path) {
    pkg_installed_entry_t installed[PKG_INSTALLED_MAX];
    pkg_repo_entry_t repo[PKG_REPO_MAX];
    uint32_t installed_count = 0;
    uint32_t repo_count = 0;
    uint32_t updated = 0;
    uint32_t failed = 0;

    if (load_installed_entries(installed, PKG_INSTALLED_MAX, &installed_count) != 0 || installed_count == 0u) {
        mya_putln("pkg: no installed packages");
        return 0;
    }
    if (load_repo_entries(index_path, repo, PKG_REPO_MAX, &repo_count) != 0) {
        mya_putln("pkg: repo index read failed");
        return 1;
    }

    for (uint32_t i = 0; i < installed_count; i++) {
        int32_t best_idx = -1;

        for (uint32_t j = 0; j < repo_count; j++) {
            if (!str_eq(repo[j].name, installed[i].name)) {
                continue;
            }
            if (repo[j].abi > MYAOS_ABI_VERSION) {
                continue;
            }
            if (compare_version(repo[j].version, installed[i].version) <= 0) {
                continue;
            }
            if (best_idx < 0 || compare_version(repo[best_idx].version, repo[j].version) < 0) {
                best_idx = (int32_t)j;
            }
        }

        if (best_idx < 0) {
            continue;
        }

        mya_puts("upgrading ");
        mya_puts(installed[i].name);
        mya_puts(" ");
        mya_puts(installed[i].version);
        mya_puts(" -> ");
        mya_puts(repo[best_idx].version);
        mya_puts("\n");

        if (install_package_file(repo[best_idx].path, repo[best_idx].path) == 0) {
            updated++;
        } else {
            failed++;
        }
    }

    mya_puts("pkg: upgraded ");
    mya_put_u32(updated);
    mya_puts(" package(s)");
    if (failed) {
        mya_puts(", failed ");
        mya_put_u32(failed);
    }
    mya_puts("\n");

    return failed ? 1 : 0;
}

static int cmd_repo_add(
    const char* index_path,
    const char* name,
    const char* version,
    const char* package_path,
    const char* abi_text
) {
    pkg_repo_entry_t entries[PKG_REPO_MAX];
    uint32_t count = 0;
    uint32_t abi = MYAOS_ABI_VERSION;
    int32_t existing_idx = -1;

    if (!is_valid_package_name(name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0 ||
        !package_path || package_path[0] != '/') {
        mya_putln("pkg: invalid repo-add args");
        return 1;
    }

    if (abi_text && abi_text[0] && parse_u32(abi_text, &abi) != 0) {
        mya_putln("pkg: invalid abi for repo-add");
        return 1;
    }

    if (load_repo_entries(index_path, entries, PKG_REPO_MAX, &count) != 0) {
        count = 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, name)) {
            existing_idx = (int32_t)i;
            break;
        }
    }

    if (existing_idx >= 0) {
        str_copy(entries[existing_idx].version, version, sizeof(entries[existing_idx].version));
        str_copy(entries[existing_idx].path, package_path, sizeof(entries[existing_idx].path));
        entries[existing_idx].abi = abi;
    } else {
        if (count >= PKG_REPO_MAX) {
            mya_putln("pkg: repo full");
            return 1;
        }
        str_copy(entries[count].name, name, sizeof(entries[count].name));
        str_copy(entries[count].version, version, sizeof(entries[count].version));
        str_copy(entries[count].path, package_path, sizeof(entries[count].path));
        entries[count].abi = abi;
        count++;
    }

    if (save_repo_entries(index_path, entries, count) != 0) {
        mya_putln("pkg: failed to write repo index");
        return 1;
    }

    mya_putln("pkg: repo updated");
    return 0;
}

static int cmd_repo_init(const char* index_path) {
    pkg_repo_entry_t none[1];
    if (save_repo_entries(index_path, none, 0) != 0) {
        mya_putln("pkg: failed to initialize repo index");
        return 1;
    }
    mya_putln("pkg: repo initialized");
    return 0;
}

static void print_usage(void) {
    mya_putln("usage:");
    mya_putln("  pkg install <package_path>");
    mya_putln("  pkg list");
    mya_putln("  pkg info <name>");
    mya_putln("  pkg avail [repo_index]");
    mya_putln("  pkg install-from <name> [repo_index]");
    mya_putln("  pkg upgrade [repo_index]");
    mya_putln("  pkg repo-init [repo_index]");
    mya_putln("  pkg repo-add <repo_index> <name> <version> <package_path> [abi]");
}

int program_main(int argc, char** argv) {
    const char* cmd;
    const char* index_path = PKG_INDEX_DEFAULT;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    cmd = argv[1];

    if (str_eq(cmd, "install")) {
        if (argc < 3) {
            mya_putln("pkg: install requires package path");
            return 1;
        }
        return install_package_file(argv[2], argv[2]) == 0 ? 0 : 1;
    }

    if (str_eq(cmd, "list")) {
        return cmd_list();
    }

    if (str_eq(cmd, "info")) {
        if (argc < 3) {
            mya_putln("pkg: info requires package name");
            return 1;
        }
        return cmd_info(argv[2]);
    }

    if (str_eq(cmd, "avail")) {
        if (argc >= 3) {
            index_path = argv[2];
        }
        return cmd_avail(index_path);
    }

    if (str_eq(cmd, "install-from")) {
        if (argc < 3) {
            mya_putln("pkg: install-from requires package name");
            return 1;
        }
        if (argc >= 4) {
            index_path = argv[3];
        }
        return cmd_install_from(argv[2], index_path);
    }

    if (str_eq(cmd, "upgrade")) {
        if (argc >= 3) {
            index_path = argv[2];
        }
        return cmd_upgrade(index_path);
    }

    if (str_eq(cmd, "repo-init")) {
        if (argc >= 3) {
            index_path = argv[2];
        }
        return cmd_repo_init(index_path);
    }

    if (str_eq(cmd, "repo-add")) {
        if (argc < 6) {
            mya_putln("pkg: repo-add requires <repo_index> <name> <version> <package_path> [abi]");
            return 1;
        }
        return cmd_repo_add(argv[2], argv[3], argv[4], argv[5], argc >= 7 ? argv[6] : "");
    }

    print_usage();
    return 1;
}
