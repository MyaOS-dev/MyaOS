#include "../lib/myaos.h"
#include <stdint.h>

#define SVC_DB_PATH "/var/run/services.db"
#define SVC_ENABLE_DB_PATH "/etc/services.enabled"
#define SVC_TEXT_MAX 4096u
#define SVC_MAX_ENTRIES 64u

typedef struct {
    char name[MYAOS_NAME_MAX];
    int32_t pid;
    char exec[MYAOS_PATH_MAX];
} svc_entry_t;

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

static int load_services(svc_entry_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t size = 0;
    uint32_t count = 0;
    char* cursor;

    if (!out || !out_count) {
        return -1;
    }
    if (mya_fs_read(SVC_DB_PATH, g_text, sizeof(g_text) - 1u, &size) != 0) {
        *out_count = 0u;
        return 0;
    }

    g_text[size] = '\0';
    cursor = (char*)g_text;

    while (1) {
        char* line = next_line(&cursor);
        char* p0;
        char* p1;
        uint32_t pid_u32 = 0;

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

        p1 = p0;
        while (*p1 && *p1 != '|') {
            p1++;
        }
        if (*p1 != '|') {
            continue;
        }
        *p1++ = '\0';

        if (!is_valid_name(line) || parse_u32(p0, &pid_u32) != 0 || !p1[0]) {
            continue;
        }

        str_copy(out[count].name, line, sizeof(out[count].name));
        out[count].pid = (int32_t)pid_u32;
        str_copy(out[count].exec, p1, sizeof(out[count].exec));
        count++;
    }

    *out_count = count;
    return 0;
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

static int find_service(const svc_entry_t* entries, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_enabled(const svc_enabled_entry_t* entries, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static void svc_tag_prefix(const char* name) {
    mya_puts("[svc:");
    mya_puts(name ? name : "?");
    mya_puts("] ");
}

int program_main(int argc, char** argv) {
    svc_entry_t services[SVC_MAX_ENTRIES];
    svc_enabled_entry_t enabled[SVC_MAX_ENTRIES];
    uint32_t service_count = 0;
    uint32_t enabled_count = 0;
    const char* exec_path = NULL;
    int idx;

    if (argc < 2 || argc > 3) {
        mya_putln("usage: enable <service_name> [exec_path]");
        return 1;
    }
    if (!is_valid_name(argv[1])) {
        mya_puts("enable: invalid service name: ");
        mya_putln(argv[1]);
        return 1;
    }

    if (argc >= 3) {
        exec_path = argv[2];
    } else {
        if (load_services(services, SVC_MAX_ENTRIES, &service_count) != 0) {
            mya_putln("enable: failed to read service database");
            return 1;
        }
        idx = find_service(services, service_count, argv[1]);
        if (idx < 0) {
            mya_puts("enable: service not found: ");
            mya_putln(argv[1]);
            mya_putln("hint: start it first or pass exec_path");
            return 1;
        }
        exec_path = services[(uint32_t)idx].exec;
    }

    if (!exec_path || exec_path[0] != '/') {
        mya_putln("enable: exec_path must be absolute (/bin/...)");
        return 1;
    }

    if (load_enabled(enabled, SVC_MAX_ENTRIES, &enabled_count) != 0) {
        mya_putln("enable: failed to read enabled service list");
        return 1;
    }

    idx = find_enabled(enabled, enabled_count, argv[1]);
    if (idx >= 0) {
        str_copy(enabled[(uint32_t)idx].exec, exec_path, sizeof(enabled[(uint32_t)idx].exec));
    } else {
        if (enabled_count >= SVC_MAX_ENTRIES) {
            mya_putln("enable: enabled service list is full");
            return 1;
        }
        str_copy(enabled[enabled_count].name, argv[1], sizeof(enabled[enabled_count].name));
        str_copy(enabled[enabled_count].exec, exec_path, sizeof(enabled[enabled_count].exec));
        enabled_count++;
    }

    if (save_enabled(enabled, enabled_count) != 0) {
        mya_putln("enable: failed to persist enabled service list");
        return 1;
    }

    svc_tag_prefix(argv[1]);
    mya_puts("enabled exec=");
    mya_putln(exec_path);
    return 0;
}
