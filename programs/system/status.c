#include "../lib/myaos.h"
#include <stdint.h>

#define SVC_DB_PATH "/var/run/services.db"
#define SVC_TEXT_MAX 4096u
#define SVC_MAX_ENTRIES 64u

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

static void print_entry(const svc_entry_t* e) {
    int running = is_pid_running(e->pid);

    mya_puts("[svc:");
    mya_puts(e->name);
    mya_puts("] pid=");
    mya_put_u32((uint32_t)e->pid);
    mya_puts(" state=");
    mya_puts(running ? "running" : "stopped");
    if (!running) {
        mya_puts(" error=");
        if (e->pid <= 0) {
            mya_puts("invalid-pid");
        } else {
            mya_puts("process-missing");
        }
    }
    mya_puts(" exec=");
    mya_putln(e->exec);
}

int program_main(int argc, char** argv) {
    svc_entry_t entries[SVC_MAX_ENTRIES];
    uint32_t count = 0;
    uint8_t found = 0u;

    if (load_entries(entries, SVC_MAX_ENTRIES, &count) != 0) {
        mya_putln("status: failed to read service database");
        return 1;
    }
    if (count == 0u) {
        mya_putln("status: no services");
        return 0;
    }

    if (argc >= 2) {
        for (uint32_t i = 0; i < count; i++) {
            if (str_eq(entries[i].name, argv[1])) {
                print_entry(&entries[i]);
                found = 1u;
                break;
            }
        }
        if (!found) {
            mya_puts("[svc:");
            mya_puts(argv[1]);
            mya_puts("] not-found ");
            mya_putln(argv[1]);
            return 1;
        }
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        print_entry(&entries[i]);
    }
    return 0;
}
