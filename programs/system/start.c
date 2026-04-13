#include "../lib/myaos.h"
#include <stdint.h>

#define SVC_DB_PATH "/var/run/services.db"
#define SVC_TEXT_MAX 4096u
#define SVC_MAX_ENTRIES 64u
#define SVC_ARGV_MAX 16

typedef struct {
    char name[MYAOS_NAME_MAX];
    int32_t pid;
    char exec[MYAOS_PATH_MAX];
} svc_entry_t;

static uint8_t g_text[SVC_TEXT_MAX];

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
        int ok = (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 c == '_' || c == '-' || c == '.';
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

static void ensure_runtime_dirs(void) {
    (void)mya_fs_mkdir("/var");
    (void)mya_fs_mkdir("/var/run");
}

static void svc_tag_prefix(const char* name) {
    mya_puts("[svc:");
    mya_puts(name ? name : "?");
    mya_puts("] ");
}

static int load_entries(svc_entry_t* out, uint32_t max_entries, uint32_t* out_count) {
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

static int save_entries(const svc_entry_t* entries, uint32_t count) {
    char out[SVC_TEXT_MAX];
    uint32_t pos = 0;

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "# MYAOS_SVC_DB1\n") != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        char pid_text[16];

        mya_u32_to_dec((uint32_t)entries[i].pid, pid_text, sizeof(pid_text));
        if (append_text(out, sizeof(out), &pos, entries[i].name) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, pid_text) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].exec) != 0 ||
            append_text(out, sizeof(out), &pos, "\n") != 0) {
            return -1;
        }
    }

    ensure_runtime_dirs();
    return mya_fs_write(SVC_DB_PATH, out, pos);
}

static int find_entry(const svc_entry_t* entries, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int is_pid_running(int32_t pid) {
    myaos_proc_info_t procs[64];
    uint32_t count = 0;

    if (pid <= 0) {
        return 0;
    }
    if (mya_proc_list(procs, 64u, &count) != 0) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if ((int32_t)procs[i].pid == pid) {
            return 1;
        }
    }
    return 0;
}

int program_main(int argc, char** argv) {
    svc_entry_t entries[SVC_MAX_ENTRIES];
    uint32_t count = 0;
    int idx;
    int child_argc;
    const char* child_argv[SVC_ARGV_MAX];
    int32_t pid = -1;

    if (argc < 3) {
        mya_putln("usage: start <service_name> <exec_path> [args...]");
        return 1;
    }
    if (!is_valid_name(argv[1])) {
        mya_puts("start: invalid service name: ");
        mya_putln(argv[1]);
        return 1;
    }
    if (!argv[2][0] || argv[2][0] != '/') {
        mya_putln("start: exec_path must be absolute (/bin/...)");
        return 1;
    }

    child_argc = argc - 2;
    if (child_argc <= 0 || child_argc >= SVC_ARGV_MAX) {
        mya_putln("start: too many arguments");
        return 1;
    }
    for (int i = 0; i < child_argc; i++) {
        child_argv[i] = argv[i + 2];
    }
    child_argv[child_argc] = NULL;

    if (load_entries(entries, SVC_MAX_ENTRIES, &count) != 0) {
        mya_putln("start: failed to read service database");
        return 1;
    }

    idx = find_entry(entries, count, argv[1]);
    if (idx >= 0 && is_pid_running(entries[idx].pid)) {
        svc_tag_prefix(entries[idx].name);
        mya_puts("already-running pid=");
        mya_put_u32((uint32_t)entries[idx].pid);
        mya_puts("\n");
        return 0;
    }

    if (mya_proc_spawn(argv[2], child_argc, child_argv, MYAOS_SPAWN_BACKGROUND, &pid) != 0 || pid <= 0) {
        svc_tag_prefix(argv[1]);
        mya_puts("launch-failed exec=");
        mya_putln(argv[2]);
        mya_putln("hint: verify file exists and has execute permission");
        return 1;
    }

    if (idx >= 0) {
        entries[idx].pid = pid;
        str_copy(entries[idx].exec, argv[2], sizeof(entries[idx].exec));
    } else {
        if (count >= SVC_MAX_ENTRIES) {
            mya_putln("start: service database is full");
            return 1;
        }
        str_copy(entries[count].name, argv[1], sizeof(entries[count].name));
        entries[count].pid = pid;
        str_copy(entries[count].exec, argv[2], sizeof(entries[count].exec));
        count++;
    }

    if (save_entries(entries, count) != 0) {
        mya_putln("start: warning: failed to persist service state");
    }

    svc_tag_prefix(argv[1]);
    mya_puts("started pid=");
    mya_put_u32((uint32_t)pid);
    mya_puts("\n");
    return 0;
}
