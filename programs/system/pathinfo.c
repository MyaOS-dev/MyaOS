#include "../lib/myaos.h"
#include <stdint.h>

#define PATHINFO_MAX_MOUNTS 32u
#define PATHINFO_META_MAX 4096u
#define PATHINFO_PKG_MAX 32u

typedef struct {
    const char* path;
    const char* desc;
} path_desc_t;

static const path_desc_t g_paths[] = {
    {"/", "root filesystem"},
    {"/boot", "boot volume and fallback commands/programs"},
    {"/bin", "user executable programs (.elf)"},
    {"/cmd", "command manifests (.cmd)"},
    {"/home", "user data"},
    {"/tmp", "temporary files"},
    {"/var", "variable state"},
    {"/var/run", "runtime service/process state"},
    {"/sys", "system virtual information/configuration"},
    {"/dev", "device namespace"},
    {"/ram", "ramfs scratch mount"},
};

typedef struct {
    char name[MYAOS_NAME_MAX];
    char version[32];
    char source[MYAOS_PATH_MAX];
} package_hit_t;

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int str_starts_with(const char* text, const char* prefix) {
    uint32_t i = 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static uint32_t str_len(const char* text) {
    uint32_t len = 0u;
    while (text && text[len]) {
        len++;
    }
    return len;
}

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0u;

    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_ends_with(const char* text, const char* suffix) {
    uint32_t text_len = str_len(text);
    uint32_t suffix_len = str_len(suffix);

    if (suffix_len > text_len) {
        return 0;
    }
    return str_eq(text + (text_len - suffix_len), suffix);
}

static int path_has_prefix(const char* path, const char* prefix) {
    uint32_t prefix_len;

    if (!path || !prefix) {
        return 0;
    }
    if (str_eq(prefix, "/")) {
        return path[0] == '/';
    }
    if (!str_starts_with(path, prefix)) {
        return 0;
    }

    prefix_len = str_len(prefix);
    return (path[prefix_len] == '\0' || path[prefix_len] == '/') ? 1 : 0;
}

static int mount_runtime_persistence_uncertain(const myaos_mount_info_t* mount) {
    if (!mount || mount->read_only) {
        return 0;
    }
    return str_eq(mount->fs_name, "fat32") ||
           str_eq(mount->fs_name, "ext2") ||
           str_eq(mount->fs_name, "ext3") ||
           str_eq(mount->fs_name, "ext4") ||
           str_eq(mount->fs_name, "ntfs");
}

static void normalize_path(const char* in, char* out, uint32_t out_size) {
    uint32_t len;

    if (!out || out_size == 0u) {
        return;
    }
    if (!in || !in[0]) {
        out[0] = '\0';
        return;
    }

    len = 0u;
    while (in[len] && len + 1u < out_size) {
        out[len] = in[len];
        len++;
    }
    out[len] = '\0';

    while (len > 1u && out[len - 1u] == '/') {
        out[len - 1u] = '\0';
        len--;
    }
}

static void print_mapping(const char* path, const char* desc) {
    mya_puts(path);
    mya_puts(" -> ");
    mya_putln(desc);
}

static void print_mount_info(const char* path) {
    myaos_mount_info_t mounts[PATHINFO_MAX_MOUNTS];
    myaos_mount_info_t* best = NULL;
    uint32_t count = 0u;
    uint32_t best_len = 0u;

    if (!path) {
        return;
    }
    if (mya_fs_mounts(mounts, PATHINFO_MAX_MOUNTS, &count) != 0) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t len;
        if (!path_has_prefix(path, mounts[i].path)) {
            continue;
        }
        len = str_len(mounts[i].path);
        if (!best || len > best_len) {
            best = &mounts[i];
            best_len = len;
        }
    }

    if (!best) {
        return;
    }

    mya_puts("mount=");
    mya_puts(best->path);
    mya_puts(" fs=");
    mya_puts(best->fs_name[0] ? best->fs_name : "unknown");
    mya_puts(" source=");
    mya_puts(best->source[0] ? best->source : "unknown");
    mya_puts(" mode=");
    mya_putln(best->read_only ? "ro" : "rw");
    if (mount_runtime_persistence_uncertain(best)) {
        mya_putln("persistence=runtime-only (flush to source disk is not guaranteed)");
    }
}

static char* next_line(char** io_cursor) {
    char* start;
    char* cursor;

    if (!io_cursor || !*io_cursor) {
        return NULL;
    }

    cursor = *io_cursor;
    if (*cursor == '\0') {
        return NULL;
    }

    start = cursor;
    while (*cursor && *cursor != '\n' && *cursor != '\r') {
        cursor++;
    }
    if (*cursor == '\r') {
        *cursor++ = '\0';
    }
    if (*cursor == '\n') {
        *cursor++ = '\0';
    }
    *io_cursor = cursor;
    return start;
}

