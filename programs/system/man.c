#include "../lib/myaos.h"

#define MAN_BUF_MAX 4096u

typedef struct {
    const char* topic;
    const char* text;
} man_page_t;

static const man_page_t g_pages[] = {
    {
        "pkg",
        "NAME\n"
        "  pkg - package manager\n\n"
        "SYNOPSIS\n"
        "  pkg install <file.pkg|http[s]://...>\n"
        "  pkg install --dry-run <file.pkg|http[s]://...>\n"
        "  pkg list\n"
        "  pkg info <name>\n"
        "  pkg avail [index]\n"
        "  pkg install-from <name> [index]\n"
        "  pkg upgrade [index]\n"
        "  pkg downgrade <name> <version> [index]\n"
        "  pkg dry-run <install|install-from|upgrade|downgrade|remove> ...\n"
        "  pkg remove <name>\n"
        "  pkg search <pattern> [index]\n\n"
        "EXAMPLES\n"
        "  pkg install /repo/demo.pkg\n"
        "  pkg install https://repo.example/mya/demo.pkg\n"
        "  pkg dry-run upgrade\n"
        "  pkg dry-run remove demo\n"
        "  pkg search demo\n"
    },
    {
        "find",
        "NAME\n"
        "  find - recursive file listing\n\n"
        "SYNOPSIS\n"
        "  find [path] [pattern]\n\n"
        "EXAMPLES\n"
        "  find /\n"
        "  find /bin sh\n"
    },
    {
        "du",
        "NAME\n"
        "  du - recursive size per directory\n\n"
        "SYNOPSIS\n"
        "  du [path]\n"
    },
    {
        "df",
        "NAME\n"
        "  df - mounted filesystems and disk sizes\n\n"
        "SYNOPSIS\n"
        "  df\n"
    },
    {
        "pathinfo",
        "NAME\n"
        "  pathinfo - explain path role, mount source, and package origin\n\n"
        "SYNOPSIS\n"
        "  pathinfo [path]\n\n"
        "EXAMPLES\n"
        "  pathinfo /boot\n"
        "  pathinfo /bin/pkg.elf\n"
    },
    {
        "msh",
        "NAME\n"
        "  msh - userspace shell\n\n"
        "SYNOPSIS\n"
        "  msh\n"
    },
};

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

static int build_manifest_path(const char* base, const char* topic, char* out, uint32_t out_size) {
    uint32_t pos = 0;

    if (!base || !topic || !out || out_size == 0u) {
        return -1;
    }

    for (uint32_t i = 0; base[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = base[i];
    }
    if (pos == 0u || out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = '/';
    }
    for (uint32_t i = 0; topic[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = topic[i];
    }
    if (pos + 4u >= out_size) {
        return -1;
    }
    out[pos++] = '.';
    out[pos++] = 'c';
    out[pos++] = 'm';
    out[pos++] = 'd';
    out[pos] = '\0';
    return 0;
}

static void print_topic_list(void) {
    mya_putln("usage: man <topic>");
    mya_putln("built-in topics: pkg, find, du, df, msh");
    mya_putln("tip: for command list use 'help'");
}

static int print_builtin_page(const char* topic) {
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_pages) / sizeof(g_pages[0])); i++) {
        if (str_eq(topic, g_pages[i].topic)) {
            mya_puts(g_pages[i].text);
            return 0;
        }
    }
    return -1;
}

static int print_manifest_page(const char* topic) {
    uint8_t buf[MAN_BUF_MAX];
    uint32_t size = 0;
    char path[MYAOS_PATH_MAX];
    uint32_t line = 0;

    if (build_manifest_path("/cmd", topic, path, sizeof(path)) != 0 ||
        mya_fs_read(path, buf, sizeof(buf) - 1u, &size) != 0) {
        if (build_manifest_path("/boot/cmd", topic, path, sizeof(path)) != 0 ||
            mya_fs_read(path, buf, sizeof(buf) - 1u, &size) != 0) {
            return -1;
        }
    }

    buf[size] = '\0';

    mya_puts("NAME\n  ");
    mya_puts(topic);
    mya_puts(" - ");

    for (uint32_t i = 0; i < size; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            line++;
            continue;
        }
        if (line == 1u) {
            char ch[2];
            ch[0] = (char)buf[i];
            ch[1] = '\0';
            mya_puts(ch);
        }
    }
    mya_puts("\n\nSYNOPSIS\n  ");
    mya_puts(topic);
    mya_puts(" [args...]\n");

    mya_puts("\nDETAILS\n  manifest: ");
    mya_putln(path);

    mya_puts("  exec: ");
    line = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            break;
        }
        if (str_starts_with((const char*)buf, "exec=") && i < 5u) {
            continue;
        }
        {
            char ch[2];
            ch[0] = (char)buf[i];
            ch[1] = '\0';
            mya_puts(ch);
        }
    }
    mya_puts("\n");

    return 0;
}

int program_main(int argc, char** argv) {
    if (argc < 2) {
        print_topic_list();
        return 1;
    }

    if (print_builtin_page(argv[1]) == 0) {
        return 0;
    }
    if (print_manifest_page(argv[1]) == 0) {
        return 0;
    }

    mya_puts("man: topic not found: ");
    mya_putln(argv[1]);
    return 1;
}
