#include "../lib/myaos.h"
#include <stdint.h>

#define SVC_ENABLE_DB_PATH "/etc/services.enabled"
#define SVC_TEXT_MAX 4096u
#define SVC_MAX_ENTRIES 64u

typedef struct {
    char name[MYAOS_NAME_MAX];
    char exec[MYAOS_PATH_MAX];
} svc_enabled_entry_t;

static uint8_t g_text[SVC_TEXT_MAX];

static void str_copy(char* out, const char* in, uint32_t out_size) {
    uint32_t i = 0;

    if (!out || out_size == 0u) {
        return;
    }
    if (!in) {
        out[0] = '\0';
        return;
    }

    while (i + 1u < out_size && in[i]) {
        out[i] = in[i];
        i++;
    }
    out[i] = '\0';
}

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

static int is_valid_name(const char* name) {
    uint32_t n = 0;
    if (!name || !name[0]) {
        return 0;
    }
    for (n = 0; name[n]; n++) {
        char c = name[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                 c == '.';
        if (!ok) {
            return 0;
        }
    }
    return n < MYAOS_NAME_MAX;
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

static void ensure_enable_dirs(void) {
    (void)mya_fs_mkdir("/etc");
}

static int load_enabled(svc_enabled_entry_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t size = 0;
    uint32_t count = 0;
    char* cursor;

    if (!out || !out_count) {
        return -1;
    }
    if (mya_fs_read(SVC_ENABLE_DB_PATH, g_text, sizeof(g_text) - 1u, &size) != 0) {
        *out_count = 0u;
        return 0;
    }

    g_text[size] = '\0';
    cursor = (char*)g_text;

    while (1) {
        char* line = next_line(&cursor);
        char* p0;

        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (count >= max_entries) {
            break;
        }

        p0 = line;
        while (*p0 && *p0 != '|') {
            p0++;
        }
        if (*p0 != '|') {
            continue;
        }
        *p0++ = '\0';

        if (!is_valid_name(line) || !p0[0]) {
            continue;
        }

        str_copy(out[count].name, line, sizeof(out[count].name));
        str_copy(out[count].exec, p0, sizeof(out[count].exec));
        count++;
    }

    *out_count = count;
    return 0;
}

static int save_enabled(const svc_enabled_entry_t* entries, uint32_t count) {
    char out[SVC_TEXT_MAX];
    uint32_t pos = 0;

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "# MYAOS_SVC_ENABLE1\n") != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (append_text(out, sizeof(out), &pos, entries[i].name) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].exec) != 0 ||
            append_text(out, sizeof(out), &pos, "\n") != 0) {
            return -1;
        }
    }

    ensure_enable_dirs();
    return mya_fs_write(SVC_ENABLE_DB_PATH, out, pos);
}

static void svc_tag_prefix(const char* name) {
    mya_puts("[svc:");
    mya_puts(name ? name : "?");
    mya_puts("] ");
}

int program_main(int argc, char** argv) {
    svc_enabled_entry_t entries[SVC_MAX_ENTRIES];
    svc_enabled_entry_t kept[SVC_MAX_ENTRIES];
    uint32_t count = 0;
    uint32_t kept_count = 0;
    uint8_t found = 0u;

    if (argc != 2) {
        mya_putln("usage: disable <service_name>");
        return 1;
    }
    if (!is_valid_name(argv[1])) {
        mya_puts("disable: invalid service name: ");
        mya_putln(argv[1]);
        return 1;
    }

    if (load_enabled(entries, SVC_MAX_ENTRIES, &count) != 0) {
        mya_putln("disable: failed to read enabled service list");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, argv[1])) {
            found = 1u;
            continue;
        }
        if (kept_count < SVC_MAX_ENTRIES) {
            str_copy(kept[kept_count].name, entries[i].name, sizeof(kept[kept_count].name));
            str_copy(kept[kept_count].exec, entries[i].exec, sizeof(kept[kept_count].exec));
            kept_count++;
        }
    }

    if (!found) {
        svc_tag_prefix(argv[1]);
        mya_putln("not-enabled");
        return 1;
    }

    if (save_enabled(kept, kept_count) != 0) {
        mya_putln("disable: failed to persist enabled service list");
        return 1;
    }

    svc_tag_prefix(argv[1]);
    mya_putln("disabled");
    return 0;
}