static int find_package_hits(const char* path, package_hit_t* out_hits, uint32_t max_hits, uint32_t* out_count) {
    myaos_dirent_t entries[PATHINFO_PKG_MAX];
    uint8_t meta_buf[PATHINFO_META_MAX];
    uint32_t count = 0u;
    uint32_t found = 0u;

    if (!path || !out_hits || max_hits == 0u || !out_count) {
        return -1;
    }
    *out_count = 0u;

    if (mya_fs_list("/var/pkg", entries, PATHINFO_PKG_MAX, &count) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count && found < max_hits; i++) {
        char meta_path[MYAOS_PATH_MAX];
        char pkg_name[MYAOS_NAME_MAX];
        char* cursor;
        uint32_t read_size = 0u;
        uint32_t name_len;
        uint8_t matched = 0u;

        if (entries[i].type != MYAOS_NODE_FILE || !str_ends_with(entries[i].name, ".meta")) {
            continue;
        }

        if (str_len(entries[i].name) + 10u >= sizeof(meta_path)) {
            continue;
        }
        str_copy(meta_path, "/var/pkg/", sizeof(meta_path));
        str_copy(meta_path + 9, entries[i].name, (uint32_t)(sizeof(meta_path) - 9u));
        if (mya_fs_read(meta_path, meta_buf, PATHINFO_META_MAX - 1u, &read_size) != 0) {
            continue;
        }
        meta_buf[read_size] = '\0';

        name_len = str_len(entries[i].name);
        if (name_len <= 5u) {
            continue;
        }
        str_copy(pkg_name, entries[i].name, sizeof(pkg_name));
        pkg_name[name_len - 5u] = '\0';

        str_copy(out_hits[found].name, pkg_name, sizeof(out_hits[found].name));
        out_hits[found].version[0] = '\0';
        out_hits[found].source[0] = '\0';

        cursor = (char*)meta_buf;
        for (;;) {
            char* line = next_line(&cursor);
            if (!line) {
                break;
            }
            if (str_starts_with(line, "name=")) {
                str_copy(out_hits[found].name, line + 5, sizeof(out_hits[found].name));
            } else if (str_starts_with(line, "version=")) {
                str_copy(out_hits[found].version, line + 8, sizeof(out_hits[found].version));
            } else if (str_starts_with(line, "source=")) {
                str_copy(out_hits[found].source, line + 7, sizeof(out_hits[found].source));
            } else if (str_starts_with(line, "file=") && str_eq(line + 5, path)) {
                matched = 1u;
            }
        }

        if (matched) {
            found++;
        }
    }

    *out_count = found;
    return found > 0u ? 0 : -1;
}

static void print_package_hits(const char* path) {
    package_hit_t hits[PATHINFO_PKG_MAX];
    uint32_t count = 0u;

    if (find_package_hits(path, hits, PATHINFO_PKG_MAX, &count) != 0) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts("package=");
        mya_puts(hits[i].name[0] ? hits[i].name : "unknown");
        if (hits[i].version[0]) {
            mya_puts(" version=");
            mya_puts(hits[i].version);
        }
        if (hits[i].source[0]) {
            mya_puts(" source=");
            mya_puts(hits[i].source);
        }
        mya_putln("");
    }
}

static void print_usage(void) {
    mya_putln("usage: pathinfo [PATH]");
    mya_putln("without PATH prints known directory roles");
    mya_putln("with PATH also shows mount source and owning package when known");
}

static void print_all(void) {
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_paths) / sizeof(g_paths[0])); i++) {
        print_mapping(g_paths[i].path, g_paths[i].desc);
    }
}

static int explain_one(const char* raw_path) {
    char path[MYAOS_PATH_MAX];
    int rc = 1;

    normalize_path(raw_path, path, sizeof(path));
    if (!path[0]) {
        return -1;
    }

    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_paths) / sizeof(g_paths[0])); i++) {
        if (str_eq(path, g_paths[i].path)) {
            print_mapping(g_paths[i].path, g_paths[i].desc);
            rc = 0;
            goto done;
        }
    }

    if (str_starts_with(path, "/home/")) {
        print_mapping(path, "file inside user data area");
        rc = 0;
        goto done;
    }
    if (str_starts_with(path, "/var/run/")) {
        print_mapping(path, "runtime state file (typically regenerated)");
        rc = 0;
        goto done;
    }
    if (str_starts_with(path, "/tmp/")) {
        print_mapping(path, "temporary file/directory");
        rc = 0;
        goto done;
    }
    if (str_starts_with(path, "/sys/")) {
        print_mapping(path, "virtual system info/config path");
        rc = 0;
        goto done;
    }
    if (str_starts_with(path, "/dev/")) {
        print_mapping(path, "device entry");
        rc = 0;
        goto done;
    }
    if (str_starts_with(path, "/boot/")) {
        print_mapping(path, "boot volume file");
        rc = 0;
        goto done;
    }
    if (str_starts_with(path, "/bin/")) {
        print_mapping(path, "executable program");
        rc = 0;
        goto done;
    }
    if (str_starts_with(path, "/cmd/")) {
        print_mapping(path, "command manifest");
        rc = 0;
        goto done;
    }

    mya_puts(path);
    mya_putln(" -> unknown role");

done:
    print_mount_info(path);
    print_package_hits(path);
    return rc;
}

int program_main(int argc, char** argv) {
    if (argc == 1) {
        print_all();
        return 0;
    }
    if (argc == 2) {
        return explain_one(argv[1]);
    }

    print_usage();
    return 1;
}
