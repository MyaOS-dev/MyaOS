#include "../lib/myaos.h"

#define FIND_LIST_MAX 64u
#define FIND_MAX_DEPTH 24u

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

static int str_contains(const char* text, const char* needle) {
    size_t text_len;
    size_t needle_len;

    if (!needle || !needle[0]) {
        return 1;
    }
    if (!text) {
        return 0;
    }

    text_len = str_len(text);
    needle_len = str_len(needle);
    if (needle_len > text_len) {
        return 0;
    }

    for (size_t i = 0; i + needle_len <= text_len; i++) {
        size_t j = 0;
        while (j < needle_len && text[i + j] == needle[j]) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }

    return 0;
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

static void print_path(const char* path, uint8_t type) {
    mya_puts(path);
    if (type == MYAOS_NODE_DIR) {
        mya_puts("/");
    } else if (type == MYAOS_NODE_MOUNT) {
        mya_puts("@");
    }
    mya_puts("\n");
}

static int walk(const char* path, const char* pattern, uint32_t depth) {
    myaos_dirent_t entries[FIND_LIST_MAX];
    uint32_t count = 0;

    if (depth == 0u) {
        return 0;
    }

    if (mya_fs_list(path, entries, FIND_LIST_MAX, &count) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        char child[MYAOS_PATH_MAX];
        uint8_t is_dir;

        if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) {
            continue;
        }
        if (join_path(path, entries[i].name, child, sizeof(child)) != 0) {
            continue;
        }

        if (str_contains(entries[i].name, pattern) || str_contains(child, pattern)) {
            print_path(child, entries[i].type);
        }

        is_dir = (entries[i].type == MYAOS_NODE_DIR || entries[i].type == MYAOS_NODE_MOUNT) ? 1u : 0u;
        if (is_dir) {
            (void)walk(child, pattern, depth - 1u);
        }
    }

    return 0;
}

int program_main(int argc, char** argv) {
    const char* root = ".";
    const char* pattern = "";

    if (argc >= 2) {
        root = argv[1];
    }
    if (argc >= 3) {
        pattern = argv[2];
    }

    if (walk(root, pattern, FIND_MAX_DEPTH) != 0) {
        mya_putln("find: failed");
        return 1;
    }

    return 0;
}
