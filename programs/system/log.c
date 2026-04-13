#include "../lib/myaos.h"
#include <stdint.h>

#define LOG_BUF_MAX (64u * 1024u)
#define LOG_LINE_MAX 512u
#define LOG_SERVICE_MARKER_MAX (MYAOS_NAME_MAX + 8u)

static uint8_t g_log_buf[LOG_BUF_MAX];
static uint8_t g_export_buf[LOG_BUF_MAX];

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

static int parse_u64(const char* text, uint64_t* out) {
    uint64_t value = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }
    for (uint32_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
    }
    *out = value;
    return 0;
}

static int is_valid_service_name(const char* name) {
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
    return (n < MYAOS_NAME_MAX) ? 1 : 0;
}

static int build_service_marker(const char* service, char* out, uint32_t out_size) {
    uint32_t pos = 0;

    if (!service || !out || out_size < 8u || !is_valid_service_name(service)) {
        return -1;
    }

    out[pos++] = '[';
    out[pos++] = 's';
    out[pos++] = 'v';
    out[pos++] = 'c';
    out[pos++] = ':';

    for (uint32_t i = 0; service[i]; i++) {
        if (pos + 2u >= out_size) {
            return -1;
        }
        out[pos++] = service[i];
    }

    out[pos++] = ']';
    out[pos] = '\0';
    return 0;
}

static int line_contains(const char* line, uint32_t len, const char* needle) {
    uint32_t needle_len = 0;

    if (!needle || !needle[0]) {
        return 1;
    }
    while (needle[needle_len]) {
        needle_len++;
    }
    if (needle_len > len) {
        return 0;
    }

    for (uint32_t i = 0; i + needle_len <= len; i++) {
        uint32_t j = 0;
        while (j < needle_len && line[i + j] == needle[j]) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int parse_line_tick(const char* line, uint32_t len, uint64_t* out_tick) {
    uint64_t value = 0;
    uint32_t i = 1;

    if (!line || len < 3u || !out_tick) {
        return -1;
    }
    if (line[0] != '[') {
        return -1;
    }

    while (i < len && line[i] >= '0' && line[i] <= '9') {
        value = value * 10u + (uint64_t)(line[i] - '0');
        i++;
    }
    if (i < 2u || i >= len || line[i] != ']') {
        return -1;
    }
    *out_tick = value;
    return 0;
}

static void print_line(const char* line, uint32_t len) {
    char temp[LOG_LINE_MAX];
    uint32_t copy = (len < (uint32_t)(sizeof(temp) - 1u)) ? len : (uint32_t)(sizeof(temp) - 1u);

    for (uint32_t i = 0; i < copy; i++) {
        temp[i] = line[i];
    }
    temp[copy] = '\0';
    mya_putln(temp);
}

static void usage(void) {
    mya_putln(
        "usage: log [--file PATH] [--since TICK] [--until TICK] [--contains TEXT] [--service NAME] [--export PATH]"
    );
    mya_putln("default file: /sys/log");
}

int program_main(int argc, char** argv) {
    const char* file_path = "/sys/log";
    const char* contains = NULL;
    const char* service = NULL;
    const char* export_path = NULL;
    uint8_t has_since = 0u;
    uint8_t has_until = 0u;
    uint8_t has_service = 0u;
    uint8_t has_export = 0u;
    uint64_t since_tick = 0u;
    uint64_t until_tick = 0u;
    char service_marker[LOG_SERVICE_MARKER_MAX];
    uint32_t size = 0;
    uint32_t line_start = 0;
    uint32_t shown = 0;
    uint32_t export_len = 0;
    uint8_t export_truncated = 0u;

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "--file")) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            file_path = argv[++i];
            continue;
        }
        if (str_eq(argv[i], "--since")) {
            if (i + 1 >= argc || parse_u64(argv[++i], &since_tick) != 0) {
                usage();
                return 1;
            }
            has_since = 1u;
            continue;
        }
        if (str_eq(argv[i], "--until")) {
            if (i + 1 >= argc || parse_u64(argv[++i], &until_tick) != 0) {
                usage();
                return 1;
            }
            has_until = 1u;
            continue;
        }
        if (str_eq(argv[i], "--contains")) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            contains = argv[++i];
            continue;
        }
        if (str_eq(argv[i], "--service")) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            service = argv[++i];
            has_service = 1u;
            continue;
        }
        if (str_eq(argv[i], "--export")) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            export_path = argv[++i];
            has_export = 1u;
            continue;
        }

        usage();
        return 1;
    }

    if (has_service) {
        if (build_service_marker(service, service_marker, sizeof(service_marker)) != 0) {
            mya_putln("log: invalid service name");
            return 1;
        }
    }

    if (mya_fs_read(file_path, g_log_buf, sizeof(g_log_buf) - 1u, &size) != 0) {
        mya_puts("log: failed to read ");
        mya_putln(file_path);
        return 1;
    }
    g_log_buf[size] = '\0';

    for (uint32_t i = 0; i <= size; i++) {
        if (i == size || g_log_buf[i] == '\n' || g_log_buf[i] == '\r') {
            uint32_t len = i - line_start;
            const char* line = (const char*)g_log_buf + line_start;
            uint8_t ok = 1u;

            if (len > 0u) {
                if (has_since || has_until) {
                    uint64_t tick = 0u;
                    if (parse_line_tick(line, len, &tick) != 0) {
                        ok = 0u;
                    } else {
                        if (has_since && tick < since_tick) {
                            ok = 0u;
                        }
                        if (has_until && tick > until_tick) {
                            ok = 0u;
                        }
                    }
                }

                if (ok && contains && contains[0] && !line_contains(line, len, contains)) {
                    ok = 0u;
                }
                if (ok && has_service && !line_contains(line, len, service_marker)) {
                    ok = 0u;
                }

                if (ok) {
                    print_line(line, len);
                    if (has_export) {
                        if (export_len + len + 1u <= sizeof(g_export_buf)) {
                            for (uint32_t j = 0; j < len; j++) {
                                g_export_buf[export_len++] = (uint8_t)line[j];
                            }
                            g_export_buf[export_len++] = (uint8_t)'\n';
                        } else {
                            export_truncated = 1u;
                        }
                    }
                    shown++;
                }
            }
            line_start = i + 1u;
        }
    }

    if (shown == 0u) {
        mya_putln("(no matching log entries)");
    }

    if (has_export) {
        if (mya_fs_write(export_path, g_export_buf, export_len) != 0) {
            mya_puts("log: failed to export to ");
            mya_putln(export_path);
            return 1;
        }
        mya_puts("log: exported ");
        mya_put_u32(shown);
        mya_puts(" line(s) to ");
        mya_putln(export_path);
        if (export_truncated) {
            mya_putln("log: warning: export truncated by buffer limit");
        }
    }
    return 0;
}
