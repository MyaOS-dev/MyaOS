#include "../lib/myaos.h"

#define WHICH_MAX_ENTRIES 128u

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int str_contains_ci(const char* text, const char* needle) {
    size_t tlen = str_len(text);
    size_t nlen = str_len(needle);

    if (!needle || !needle[0]) {
        return 1;
    }
    if (!text || nlen > tlen) {
        return 0;
    }

    for (size_t i = 0; i + nlen <= tlen; i++) {
        size_t j = 0;
        while (j < nlen) {
            char a = text[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            j++;
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

static int str_ends_with_ci(const char* text, const char* suffix) {
    size_t tlen = str_len(text);
    size_t slen = str_len(suffix);
    if (slen > tlen) {
        return 0;
    }
    return str_contains_ci(text + (tlen - slen), suffix);
}

static void print_manifest_matches(const char* dir, const char* pattern, uint32_t* io_found) {
    myaos_dirent_t entries[WHICH_MAX_ENTRIES];
    uint32_t count = 0u;

    if (mya_fs_list(dir, entries, WHICH_MAX_ENTRIES, &count) != 0) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        size_t len;
        char name[MYAOS_NAME_MAX];

        if (entries[i].type != MYAOS_NODE_FILE || !str_ends_with_ci(entries[i].name, ".cmd")) {
            continue;
        }

        len = str_len(entries[i].name);
        if (len <= 4u || len >= sizeof(name)) {
            continue;
        }
        for (size_t j = 0; j < len - 4u; j++) {
            name[j] = entries[i].name[j];
            name[j + 1u] = '\0';
        }
        name[len - 4u] = '\0';

        if (!str_contains_ci(name, pattern) && !str_contains_ci(entries[i].name, pattern)) {
            continue;
        }
        mya_puts("cmd ");
        mya_puts(name);
        mya_puts(" -> ");
        mya_puts(dir);
        mya_puts("/");
        mya_putln(entries[i].name);
        (*io_found)++;
    }
}

static void print_elf_matches(const char* dir, const char* pattern, uint32_t* io_found) {
    myaos_dirent_t entries[WHICH_MAX_ENTRIES];
    uint32_t count = 0u;

    if (mya_fs_list(dir, entries, WHICH_MAX_ENTRIES, &count) != 0) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        size_t len;
        char name[MYAOS_NAME_MAX];

        if (entries[i].type != MYAOS_NODE_FILE || !str_ends_with_ci(entries[i].name, ".elf")) {
            continue;
        }
        len = str_len(entries[i].name);
        if (len <= 4u || len >= sizeof(name)) {
            continue;
        }
        for (size_t j = 0; j < len - 4u; j++) {
            name[j] = entries[i].name[j];
            name[j + 1u] = '\0';
        }
        name[len - 4u] = '\0';

        if (!str_contains_ci(name, pattern) && !str_contains_ci(entries[i].name, pattern)) {
            continue;
        }
        mya_puts("elf ");
        mya_puts(name);
        mya_puts(" -> ");
        mya_puts(dir);
        mya_puts("/");
        mya_putln(entries[i].name);
        (*io_found)++;
    }
}

int program_main(int argc, char** argv) {
    const char* pattern = "";
    uint32_t found = 0u;

    if (argc >= 2) {
        pattern = argv[1];
    }

    print_manifest_matches("/cmd", pattern, &found);
    print_manifest_matches("/boot/cmd", pattern, &found);
    print_elf_matches("/bin", pattern, &found);
    print_elf_matches("/boot/bin", pattern, &found);

    if (found == 0u) {
        mya_putln("which: no commands matched");
        return 1;
    }
    return 0;
}
