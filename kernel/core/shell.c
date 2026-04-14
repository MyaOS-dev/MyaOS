#include "shell.h"
#include "console.h"
#include "graphics.h"
#include "heap.h"
#include "keyboard.h"
#include "scheduler.h"
#include "timer.h"
#include "vfs.h"
#include <myaos/syscall.h>
#include <stdint.h>

#define SHELL_INPUT_MAX 256
#define SHELL_ARGV_MAX 16
#define SHELL_TOK_MAX 64
#define SHELL_MANIFEST_MAX 256
#define SHELL_AUTORUN_MAX 4096
#define SHELL_MANIFEST_CACHE_MAX 64
#define SHELL_HELP_LIST_MAX 128
#define SHELL_HELP_SEEN_MAX 128
#define SHELL_HELP_TEXT_MAX (64u * 1024u)
#define SHELL_REDIRECT_MAX_FILE (256u * 1024u)
#define SHELL_CURSOR_BLINK_TICKS 20u
#define SHELL_HISTORY_MAX 64u
#define SHELL_HISTORY_FILE_MAX (64u * 1024u)
#define SHELL_HISTORY_PATH "/var/log/shell_history.log"

typedef struct {
    uint8_t used;
    char name[MYAOS_NAME_MAX];
    char exec_path[MYAOS_PATH_MAX];
    char desc[MYAOS_DESC_MAX];
} shell_manifest_cache_entry_t;

static char g_input[SHELL_INPUT_MAX];
static uint32_t g_input_len;
static uint32_t g_input_cursor;
static char g_last_command[SHELL_INPUT_MAX];
static uint8_t g_has_last_command;
static char g_history[SHELL_HISTORY_MAX][SHELL_INPUT_MAX];
static uint32_t g_history_start;
static uint32_t g_history_count;
static int32_t g_history_nav;
static char g_history_saved_input[SHELL_INPUT_MAX];
static uint8_t g_history_saved_valid;
static uint8_t g_prompt_shown;
static uint32_t g_pipe_seq;
static uint8_t g_debug_mode;
static uint8_t g_autorun_done;
static uint8_t g_recovery_mode;
static uint32_t g_rendered_input_len;
static uint8_t g_cursor_visible;
static uint64_t g_cursor_last_blink_tick;
static uint32_t g_prompt_col;
static uint32_t g_prompt_row;
static uint8_t g_cursor_overlay_active;
static uint32_t g_cursor_overlay_col;
static uint32_t g_cursor_overlay_row;
static uint32_t g_cursor_overlay_index;
static int32_t g_last_status;
static uint8_t g_verbose_mode;
static shell_manifest_cache_entry_t g_manifest_cache[SHELL_MANIFEST_CACHE_MAX];

static int64_t syscall_invoke(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
) {
    uint64_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4)
        : "cc", "memory"
    );
    return (int64_t)ret;
}

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

    if (dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

static char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int str_eq_ci(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (to_lower_char(a[i]) != to_lower_char(b[i])) {
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

static int str_ends_with(const char* text, const char* suffix) {
    size_t text_len = str_len(text);
    size_t suffix_len = str_len(suffix);

    if (suffix_len > text_len) {
        return 0;
    }
    return str_eq(text + (text_len - suffix_len), suffix);
}

static int str_ends_with_ci(const char* text, const char* suffix) {
    size_t text_len = str_len(text);
    size_t suffix_len = str_len(suffix);

    if (suffix_len > text_len) {
        return 0;
    }
    return str_eq_ci(text + (text_len - suffix_len), suffix);
}

static int str_contains_char(const char* text, char c) {
    size_t i = 0;
    while (text && text[i]) {
        if (text[i] == c) {
            return 1;
        }
        i++;
    }
    return 0;
}

static int str_contains(const char* text, const char* needle) {
    size_t text_len = str_len(text);
    size_t needle_len = str_len(needle);

    if (!needle || !needle[0]) {
        return 1;
    }
    if (!text || needle_len > text_len) {
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

static int str_starts_with_ci(const char* text, const char* prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (to_lower_char(text[i]) != to_lower_char(prefix[i])) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void to_lower(char* text) {
    for (size_t i = 0; text[i]; i++) {
        if (text[i] >= 'A' && text[i] <= 'Z') {
            text[i] = (char)(text[i] - 'A' + 'a');
        }
    }
}

static void trim_in_place(char* text) {
    size_t begin = 0;
    size_t end = str_len(text);
    size_t out = 0;

    while (text[begin] == ' ' || text[begin] == '\t') {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        end--;
    }

    for (size_t i = begin; i < end; i++) {
        text[out++] = text[i];
    }
    text[out] = '\0';
}

static uint32_t shell_utf8_char_len_at(const char* text, uint32_t len, uint32_t pos) {
    uint8_t b0;

    if (!text || pos >= len) {
        return 0u;
    }

    b0 = (uint8_t)text[pos];
    if (b0 < 0x80u) {
        return 1u;
    }

    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (pos + 1u < len && (((uint8_t)text[pos + 1u] & 0xC0u) == 0x80u)) {
            return 2u;
        }
        return 1u;
    }

    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        uint8_t b1;
        uint8_t b2;
        if (pos + 2u >= len) {
            return 1u;
        }
        b1 = (uint8_t)text[pos + 1u];
        b2 = (uint8_t)text[pos + 2u];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return 1u;
        }
        if ((b0 == 0xE0u && b1 < 0xA0u) || (b0 == 0xEDu && b1 >= 0xA0u)) {
            return 1u;
        }
        return 3u;
    }

    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
        if (pos + 3u >= len) {
            return 1u;
        }
        b1 = (uint8_t)text[pos + 1u];
        b2 = (uint8_t)text[pos + 2u];
        b3 = (uint8_t)text[pos + 3u];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
            return 1u;
        }
        if ((b0 == 0xF0u && b1 < 0x90u) || (b0 == 0xF4u && b1 > 0x8Fu)) {
            return 1u;
        }
        return 4u;
    }

    return 1u;
}

static uint32_t shell_utf8_prev_start(const char* text, uint32_t pos) {
    if (!text || pos == 0u) {
        return 0u;
    }

    pos--;
    while (pos > 0u && (((uint8_t)text[pos] & 0xC0u) == 0x80u)) {
        pos--;
    }
    return pos;
}

static uint32_t shell_utf8_next_start(const char* text, uint32_t len, uint32_t pos) {
    uint32_t step = shell_utf8_char_len_at(text, len, pos);
    if (step == 0u) {
        return len;
    }
    pos += step;
    if (pos > len) {
        pos = len;
    }
    return pos;
}

static uint32_t shell_utf8_cell_count(const char* text, uint32_t bytes) {
    uint32_t i = 0u;
    uint32_t cells = 0u;

    while (i < bytes) {
        uint32_t step = shell_utf8_char_len_at(text, bytes, i);
        if (step == 0u) {
            break;
        }
        i += step;
        cells++;
    }
    return cells;
}

typedef struct {
    char* argv[SHELL_ARGV_MAX];
    int argc;
    uint8_t background;
    uint8_t out_append;
    char out_path[MYAOS_PATH_MAX];
} shell_cmd_t;

typedef enum {
    SHELL_EXEC_PROBE_OK = 0,
    SHELL_EXEC_PROBE_INVALID_PATH = 1,
    SHELL_EXEC_PROBE_IS_DIR = 2,
    SHELL_EXEC_PROBE_NO_EXEC = 3,
    SHELL_EXEC_PROBE_NOT_FOUND = 4,
} shell_exec_probe_t;

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int is_special(char c) {
    return c == ';' || c == '&' || c == '|' || c == '>';
}

static void u32_to_dec(uint32_t value, char* out, size_t out_size) {
    char rev[16];
    size_t n = 0;
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static void i32_to_dec(int32_t value, char* out, size_t out_size) {
    if (!out || out_size == 0u) {
        return;
    }
    if (value < 0) {
        uint32_t mag = (uint32_t)(~(uint32_t)value) + 1u;
        if (out_size < 3u) {
            out[0] = '\0';
            return;
        }
        out[0] = '-';
        u32_to_dec(mag, out + 1, out_size - 1u);
    } else {
        u32_to_dec((uint32_t)value, out, out_size);
    }
}

static int append_text(char* out, size_t out_size, size_t* io_pos, const char* text) {
    size_t pos;

    if (!out || !io_pos || !text || out_size == 0u) {
        return -1;
    }

    pos = *io_pos;
    for (size_t i = 0; text[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = text[i];
    }
    out[pos] = '\0';
    *io_pos = pos;
    return 0;
}

static int shell_output_blob(const shell_cmd_t* cmd, const uint8_t* data, uint32_t size) {
    uint8_t* old_buf;
    uint8_t* merged;
    uint32_t old_size = 0;
    int rc;

    if (!cmd || !data) {
        return -1;
    }
    if (!cmd->out_path[0]) {
        console_write_len((const char*)data, (size_t)size);
        return 0;
    }
    if (!cmd->out_append) {
        return vfs_write_file(scheduler_current_cwd(), cmd->out_path, data, size);
    }

    old_buf = (uint8_t*)kmalloc(SHELL_REDIRECT_MAX_FILE);
    if (!old_buf) {
        return -1;
    }

    if (vfs_read_file(scheduler_current_cwd(), cmd->out_path, old_buf, SHELL_REDIRECT_MAX_FILE, &old_size) != 0) {
        old_size = 0;
    }
    if (old_size > SHELL_REDIRECT_MAX_FILE || size > SHELL_REDIRECT_MAX_FILE || old_size + size > SHELL_REDIRECT_MAX_FILE) {
        kfree(old_buf);
        return -1;
    }

    merged = (uint8_t*)kmalloc((size_t)old_size + (size_t)size + 1u);
    if (!merged) {
        kfree(old_buf);
        return -1;
    }

    for (uint32_t i = 0; i < old_size; i++) {
        merged[i] = old_buf[i];
    }
    for (uint32_t i = 0; i < size; i++) {
        merged[old_size + i] = data[i];
    }

    rc = vfs_write_file(scheduler_current_cwd(), cmd->out_path, merged, old_size + size);
    kfree(merged);
    kfree(old_buf);
    return rc;
}

static int tokenize_line(char* line, char** tokens, int max_tokens) {
    char* p = line;
    int count = 0;

    while (*p) {
        while (is_space(*p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        if (is_special(*p)) {
            if (count >= max_tokens) {
                return -1;
            }
            if (*p == '&' && p[1] == '&') {
                tokens[count++] = "&&";
                p += 2;
            } else if (*p == '|' && p[1] == '|') {
                tokens[count++] = "||";
                p += 2;
            } else if (*p == '>' && p[1] == '>') {
                tokens[count++] = ">>";
                p += 2;
            } else if (*p == '&') {
                tokens[count++] = "&";
                p++;
            } else if (*p == '|') {
                tokens[count++] = "|";
                p++;
            } else if (*p == ';') {
                tokens[count++] = ";";
                p++;
            } else {
                tokens[count++] = ">";
                p++;
            }
            continue;
        }

        {
            char* read = p;
            char* write = p;
            char quote = 0;
            char endc = 0;
            char endc_next = 0;

            while (*read) {
                char c = *read;

                if (quote) {
                    if (c == quote) {
                        quote = 0;
                        read++;
                        continue;
                    }
                    if (c == '\\' && quote == '"' && read[1]) {
                        read++;
                        c = *read;
                    }
                    *write++ = c;
                    read++;
                    continue;
                }

                if (c == '"' || c == '\'') {
                    quote = c;
                    read++;
                    continue;
                }
                if (c == '\\' && read[1]) {
                    read++;
                    *write++ = *read++;
                    continue;
                }
                if (is_space(c) || is_special(c)) {
                    break;
                }

                *write++ = c;
                read++;
            }

            if (quote) {
                return -1;
            }
            endc = *read;
            endc_next = (endc != '\0') ? read[1] : '\0';
            *write = '\0';
            if (count >= max_tokens) {
                return -1;
            }
            tokens[count++] = p;

            if (endc == '\0') {
                p = read;
                continue;
            }
            if (is_space(endc)) {
                p = read + 1;
                continue;
            }

            if (count >= max_tokens) {
                return -1;
            }
            if (endc == '&' && endc_next == '&') {
                tokens[count++] = "&&";
                p = read + 2;
            } else if (endc == '|' && endc_next == '|') {
                tokens[count++] = "||";
                p = read + 2;
            } else if (endc == '>' && endc_next == '>') {
                tokens[count++] = ">>";
                p = read + 2;
            } else if (endc == '&') {
                tokens[count++] = "&";
                p = read + 1;
            } else if (endc == '|') {
                tokens[count++] = "|";
                p = read + 1;
            } else if (endc == ';') {
                tokens[count++] = ";";
                p = read + 1;
            } else if (endc == '>') {
                tokens[count++] = ">";
                p = read + 1;
            } else {
                p = read;
            }
        }
    }

    return count;
}

static int manifest_cache_lookup(const char* cmd, char* exec_path, size_t exec_size, char* desc, size_t desc_size) {
    for (size_t i = 0; i < SHELL_MANIFEST_CACHE_MAX; i++) {
        if (!g_manifest_cache[i].used || !str_eq(g_manifest_cache[i].name, cmd)) {
            continue;
        }
        str_copy(exec_path, g_manifest_cache[i].exec_path, exec_size);
        str_copy(desc, g_manifest_cache[i].desc, desc_size);
        return 0;
    }
    return -1;
}

static void manifest_cache_store(const char* cmd, const char* exec_path, const char* desc) {
    for (size_t i = 0; i < SHELL_MANIFEST_CACHE_MAX; i++) {
        if (!g_manifest_cache[i].used || str_eq(g_manifest_cache[i].name, cmd)) {
            g_manifest_cache[i].used = 1u;
            str_copy(g_manifest_cache[i].name, cmd, sizeof(g_manifest_cache[i].name));
            str_copy(g_manifest_cache[i].exec_path, exec_path, sizeof(g_manifest_cache[i].exec_path));
            str_copy(g_manifest_cache[i].desc, desc, sizeof(g_manifest_cache[i].desc));
            return;
        }
    }
}

static int shell_help_name_seen(char names[][MYAOS_NAME_MAX], size_t count, const char* name) {
    for (size_t i = 0; i < count; i++) {
        if (str_eq(names[i], name)) {
            return 1;
        }
    }
    return 0;
}

static void shell_help_mark_name(char names[][MYAOS_NAME_MAX], size_t* count, const char* name) {
    if (!names || !count || !name || !name[0]) {
        return;
    }
    if (*count >= SHELL_HELP_SEEN_MAX) {
        return;
    }
    if (shell_help_name_seen(names, *count, name)) {
        return;
    }

    str_copy(names[*count], name, MYAOS_NAME_MAX);
    (*count)++;
}

static int build_child_path(const char* base_dir, const char* file_name, char* out_path, size_t out_size) {
    size_t pos = 0;

    if (!base_dir || !file_name || !out_path || out_size == 0) {
        return -1;
    }

    while (base_dir[pos] && pos + 1 < out_size) {
        out_path[pos] = base_dir[pos];
        pos++;
    }
    if (base_dir[pos] != '\0') {
        return -1;
    }

    if (pos == 0 || out_path[pos - 1] != '/') {
        if (pos + 1 >= out_size) {
            return -1;
        }
        out_path[pos++] = '/';
    }

    for (size_t i = 0; file_name[i]; i++) {
        if (pos + 1 >= out_size) {
            return -1;
        }
        out_path[pos++] = file_name[i];
    }
    out_path[pos] = '\0';
    return 0;
}

static int build_manifest_path(const char* base_dir, const char* cmd, char* out_path, size_t out_size) {
    size_t pos = 0;

    if (!base_dir || !cmd || !out_path || out_size == 0) {
        return -1;
    }

    while (base_dir[pos] && pos + 1 < out_size) {
        out_path[pos] = base_dir[pos];
        pos++;
    }
    if (base_dir[pos] != '\0') {
        return -1;
    }

    if (pos == 0 || out_path[pos - 1] != '/') {
        if (pos + 1 >= out_size) {
            return -1;
        }
        out_path[pos++] = '/';
    }

    for (size_t i = 0; cmd[i]; i++) {
        if (pos + 1 >= out_size) {
            return -1;
        }
        out_path[pos++] = cmd[i];
    }
    if (pos + 4 >= out_size) {
        return -1;
    }
    out_path[pos++] = '.';
    out_path[pos++] = 'c';
    out_path[pos++] = 'm';
    out_path[pos++] = 'd';
    out_path[pos] = '\0';
    return 0;
}

static int extract_command_name(const char* entry_name, const char* suffix, char* out_name, size_t out_size) {
    size_t name_len;
    size_t suffix_len;

    if (!entry_name || !suffix || !out_name || out_size == 0) {
        return -1;
    }

    str_copy(out_name, entry_name, out_size);
    to_lower(out_name);
    name_len = str_len(out_name);
    suffix_len = str_len(suffix);
    if (name_len <= suffix_len) {
        return -1;
    }
    if (!str_ends_with(out_name, suffix)) {
        return -1;
    }

    out_name[name_len - suffix_len] = '\0';
    return out_name[0] ? 0 : -1;
}

static int read_manifest_from_dir(
    const char* manifest_dir,
    const char* cmd,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
) {
    char manifest_path[MYAOS_PATH_MAX];
    myaos_dirent_t entries[SHELL_HELP_LIST_MAX];
    size_t count = 0;

    if (!manifest_dir || !cmd || !out_buf || !out_size || out_buf_size == 0u) {
        return -1;
    }

    if (build_manifest_path(manifest_dir, cmd, manifest_path, sizeof(manifest_path)) == 0 &&
        vfs_read_file(scheduler_current_cwd(), manifest_path, out_buf, out_buf_size, out_size) == 0) {
        return 0;
    }

    if (vfs_list(scheduler_current_cwd(), manifest_dir, entries, SHELL_HELP_LIST_MAX, &count) != 0) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        char entry_cmd[MYAOS_NAME_MAX];

        if (entries[i].type != MYAOS_NODE_FILE) {
            continue;
        }
        if (!str_ends_with_ci(entries[i].name, ".cmd")) {
            continue;
        }
        if (extract_command_name(entries[i].name, ".cmd", entry_cmd, sizeof(entry_cmd)) != 0) {
            continue;
        }
        if (!str_eq_ci(entry_cmd, cmd)) {
            continue;
        }
        if (build_child_path(manifest_dir, entries[i].name, manifest_path, sizeof(manifest_path)) != 0) {
            continue;
        }
        if (vfs_read_file(scheduler_current_cwd(), manifest_path, out_buf, out_buf_size, out_size) == 0) {
            return 0;
        }
    }

    return -1;
}

static int read_command_manifest(const char* cmd, char* exec_path, size_t exec_size, char* desc, size_t desc_size) {
    char cmd_key[MYAOS_NAME_MAX];
    uint8_t buf[SHELL_MANIFEST_MAX];
    uint32_t read_size = 0;
    size_t i = 0;
    size_t pos = 0;

    str_copy(cmd_key, cmd, sizeof(cmd_key));
    to_lower(cmd_key);

    if (manifest_cache_lookup(cmd_key, exec_path, exec_size, desc, desc_size) == 0) {
        return 0;
    }

    if (read_manifest_from_dir("/cmd", cmd_key, buf, sizeof(buf) - 1u, &read_size) != 0 &&
        read_manifest_from_dir("/boot/cmd", cmd_key, buf, sizeof(buf) - 1u, &read_size) != 0) {
        return -1;
    }
    buf[read_size] = 0;

    while (i < read_size && buf[i] != '\n' && pos + 1 < exec_size) {
        exec_path[pos++] = (char)buf[i++];
    }
    exec_path[pos] = '\0';
    if (buf[i] == '\n') {
        i++;
    }

    pos = 0;
    while (i < read_size && buf[i] != '\n' && pos + 1 < desc_size) {
        desc[pos++] = (char)buf[i++];
    }
    desc[pos] = '\0';

    if (str_starts_with(exec_path, "exec=")) {
        str_copy(exec_path, exec_path + 5, exec_size);
    }
    if (str_starts_with(desc, "desc=")) {
        str_copy(desc, desc + 5, desc_size);
    }
    manifest_cache_store(cmd_key, exec_path, desc);
    return exec_path[0] ? 0 : -1;
}

static int shell_spawn_command(const shell_cmd_t* cmd, int32_t* out_exit_code);
static int make_pipe_temp_path(char* out, size_t out_size);

static int shell_help(const shell_cmd_t* cmd) {
    myaos_dirent_t entries[SHELL_HELP_LIST_MAX];
    char shown_names[SHELL_HELP_SEEN_MAX][MYAOS_NAME_MAX];
    static char out_text[SHELL_HELP_TEXT_MAX];
    size_t count = 0;
    size_t shown_count = 0;
    size_t out_pos = 0;
    const char* manifest_dir = "/cmd";
    const char* elf_dirs[] = {"/bin", "/boot/bin"};
    const size_t elf_dir_count = sizeof(elf_dirs) / sizeof(elf_dirs[0]);
    uint8_t manifest_listed = 0u;
    uint8_t elf_listed = 0u;
    uint8_t truncated = 0u;
    const char* pattern = NULL;
    uint8_t use_pager = 0u;

    if (cmd) {
        for (int ai = 1; ai < cmd->argc; ai++) {
            if (str_eq(cmd->argv[ai], "--pager") || str_eq(cmd->argv[ai], "-p")) {
                use_pager = 1u;
            } else if (!pattern) {
                pattern = cmd->argv[ai];
            }
        }
    }

#define HELP_APPEND_TEXT(text_literal)                                                        \
    do {                                                                                      \
        if (!truncated && append_text(out_text, sizeof(out_text), &out_pos, (text_literal)) != 0) { \
            truncated = 1u;                                                                   \
        }                                                                                     \
    } while (0)

    out_text[0] = '\0';
    if (!pattern || str_contains("help", pattern) || str_contains("show commands", pattern)) {
        HELP_APPEND_TEXT("help - show commands\n");
    }
    if (!pattern || str_contains("history", pattern)) {
        HELP_APPEND_TEXT("history - show command history [pattern]\n");
    }
    if (!pattern || str_contains("cd", pattern)) {
        HELP_APPEND_TEXT("cd - change shell directory\n");
    }
    if (!pattern || str_contains("debug", pattern)) {
        HELP_APPEND_TEXT("debug - shell debug [on|off|toggle]\n");
    }
    if (!pattern || str_contains("verbose", pattern)) {
        HELP_APPEND_TEXT("verbose - print command exit code details [on|off|toggle]\n");
    }
    if (!pattern || str_contains("autorun", pattern)) {
        HELP_APPEND_TEXT("autorun - run /autorun.sh, /boot/autorun.sh, or /ram/autorun.sh\n");
    }
    if (!pattern || str_contains("load", pattern)) {
        HELP_APPEND_TEXT("load - run ELF directly: load <path.elf> [args...]\n");
    }
    if (!pattern || str_contains("script", pattern) || str_contains(".sh", pattern)) {
        HELP_APPEND_TEXT("./name.sh - execute script from current directory\n");
    }

    if (vfs_list(scheduler_current_cwd(), manifest_dir, entries, SHELL_HELP_LIST_MAX, &count) != 0 || count == 0u) {
        manifest_dir = "/boot/cmd";
    }
    if (vfs_list(scheduler_current_cwd(), manifest_dir, entries, SHELL_HELP_LIST_MAX, &count) == 0) {
        manifest_listed = 1u;
        for (size_t i = 0; i < count; i++) {
            char name[MYAOS_NAME_MAX];
            char exec_path[MYAOS_PATH_MAX];
            char desc[MYAOS_DESC_MAX];

            if (entries[i].type != MYAOS_NODE_FILE) {
                continue;
            }
            if (extract_command_name(entries[i].name, ".cmd", name, sizeof(name)) != 0) {
                continue;
            }
            if (shell_help_name_seen(shown_names, shown_count, name)) {
                continue;
            }
            if (read_command_manifest(name, exec_path, sizeof(exec_path), desc, sizeof(desc)) != 0) {
                continue;
            }
            if (pattern && pattern[0] &&
                !str_contains(name, pattern) &&
                !str_contains(desc, pattern) &&
                !str_contains(exec_path, pattern)) {
                continue;
            }

            HELP_APPEND_TEXT(name);
            HELP_APPEND_TEXT(" - ");
            HELP_APPEND_TEXT(desc[0] ? desc : "(no description)");
            HELP_APPEND_TEXT("\n");
            shell_help_mark_name(shown_names, &shown_count, name);
        }
    }

    for (size_t d = 0; d < elf_dir_count; d++) {
        const char* elf_dir = elf_dirs[d];

        if (vfs_list(scheduler_current_cwd(), elf_dir, entries, SHELL_HELP_LIST_MAX, &count) != 0) {
            continue;
        }
        for (size_t i = 0; i < count; i++) {
            char name[MYAOS_NAME_MAX];
            char exec_path[MYAOS_PATH_MAX];
            char desc[MYAOS_DESC_MAX];

            if (entries[i].type != MYAOS_NODE_FILE) {
                continue;
            }
            if (extract_command_name(entries[i].name, ".elf", name, sizeof(name)) != 0) {
                continue;
            }
            if (shell_help_name_seen(shown_names, shown_count, name)) {
                continue;
            }
            if (manifest_listed &&
                read_command_manifest(name, exec_path, sizeof(exec_path), desc, sizeof(desc)) == 0) {
                continue;
            }
            if (build_child_path(elf_dir, entries[i].name, exec_path, sizeof(exec_path)) != 0) {
                continue;
            }
            if (pattern && pattern[0] &&
                !str_contains(name, pattern) &&
                !str_contains(exec_path, pattern)) {
                continue;
            }

            HELP_APPEND_TEXT(name);
            HELP_APPEND_TEXT(" - ELF program (use: load ");
            HELP_APPEND_TEXT(exec_path);
            HELP_APPEND_TEXT(")\n");
            shell_help_mark_name(shown_names, &shown_count, name);
            elf_listed = 1u;
        }
    }

    if (!manifest_listed && !elf_listed) {
        HELP_APPEND_TEXT("no command manifests or ELF programs found (/cmd, /boot/cmd, /bin, /boot/bin)\n");
    }
    if (truncated) {
        if (out_pos + 40u < sizeof(out_text)) {
            (void)append_text(out_text, sizeof(out_text), &out_pos, "\n[help output truncated]\n");
        } else if (out_pos + 2u < sizeof(out_text)) {
            out_text[out_pos++] = '\n';
            out_text[out_pos] = '\0';
        }
    }

#undef HELP_APPEND_TEXT

    if (use_pager && (!cmd || !cmd->out_path[0])) {
        char tmp_path[MYAOS_PATH_MAX];
        shell_cmd_t pager_cmd;
        char* pager_argv[2];
        int32_t pager_exit = 0;

        if (make_pipe_temp_path(tmp_path, sizeof(tmp_path)) == 0 &&
            vfs_write_file("/", tmp_path, (const uint8_t*)out_text, (uint32_t)out_pos) == 0) {
            pager_cmd.argc = 2;
            pager_cmd.background = 0u;
            pager_cmd.out_append = 0u;
            pager_cmd.out_path[0] = '\0';
            pager_argv[0] = "less";
            pager_argv[1] = tmp_path;
            pager_cmd.argv[0] = pager_argv[0];
            pager_cmd.argv[1] = pager_argv[1];
            (void)shell_spawn_command(&pager_cmd, &pager_exit);
            (void)vfs_remove("/", tmp_path);
            return pager_exit;
        }
        console_write("help: pager fallback failed\n");
    }

    if (shell_output_blob(cmd, (const uint8_t*)out_text, (uint32_t)out_pos) != 0) {
        console_write("help: write failed\n");
        if (cmd && cmd->out_path[0]) {
            console_write("path: ");
            console_write(cmd->out_path);
            console_put_char('\n');
        }
        return 1;
    }
    return 0;
}

static void shell_render_input(uint8_t show_cursor);
static void shell_cursor_activity(void);
static void shell_restore_cursor_overlay(void);

static uint32_t shell_history_index(uint32_t pos) {
    if (g_history_count == 0u) {
        return 0u;
    }
    return (g_history_start + pos) % SHELL_HISTORY_MAX;
}

static const char* shell_history_get(uint32_t pos) {
    if (pos >= g_history_count) {
        return NULL;
    }
    return g_history[shell_history_index(pos)];
}

static void shell_history_add_runtime(const char* line) {
    uint32_t index;

    if (!line || !line[0]) {
        return;
    }

    if (g_history_count > 0u) {
        const char* last = shell_history_get(g_history_count - 1u);
        if (last && str_eq(last, line)) {
            return;
        }
    }

    if (g_history_count < SHELL_HISTORY_MAX) {
        index = shell_history_index(g_history_count);
        g_history_count++;
    } else {
        index = g_history_start;
        g_history_start = (g_history_start + 1u) % SHELL_HISTORY_MAX;
    }

    str_copy(g_history[index], line, sizeof(g_history[index]));
}

static void shell_history_save_persistent(const char* line) {
    uint8_t* buf;
    uint32_t size = 0u;
    uint32_t line_len = (uint32_t)str_len(line);
    uint32_t append_len;

    if (!line || !line[0]) {
        return;
    }

    buf = (uint8_t*)kmalloc(SHELL_HISTORY_FILE_MAX);
    if (!buf) {
        return;
    }

    if (vfs_read_file("/", SHELL_HISTORY_PATH, buf, SHELL_HISTORY_FILE_MAX - 1u, &size) != 0) {
        size = 0u;
    }

    append_len = line_len + 1u;
    if (size + append_len >= SHELL_HISTORY_FILE_MAX) {
        uint32_t drop = (size + append_len) - (SHELL_HISTORY_FILE_MAX - 1u);
        uint32_t keep_from = drop;

        while (keep_from < size && buf[keep_from] != '\n') {
            keep_from++;
        }
        if (keep_from < size && buf[keep_from] == '\n') {
            keep_from++;
        }

        if (keep_from >= size) {
            size = 0u;
        } else {
            uint32_t remain = size - keep_from;
            for (uint32_t i = 0; i < remain; i++) {
                buf[i] = buf[keep_from + i];
            }
            size = remain;
        }
    }

    for (uint32_t i = 0; i < line_len && size + 1u < SHELL_HISTORY_FILE_MAX; i++) {
        buf[size++] = (uint8_t)line[i];
    }
    if (size + 1u < SHELL_HISTORY_FILE_MAX) {
        buf[size++] = '\n';
    }
    buf[size] = 0u;

    (void)vfs_mkdir("/", "/var");
    (void)vfs_mkdir("/", "/var/log");
    (void)vfs_write_file("/", SHELL_HISTORY_PATH, buf, size);
    kfree(buf);
}

static void shell_history_load_persistent(void) {
    uint8_t* buf;
    uint32_t size = 0u;
    uint32_t line_start = 0u;

    buf = (uint8_t*)kmalloc(SHELL_HISTORY_FILE_MAX);
    if (!buf) {
        return;
    }
    if (vfs_read_file("/", SHELL_HISTORY_PATH, buf, SHELL_HISTORY_FILE_MAX - 1u, &size) != 0) {
        kfree(buf);
        return;
    }
    buf[size] = 0u;

    for (uint32_t i = 0; i <= size; i++) {
        if (i == size || buf[i] == '\n' || buf[i] == '\r') {
            char line[SHELL_INPUT_MAX];
            uint32_t out = 0u;

            for (uint32_t j = line_start; j < i && out + 1u < sizeof(line); j++) {
                line[out++] = (char)buf[j];
            }
            line[out] = '\0';
            trim_in_place(line);
            if (line[0]) {
                shell_history_add_runtime(line);
            }
            line_start = i + 1u;
        }
    }
    kfree(buf);
}

static void shell_history_commit(const char* line) {
    if (!line || !line[0]) {
        return;
    }
    shell_history_add_runtime(line);
    shell_history_save_persistent(line);
}

static int shell_history_builtin(const shell_cmd_t* cmd) {
    static char out[SHELL_HELP_TEXT_MAX];
    size_t pos = 0;
    const char* pattern = NULL;
    uint32_t shown = 0u;

    if (cmd && cmd->argc >= 2) {
        pattern = cmd->argv[1];
    }

    out[0] = '\0';
    for (uint32_t i = 0; i < g_history_count; i++) {
        char num[16];
        const char* line = shell_history_get(i);

        if (!line) {
            continue;
        }
        if (pattern && pattern[0] && !str_contains(line, pattern)) {
            continue;
        }

        u32_to_dec(i + 1u, num, sizeof(num));
        if (append_text(out, sizeof(out), &pos, num) != 0 ||
            append_text(out, sizeof(out), &pos, " ") != 0 ||
            append_text(out, sizeof(out), &pos, line) != 0 ||
            append_text(out, sizeof(out), &pos, "\n") != 0) {
            break;
        }
        shown++;
    }

    if (shown == 0u) {
        (void)append_text(out, sizeof(out), &pos, "(empty)\n");
    }

    return shell_output_blob(cmd, (const uint8_t*)out, (uint32_t)pos);
}

static uint8_t shell_has_token_separator(const char* text, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == ';' || text[i] == '|' || text[i] == '&') {
            return 1u;
        }
    }
    return 0u;
}

static int shell_complete_collect(
    const char* prefix,
    char matches[][MYAOS_NAME_MAX],
    size_t max_matches,
    size_t* out_count
) {
    myaos_dirent_t entries[SHELL_HELP_LIST_MAX];
    char seen[SHELL_HELP_SEEN_MAX][MYAOS_NAME_MAX];
    size_t seen_count = 0;
    size_t match_count = 0;
    size_t count = 0;
    const char* manifest_dir = "/cmd";
    const char* elf_dirs[] = {"/bin", "/boot/bin"};
    const size_t elf_dir_count = sizeof(elf_dirs) / sizeof(elf_dirs[0]);

    if (!prefix || !matches || !out_count) {
        return -1;
    }
    *out_count = 0u;

    if (vfs_list(scheduler_current_cwd(), manifest_dir, entries, SHELL_HELP_LIST_MAX, &count) != 0 || count == 0u) {
        manifest_dir = "/boot/cmd";
    }
    if (vfs_list(scheduler_current_cwd(), manifest_dir, entries, SHELL_HELP_LIST_MAX, &count) == 0) {
        for (size_t i = 0; i < count; i++) {
            char name[MYAOS_NAME_MAX];

            if (entries[i].type != MYAOS_NODE_FILE) {
                continue;
            }
            if (extract_command_name(entries[i].name, ".cmd", name, sizeof(name)) != 0) {
                continue;
            }
            if (!str_starts_with_ci(name, prefix)) {
                continue;
            }
            if (shell_help_name_seen(seen, seen_count, name)) {
                continue;
            }
            shell_help_mark_name(seen, &seen_count, name);
            if (match_count < max_matches) {
                str_copy(matches[match_count], name, MYAOS_NAME_MAX);
                match_count++;
            }
        }
    }

    for (size_t d = 0; d < elf_dir_count; d++) {
        const char* elf_dir = elf_dirs[d];

        if (vfs_list(scheduler_current_cwd(), elf_dir, entries, SHELL_HELP_LIST_MAX, &count) != 0) {
            continue;
        }
        for (size_t i = 0; i < count; i++) {
            char name[MYAOS_NAME_MAX];

            if (entries[i].type != MYAOS_NODE_FILE) {
                continue;
            }
            if (extract_command_name(entries[i].name, ".elf", name, sizeof(name)) != 0) {
                continue;
            }
            if (!str_starts_with_ci(name, prefix)) {
                continue;
            }
            if (shell_help_name_seen(seen, seen_count, name)) {
                continue;
            }
            shell_help_mark_name(seen, &seen_count, name);
            if (match_count < max_matches) {
                str_copy(matches[match_count], name, MYAOS_NAME_MAX);
                match_count++;
            }
        }
    }

    *out_count = match_count;
    return 0;
}

static void shell_try_complete_command(void) {
    char prefix[MYAOS_NAME_MAX];
    char matches[32][MYAOS_NAME_MAX];
    size_t match_count = 0;

    if (g_input_cursor != g_input_len || g_input_len == 0u) {
        return;
    }
    if (g_input_len >= sizeof(prefix)) {
        return;
    }
    if (shell_has_token_separator(g_input, g_input_len)) {
        return;
    }

    str_copy(prefix, g_input, sizeof(prefix));
    if (shell_complete_collect(prefix, matches, 32u, &match_count) != 0 || match_count == 0u) {
        return;
    }

    if (match_count == 1u) {
        str_copy(g_input, matches[0], sizeof(g_input));
        g_input_len = (uint32_t)str_len(g_input);
        g_input_cursor = g_input_len;
        shell_cursor_activity();
        shell_render_input(1u);
        return;
    }

    shell_restore_cursor_overlay();
    console_put_char('\n');
    for (size_t i = 0; i < match_count; i++) {
        console_write(matches[i]);
        console_put_char('\n');
    }
    g_prompt_shown = 0u;
}

static int shell_cd(int argc, char* const* argv) {
    char abs_path[MYAOS_PATH_MAX];
    int is_dir_rc;

    if (argc < 2) {
        console_write(scheduler_current_cwd());
        console_put_char('\n');
        return 0;
    }
    if (vfs_resolve_cwd(scheduler_current_cwd(), argv[1], abs_path, sizeof(abs_path)) != 0) {
        console_write("error: ");
        console_write(argv[1]);
        console_write(": invalid path\n");
        console_write("hint: use an absolute path like /dir or a valid relative path\n");
        return 1;
    }
    is_dir_rc = vfs_is_dir("/", abs_path);
    if (is_dir_rc <= 0 || scheduler_setcwd_current(abs_path) != 0) {
        console_write("error: ");
        console_write(abs_path);
        if (is_dir_rc == 0) {
            console_write(": not a directory\n");
        } else {
            console_write(": directory not found or access denied\n");
        }
        console_write("hint: verify the path with `ls` and directory permissions\n");
        return 1;
    }
    return 0;
}

static int32_t shell_execute(const char* raw_line);

static void shell_debug_write(const char* text) {
    if (!g_debug_mode) {
        return;
    }
    console_write("[debug] ");
    console_write(text ? text : "");
    console_put_char('\n');
}

static void shell_debug_tokens(char** tokens, int count) {
    if (!g_debug_mode) {
        return;
    }

    console_write("[debug] tokens:");
    for (int i = 0; i < count; i++) {
        console_write(" [");
        console_write(tokens[i]);
        console_write("]");
    }
    console_put_char('\n');
}

static int shell_run_script(
    const char* cwd,
    const char* path,
    uint8_t quiet_missing,
    const char* missing_prefix,
    const char* run_prefix
) {
    static uint8_t buf[SHELL_AUTORUN_MAX];
    uint32_t read_size = 0;
    size_t line_start = 0;

    if (vfs_read_file(cwd, path, buf, sizeof(buf) - 1u, &read_size) != 0) {
        if (!quiet_missing) {
            console_write(missing_prefix ? missing_prefix : "script");
            console_write(": not found: ");
            console_write(path);
            console_put_char('\n');
        }
        return -1;
    }

    buf[read_size] = 0;
    if (run_prefix) {
        console_write(run_prefix);
        console_write(": ");
        console_write(path);
        console_put_char('\n');
    }

    for (size_t i = 0; i <= read_size; i++) {
        if (i == read_size || buf[i] == '\n' || buf[i] == '\r') {
            size_t j = 0;
            char line[SHELL_INPUT_MAX];

            while (line_start < i && j + 1 < sizeof(line)) {
                line[j++] = (char)buf[line_start++];
            }
            line[j] = '\0';
            trim_in_place(line);
            if (line[0] && line[0] != '#') {
                if (g_debug_mode) {
                    console_write("[debug] script exec: ");
                    console_write(line);
                    console_put_char('\n');
                }
                if (shell_execute(line) != 0) {
                    console_write("warning: script command returned non-zero status\n");
                }
            }
            while (line_start <= i) {
                line_start++;
            }
        }
    }

    return 0;
}

static int shell_run_script_path(const char* path) {
    return shell_run_script(
        scheduler_current_cwd(),
        path,
        0u,
        "script",
        NULL
    );
}

static int shell_run_autorun_default(void) {
    if (shell_run_script("/", "/autorun.sh", 1u, "autorun", "autorun") == 0) {
        return 0;
    }
    if (shell_run_script("/", "/boot/autorun.sh", 1u, "autorun", "autorun") == 0) {
        return 0;
    }
    if (shell_run_script("/", "/ram/autorun.sh", 1u, "autorun", "autorun") == 0) {
        return 0;
    }
    return -1;
}

static int shell_debug_builtin(int argc, char* const* argv) {
    char mode[16];

    if (argc < 2) {
        console_write("debug is ");
        console_write(g_debug_mode ? "on" : "off");
        console_put_char('\n');
        return 0;
    }

    str_copy(mode, argv[1], sizeof(mode));
    to_lower(mode);

    if (str_eq(mode, "on")) {
        g_debug_mode = 1;
        console_write("debug on\n");
        return 0;
    }
    if (str_eq(mode, "off")) {
        g_debug_mode = 0;
        console_write("debug off\n");
        return 0;
    }
    if (str_eq(mode, "toggle")) {
        g_debug_mode = (uint8_t)(g_debug_mode ? 0u : 1u);
        console_write("debug ");
        console_write(g_debug_mode ? "on\n" : "off\n");
        return 0;
    }

    console_write("usage: debug [on|off|toggle]\n");
    return 1;
}

static int shell_verbose_builtin(int argc, char* const* argv) {
    char mode[16];

    if (argc < 2) {
        console_write("verbose is ");
        console_write(g_verbose_mode ? "on" : "off");
        console_put_char('\n');
        return 0;
    }

    str_copy(mode, argv[1], sizeof(mode));
    to_lower(mode);

    if (str_eq(mode, "on")) {
        g_verbose_mode = 1u;
        console_write("verbose on\n");
        return 0;
    }
    if (str_eq(mode, "off")) {
        g_verbose_mode = 0u;
        console_write("verbose off\n");
        return 0;
    }
    if (str_eq(mode, "toggle")) {
        g_verbose_mode = g_verbose_mode ? 0u : 1u;
        console_write("verbose ");
        console_write(g_verbose_mode ? "on\n" : "off\n");
        return 0;
    }

    console_write("usage: verbose [on|off|toggle]\n");
    return 1;
}

static int parse_command_tokens(char** tokens, int start, int end, shell_cmd_t* out) {
    int argc = 0;

    if (!out) {
        return -1;
    }

    out->argc = 0;
    out->background = 0;
    out->out_append = 0;
    out->out_path[0] = '\0';

    for (int i = start; i < end; i++) {
        if (str_eq(tokens[i], "&")) {
            if (i != end - 1) {
                return -1;
            }
            out->background = 1;
            continue;
        }
        if (str_eq(tokens[i], ">") || str_eq(tokens[i], ">>")) {
            if (i + 1 >= end) {
                return -1;
            }
            out->out_append = str_eq(tokens[i], ">>") ? 1 : 0;
            str_copy(out->out_path, tokens[i + 1], sizeof(out->out_path));
            i++;
            continue;
        }
        if (str_eq(tokens[i], "|") || str_eq(tokens[i], ";") || str_eq(tokens[i], "&&") || str_eq(tokens[i], "||")) {
            return -1;
        }
        if (argc >= SHELL_ARGV_MAX) {
            return -1;
        }
        out->argv[argc++] = tokens[i];
    }

    out->argc = argc;
    return 0;
}

static int shell_is_script_invocation(const char* cmd0) {
    if (!cmd0 || !str_ends_with(cmd0, ".sh")) {
        return 0;
    }

    return cmd0[0] == '/' ||
           str_starts_with(cmd0, "./") ||
           str_starts_with(cmd0, "../") ||
           str_contains_char(cmd0, '/');
}

static void shell_write_error_with_hint(const char* object, const char* reason, const char* hint) {
    console_write("error: ");
    if (object && object[0]) {
        console_write(object);
        console_write(": ");
    }
    console_write(reason ? reason : "operation failed");
    console_put_char('\n');
    if (hint && hint[0]) {
        console_write("hint: ");
        console_write(hint);
        console_put_char('\n');
    }
}

static int shell_build_boot_exec_path(const char* exec_path, char* out, size_t out_size) {
    size_t base = 0u;
    const char* suffix;

    if (!exec_path || !out || out_size == 0u || !str_starts_with(exec_path, "/bin/")) {
        return -1;
    }

    str_copy(out, "/boot/bin/", out_size);
    base = str_len(out);
    suffix = exec_path + 5;
    for (size_t i = 0; suffix[i] && base + 1u < out_size; i++) {
        out[base++] = suffix[i];
    }
    out[base] = '\0';
    return 0;
}

static shell_exec_probe_t shell_probe_exec_path(const char* exec_path, char* out_abs_path, size_t out_abs_size) {
    uint8_t probe = 0u;
    uint32_t read_size = 0u;

    if (!exec_path || !exec_path[0] || !out_abs_path || out_abs_size == 0u) {
        return SHELL_EXEC_PROBE_INVALID_PATH;
    }
    if (vfs_resolve_cwd(scheduler_current_cwd(), exec_path, out_abs_path, out_abs_size) != 0) {
        return SHELL_EXEC_PROBE_INVALID_PATH;
    }
    if (vfs_is_dir("/", out_abs_path) > 0) {
        return SHELL_EXEC_PROBE_IS_DIR;
    }
    if (vfs_can_exec("/", out_abs_path) != 0) {
        return SHELL_EXEC_PROBE_NO_EXEC;
    }
    if (vfs_read_file("/", out_abs_path, &probe, sizeof(probe), &read_size) != 0) {
        return SHELL_EXEC_PROBE_NOT_FOUND;
    }
    return SHELL_EXEC_PROBE_OK;
}

static void shell_report_spawn_failure(const char* exec_path, uint8_t tried_boot_fallback) {
    char abs_exec_path[MYAOS_PATH_MAX];
    shell_exec_probe_t probe = shell_probe_exec_path(exec_path, abs_exec_path, sizeof(abs_exec_path));

    if (probe == SHELL_EXEC_PROBE_INVALID_PATH) {
        shell_write_error_with_hint(exec_path, "invalid executable path", "use load /boot/bin/<name>.elf or an absolute path");
        return;
    }
    if (probe == SHELL_EXEC_PROBE_IS_DIR) {
        shell_write_error_with_hint(abs_exec_path, "path points to a directory", "choose an ELF program file (for example /boot/bin/name.elf)");
        return;
    }
    if (probe == SHELL_EXEC_PROBE_NO_EXEC) {
        shell_write_error_with_hint(abs_exec_path, "execute permission denied", "run chmod 755 <file> or use an executable from /boot/bin");
        return;
    }
    if (probe == SHELL_EXEC_PROBE_NOT_FOUND) {
        if (tried_boot_fallback && str_starts_with(exec_path, "/bin/")) {
            char boot_exec_path[MYAOS_PATH_MAX];
            char abs_boot_exec_path[MYAOS_PATH_MAX];
            shell_exec_probe_t boot_probe = SHELL_EXEC_PROBE_NOT_FOUND;

            if (shell_build_boot_exec_path(exec_path, boot_exec_path, sizeof(boot_exec_path)) == 0) {
                boot_probe = shell_probe_exec_path(boot_exec_path, abs_boot_exec_path, sizeof(abs_boot_exec_path));
            }
            if (boot_probe == SHELL_EXEC_PROBE_NOT_FOUND) {
                shell_write_error_with_hint(
                    exec_path,
                    "program not found in /bin or /boot/bin",
                    "check package installation and run help to list available commands"
                );
                return;
            }
        }
        shell_write_error_with_hint(abs_exec_path, "file not found or read denied", "check path spelling and read permissions");
        return;
    }

    shell_write_error_with_hint(
        abs_exec_path,
        "loader rejected executable (invalid ELF/ABI or corrupted file)",
        "rebuild the program and compare ABI via the `abi` command"
    );
}

static int shell_spawn_exec_path(
    const shell_cmd_t* cmd,
    const char* exec_path,
    int argc,
    char* const* argv,
    int32_t* out_exit_code
) {
    myaos_spawn_opts_t opts;
    char boot_exec_path[MYAOS_PATH_MAX];
    const char* spawn_exec_path = exec_path;
    int32_t pid = -1;
    int32_t exit_code = 0;
    int64_t spawn_rc;
    uint8_t ctrl_c_sent = 0;
    uint8_t ctrl_c_passthrough = 0;
    uint8_t tried_boot_fallback = 0u;
    uint8_t used_boot_fallback = 0u;

    if (!cmd || !exec_path || !argv || argc <= 0) {
        return 126;
    }
    if (str_eq(exec_path, "/bin/msh.elf") || str_eq(exec_path, "/boot/bin/msh.elf")) {
        ctrl_c_passthrough = 1u;
    }

    if (g_debug_mode) {
        console_write("[debug] spawn ");
        console_write(exec_path);
        console_write(" argc=");
        console_write_u32((uint32_t)argc);
        console_put_char('\n');
    }

    opts.flags = 0;
    opts.stdout_append = 0;
    opts.priority = 0;
    opts.reserved0 = 0;
    opts.cpu_limit_ticks = 0;
    opts.stdout_path[0] = '\0';
    if (cmd->background) {
        opts.flags |= MYAOS_SPAWN_BACKGROUND;
    }
    if (cmd->out_path[0]) {
        opts.flags |= MYAOS_SPAWN_STDOUT_REDIRECT;
        opts.stdout_append = cmd->out_append;
        str_copy(opts.stdout_path, cmd->out_path, sizeof(opts.stdout_path));
    }

    spawn_rc = syscall_invoke(
        MYAOS_SYS_PROC_SPAWN_EX,
        (uint64_t)(uintptr_t)spawn_exec_path,
        (uint64_t)(uint32_t)argc,
        (uint64_t)(uintptr_t)argv,
        (uint64_t)(uintptr_t)&opts,
        (uint64_t)(uintptr_t)&pid
    );
    if (spawn_rc != 0) {
        if (str_starts_with(exec_path, "/bin/")) {
            tried_boot_fallback = 1u;
            if (shell_build_boot_exec_path(exec_path, boot_exec_path, sizeof(boot_exec_path)) == 0) {
                spawn_exec_path = boot_exec_path;
                if (str_eq(spawn_exec_path, "/boot/bin/msh.elf")) {
                    ctrl_c_passthrough = 1u;
                }
                spawn_rc = syscall_invoke(
                    MYAOS_SYS_PROC_SPAWN_EX,
                    (uint64_t)(uintptr_t)spawn_exec_path,
                    (uint64_t)(uint32_t)argc,
                    (uint64_t)(uintptr_t)argv,
                    (uint64_t)(uintptr_t)&opts,
                    (uint64_t)(uintptr_t)&pid
                );
                if (spawn_rc == 0) {
                    used_boot_fallback = 1u;
                }
            }
        }
        if (spawn_rc != 0) {
            shell_report_spawn_failure(exec_path, tried_boot_fallback);
            return 126;
        }
    }

    if (used_boot_fallback) {
        console_write("note: using fallback executable ");
        console_write(spawn_exec_path);
        console_put_char('\n');
    }

    if (cmd->background) {
        console_write("started pid ");
        console_write_u32((uint32_t)pid);
        console_put_char('\n');
        return 0;
    }

    for (;;) {
        int64_t wait_state = syscall_invoke(
            MYAOS_SYS_PROC_WAIT_POLL,
            (uint64_t)pid,
            (uint64_t)(uintptr_t)&exit_code,
            0,
            0,
            0
        );

        if (wait_state < 0) {
            console_write("error: pid ");
            console_write_u32((uint32_t)pid);
            console_write(": wait failed\n");
            console_write("hint: process may already be reaped or not owned by this shell\n");
            return 125;
        }
        if (wait_state > 0) {
            break;
        }

        if (!ctrl_c_passthrough && !ctrl_c_sent && keyboard_take_ctrl_c()) {
            ctrl_c_sent = 1;
            (void)syscall_invoke(MYAOS_SYS_PROC_KILL, (uint64_t)pid, (uint64_t)130, 0, 0, 0);
            console_write("^C\n");
        }

        (void)syscall_invoke(MYAOS_SYS_PROC_YIELD, 0, 0, 0, 0, 0);
    }

    if (g_debug_mode) {
        console_write("[debug] pid ");
        console_write_u32((uint32_t)pid);
        console_write(" exit=");
        console_write_u32((uint32_t)exit_code);
        console_put_char('\n');
    }

    if (out_exit_code) {
        *out_exit_code = exit_code;
    }
    return exit_code;
}

static int shell_spawn_command(const shell_cmd_t* cmd, int32_t* out_exit_code) {
    char exec_path[MYAOS_PATH_MAX];
    char cmd_name[MYAOS_NAME_MAX];
    char desc[MYAOS_DESC_MAX];
    char* load_argv[SHELL_ARGV_MAX];
    int load_argc = 0;

    if (!cmd || cmd->argc <= 0) {
        return 0;
    }

    str_copy(cmd_name, cmd->argv[0], sizeof(cmd_name));
    to_lower(cmd_name);
    if (str_eq(cmd_name, "help")) {
        return shell_help(cmd);
    }
    if (str_eq(cmd_name, "history")) {
        return shell_history_builtin(cmd);
    }
    if (str_eq(cmd_name, "cd")) {
        if (cmd->out_path[0]) {
            console_write("redirection for builtins is not supported\n");
            return 1;
        }
        return shell_cd(cmd->argc, cmd->argv);
    }
    if (str_eq(cmd_name, "debug")) {
        if (cmd->out_path[0]) {
            console_write("redirection for builtins is not supported\n");
            return 1;
        }
        return shell_debug_builtin(cmd->argc, cmd->argv);
    }
    if (str_eq(cmd_name, "verbose")) {
        if (cmd->out_path[0]) {
            console_write("redirection for builtins is not supported\n");
            return 1;
        }
        return shell_verbose_builtin(cmd->argc, cmd->argv);
    }
    if (str_eq(cmd_name, "autorun")) {
        if (cmd->out_path[0]) {
            console_write("redirection for builtins is not supported\n");
            return 1;
        }
        if (shell_run_autorun_default() != 0) {
            console_write("autorun: no script found (/autorun.sh, /boot/autorun.sh, or /ram/autorun.sh)\n");
            return 1;
        }
        return 0;
    }

    if (str_eq(cmd_name, "load")) {
        if (cmd->argc < 2) {
            console_write("usage: load <program.elf> [args...]\n");
            return 2;
        }
        if (cmd->argc - 1 > SHELL_ARGV_MAX) {
            console_write("too many arguments for load\n");
            return 2;
        }
        for (int i = 1; i < cmd->argc && load_argc < SHELL_ARGV_MAX; i++) {
            load_argv[load_argc++] = cmd->argv[i];
        }
        if (load_argc <= 0) {
            console_write("usage: load <program.elf> [args...]\n");
            return 2;
        }
        return shell_spawn_exec_path(cmd, load_argv[0], load_argc, load_argv, out_exit_code);
    }

    if (shell_is_script_invocation(cmd->argv[0])) {
        if (cmd->background || cmd->out_path[0]) {
            console_write("background/redirection for scripts is not supported\n");
            return 2;
        }
        if (cmd->argc > 1) {
            console_write("script arguments are not supported yet\n");
            return 2;
        }
        return shell_run_script_path(cmd->argv[0]) == 0 ? 0 : 1;
    }

    if (read_command_manifest(cmd_name, exec_path, sizeof(exec_path), desc, sizeof(desc)) != 0) {
        console_write("error: command not found: ");
        console_write(cmd->argv[0]);
        console_put_char('\n');
        console_write("hint: run help to list commands from /cmd and /bin\n");
        return 127;
    }
    return shell_spawn_exec_path(cmd, exec_path, cmd->argc, cmd->argv, out_exit_code);
}

static void make_pipe_temp_path_in_dir(const char* base_dir, char* out, size_t out_size) {
    char pid_buf[16];
    char seq_buf[16];
    size_t pos = 0;
    const char* dir = base_dir ? base_dir : "/boot";

    u32_to_dec((uint32_t)scheduler_current_pid(), pid_buf, sizeof(pid_buf));
    u32_to_dec(g_pipe_seq++, seq_buf, sizeof(seq_buf));

    if (out_size == 0) {
        return;
    }
    out[0] = '\0';

    for (size_t i = 0; dir[i] && pos + 1 < out_size; i++) {
        out[pos++] = dir[i];
    }
    if (pos == 0 || out[pos - 1] != '/') {
        if (pos + 1 < out_size) {
            out[pos++] = '/';
        }
    }
    if (pos + 6 < out_size) {
        out[pos++] = '.';
        out[pos++] = 'p';
        out[pos++] = 'i';
        out[pos++] = 'p';
        out[pos++] = 'e';
        out[pos++] = '_';
    }
    for (size_t i = 0; pid_buf[i] && pos + 1 < out_size; i++) {
        out[pos++] = pid_buf[i];
    }
    if (pos + 1 < out_size) {
        out[pos++] = '_';
    }
    for (size_t i = 0; seq_buf[i] && pos + 1 < out_size; i++) {
        out[pos++] = seq_buf[i];
    }
    if (pos + 4 < out_size) {
        out[pos++] = '.';
        out[pos++] = 't';
        out[pos++] = 'm';
        out[pos++] = 'p';
    }
    out[pos] = '\0';
}

static int make_pipe_temp_path(char* out, size_t out_size) {
    const char* dirs[] = {"/boot", "/ram", "/"};
    const uint8_t empty = 0;

    if (!out || out_size == 0) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        make_pipe_temp_path_in_dir(dirs[i], out, out_size);
        if (out[0] == '\0') {
            continue;
        }
        if (vfs_write_file("/", out, &empty, 0u) == 0) {
            (void)vfs_remove("/", out);
            return 0;
        }
    }

    out[0] = '\0';
    return -1;
}

static int shell_execute_segment(char** tokens, int start, int end, int32_t* out_status) {
    int pipe_pos = -1;
    shell_cmd_t left;
    shell_cmd_t right;
    int32_t status = 0;

    if (start >= end) {
        return -1;
    }

    for (int i = start; i < end; i++) {
        if (str_eq(tokens[i], "|")) {
            if (pipe_pos >= 0) {
                console_write("only one pipe is supported per segment\n");
                return 2;
            }
            pipe_pos = i;
        }
    }

    if (pipe_pos < 0) {
        if (parse_command_tokens(tokens, start, end, &left) != 0) {
            console_write("syntax error\n");
            return 2;
        }
        status = shell_spawn_command(&left, out_status);
        if (out_status) {
            *out_status = status;
        }
        return status;
    }

    if (parse_command_tokens(tokens, start, pipe_pos, &left) != 0 ||
        parse_command_tokens(tokens, pipe_pos + 1, end, &right) != 0) {
        console_write("pipe syntax error\n");
        return 2;
    }
    if (left.background || right.background) {
        console_write("background with pipe is not supported\n");
        return 2;
    }

    {
        char pipe_path[MYAOS_PATH_MAX];

        if (make_pipe_temp_path(pipe_path, sizeof(pipe_path)) != 0) {
            console_write("pipe storage is unavailable\n");
            return 1;
        }
        str_copy(left.out_path, pipe_path, sizeof(left.out_path));
        left.out_append = 0;

        status = shell_spawn_command(&left, &status);
        if (status == 0) {
            if (right.argc < SHELL_ARGV_MAX) {
                right.argv[right.argc++] = pipe_path;
            } else {
                console_write("pipe right side has too many args\n");
                status = 2;
            }
            if (status == 0) {
                status = shell_spawn_command(&right, &status);
            }
        }

        (void)vfs_remove("/", pipe_path);
    }

    if (out_status) {
        *out_status = status;
    }
    return status;
}

static int32_t shell_execute(const char* raw_line) {
    char line[SHELL_INPUT_MAX];
    char* tokens[SHELL_TOK_MAX];
    int tok_count;
    int idx = 0;
    int run_policy = 0;
    int32_t last_status = 0;

    str_copy(line, raw_line, sizeof(line));
    trim_in_place(line);
    if (!line[0]) {
        return 0;
    }
    if (g_debug_mode) {
        console_write("[debug] line: ");
        console_write(line);
        console_put_char('\n');
    }

    tok_count = tokenize_line(line, tokens, SHELL_TOK_MAX);
    if (tok_count <= 0) {
        if (tok_count < 0) {
            console_write("parse error\n");
        }
        return 2;
    }
    shell_debug_tokens(tokens, tok_count);

    while (idx < tok_count) {
        int seg_start = idx;
        int seg_end = idx;
        int should_run = 1;

        while (seg_end < tok_count &&
               !str_eq(tokens[seg_end], ";") &&
               !str_eq(tokens[seg_end], "&&") &&
               !str_eq(tokens[seg_end], "||")) {
            seg_end++;
        }

        if (run_policy > 0 && last_status != 0) {
            should_run = 0;
        } else if (run_policy < 0 && last_status == 0) {
            should_run = 0;
        }

        if (should_run) {
            last_status = 0;
            if (shell_execute_segment(tokens, seg_start, seg_end, &last_status) < 0) {
                console_write("execution error\n");
                if (last_status == 0) {
                    last_status = 1;
                }
                break;
            }
        } else if (g_debug_mode) {
            shell_debug_write("segment skipped by &&/|| policy");
        }

        if (seg_end >= tok_count) {
            break;
        }
        if (str_eq(tokens[seg_end], "&&")) {
            run_policy = 1;
        } else if (str_eq(tokens[seg_end], "||")) {
            run_policy = -1;
        } else {
            run_policy = 0;
        }
        idx = seg_end + 1;
    }

    return last_status;
}

static void shell_erase_rendered_input(void) {
    while (g_rendered_input_len > 0u) {
        console_put_char('\b');
        g_rendered_input_len--;
    }
}

static void shell_restore_cursor_overlay(void) {
    char under = ' ';

    if (!g_cursor_overlay_active) {
        return;
    }
    if (g_cursor_overlay_index < g_input_len) {
        under = g_input[g_cursor_overlay_index];
    }
    console_draw_cell(g_cursor_overlay_col, g_cursor_overlay_row, under);
    g_cursor_overlay_active = 0u;
}

static void shell_draw_cursor_overlay(void) {
    boot_info_t* boot = console_boot_info();
    uint32_t cols = console_cols();
    uint32_t absolute_col;
    uint32_t col;
    uint32_t row;
    uint32_t x;
    uint32_t y;

    if (!boot || cols == 0u) {
        return;
    }

    absolute_col = g_prompt_col + shell_utf8_cell_count(g_input, g_input_cursor);
    col = absolute_col % cols;
    row = g_prompt_row + (absolute_col / cols);
    x = col * FONT_WIDTH;
    y = row * FONT_HEIGHT + (FONT_HEIGHT - 2u);

    if (x + FONT_WIDTH > boot->fb.width || y + 1u >= boot->fb.height) {
        return;
    }

    for (uint32_t i = 0; i < FONT_WIDTH; i++) {
        put_pixel(boot, x + i, y, 0x00FFFFFFu);
        put_pixel(boot, x + i, y + 1u, 0x00FFFFFFu);
    }
    g_cursor_overlay_active = 1u;
    g_cursor_overlay_col = col;
    g_cursor_overlay_row = row;
    g_cursor_overlay_index = g_input_cursor;
}

static void shell_render_input(uint8_t show_cursor) {
    shell_restore_cursor_overlay();
    shell_erase_rendered_input();

    if (g_input_len > 0u) {
        console_write_len(g_input, g_input_len);
        g_rendered_input_len = shell_utf8_cell_count(g_input, g_input_len);
    } else {
        g_rendered_input_len = 0u;
    }

    if (show_cursor) {
        shell_draw_cursor_overlay();
    }
}

static void shell_cursor_activity(void) {
    g_cursor_visible = 1u;
    g_cursor_last_blink_tick = timer_ticks();
}

static void shell_clear_input_line(void) {
    shell_restore_cursor_overlay();
    shell_erase_rendered_input();
    g_input_len = 0u;
    g_input_cursor = 0u;
    g_input[0] = '\0';
}

static void shell_set_input_text(const char* text) {
    shell_clear_input_line();
    str_copy(g_input, text ? text : "", sizeof(g_input));
    g_input_len = (uint32_t)str_len(g_input);
    g_input_cursor = g_input_len;
    shell_cursor_activity();
    shell_render_input(1u);
}

static void shell_restore_last_command(void) {
    const char* line;

    if (g_history_count == 0u) {
        return;
    }
    if (g_history_nav < 0) {
        str_copy(g_history_saved_input, g_input, sizeof(g_history_saved_input));
        g_history_saved_valid = 1u;
        g_history_nav = (int32_t)(g_history_count - 1u);
    } else if (g_history_nav > 0) {
        g_history_nav--;
    }

    line = shell_history_get((uint32_t)g_history_nav);
    if (line) {
        shell_set_input_text(line);
    }
}

static void shell_history_nav_down(void) {
    const char* line;

    if (g_history_nav < 0) {
        shell_clear_input_line();
        shell_render_input(1u);
        return;
    }

    if ((uint32_t)(g_history_nav + 1) < g_history_count) {
        g_history_nav++;
        line = shell_history_get((uint32_t)g_history_nav);
        if (line) {
            shell_set_input_text(line);
        }
        return;
    }

    g_history_nav = -1;
    if (g_history_saved_valid) {
        shell_set_input_text(g_history_saved_input);
    } else {
        shell_clear_input_line();
        shell_render_input(1u);
    }
    g_history_saved_valid = 0u;
}

void shell_init(void) {
    g_input_len = 0;
    g_input_cursor = 0;
    g_input[0] = '\0';
    g_last_command[0] = '\0';
    g_has_last_command = 0;
    g_history_start = 0u;
    g_history_count = 0u;
    g_history_nav = -1;
    g_history_saved_input[0] = '\0';
    g_history_saved_valid = 0u;
    g_prompt_shown = 0;
    g_pipe_seq = 1u;
    g_debug_mode = 0;
    g_autorun_done = 0;
    g_recovery_mode = 0u;
    g_rendered_input_len = 0u;
    g_cursor_visible = 1u;
    g_cursor_last_blink_tick = 0u;
    g_prompt_col = 0u;
    g_prompt_row = 0u;
    g_cursor_overlay_active = 0u;
    g_cursor_overlay_col = 0u;
    g_cursor_overlay_row = 0u;
    g_cursor_overlay_index = 0u;
    g_last_status = 0;
    g_verbose_mode = 0u;
    for (size_t i = 0; i < SHELL_HISTORY_MAX; i++) {
        g_history[i][0] = '\0';
    }
    shell_history_load_persistent();
    for (size_t i = 0; i < SHELL_MANIFEST_CACHE_MAX; i++) {
        g_manifest_cache[i].used = 0u;
        g_manifest_cache[i].name[0] = '\0';
        g_manifest_cache[i].exec_path[0] = '\0';
        g_manifest_cache[i].desc[0] = '\0';
    }
}

void shell_set_recovery_mode(uint8_t enabled) {
    g_recovery_mode = enabled ? 1u : 0u;
}

int shell_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    console_write(g_recovery_mode ? "myaos recovery shell\n" : "myaos shell\n");
    console_write("type help\n\n");
    if (g_recovery_mode) {
        console_write("recovery mode: autorun disabled\n");
        console_write("tip: log --file /var/log/boot.last.log\n\n");
    } else if (!g_autorun_done) {
        g_autorun_done = 1;
        (void)shell_run_autorun_default();
    }

    for (;;) {
        char c;
        uint64_t now;

        if (!g_prompt_shown) {
            console_set_mode(CONSOLE_MODE_TEXT);
            char code[16];
            i32_to_dec(g_last_status, code, sizeof(code));
            console_write("[");
            console_write(code);
            console_write("] ");
            console_write(scheduler_current_cwd());
            console_write(" $ ");
            console_get_cursor(&g_prompt_col, &g_prompt_row);
            g_prompt_shown = 1;
            g_rendered_input_len = 0u;
            g_cursor_visible = 1u;
            g_cursor_last_blink_tick = timer_ticks();
            g_cursor_overlay_active = 0u;
            shell_render_input(1u);
        }

        now = timer_ticks();
        if (now - g_cursor_last_blink_tick >= SHELL_CURSOR_BLINK_TICKS) {
            g_cursor_last_blink_tick = now;
            g_cursor_visible = g_cursor_visible ? 0u : 1u;
            shell_render_input(g_cursor_visible);
        }

        if (keyboard_take_ctrl_c()) {
            shell_clear_input_line();
            g_history_nav = -1;
            g_history_saved_valid = 0u;
            console_write("^C\n");
            g_prompt_shown = 0;
            continue;
        }

        c = keyboard_read_char();
        if (c == 0) {
            continue;
        }

        shell_cursor_activity();

        if (c == KEYBOARD_KEY_UP) {
            shell_restore_last_command();
            continue;
        }
        if (c == KEYBOARD_KEY_DOWN) {
            shell_history_nav_down();
            continue;
        }
        if (c == KEYBOARD_KEY_LEFT) {
            if (g_input_cursor > 0u) {
                g_input_cursor = shell_utf8_prev_start(g_input, g_input_cursor);
            }
            shell_render_input(1u);
            continue;
        }
        if (c == KEYBOARD_KEY_RIGHT) {
            if (g_input_cursor < g_input_len) {
                g_input_cursor = shell_utf8_next_start(g_input, g_input_len, g_input_cursor);
            }
            shell_render_input(1u);
            continue;
        }
        if (c == '\t') {
            shell_try_complete_command();
            continue;
        }

        if (c == '\n') {
            char committed[SHELL_INPUT_MAX];
            int32_t status;

            shell_render_input(0u);
            console_put_char('\n');
            g_input[g_input_len] = '\0';
            str_copy(committed, g_input, sizeof(committed));
            trim_in_place(committed);
            if (committed[0]) {
                str_copy(g_last_command, committed, sizeof(g_last_command));
                g_has_last_command = 1u;
                shell_history_commit(committed);
            }
            status = shell_execute(g_input);
            g_last_status = status;
            if (g_verbose_mode && status != 0) {
                char code[16];
                i32_to_dec(status, code, sizeof(code));
                console_write("exit-code: ");
                console_write(code);
                console_put_char('\n');
            }
            g_input_len = 0u;
            g_input_cursor = 0u;
            g_input[0] = '\0';
            g_history_nav = -1;
            g_history_saved_valid = 0u;
            g_rendered_input_len = 0u;
            g_cursor_visible = 1u;
            g_prompt_shown = 0;
            continue;
        }

        if (c == '\b') {
            if (g_history_nav >= 0) {
                g_history_nav = -1;
                g_history_saved_valid = 0u;
            }
            if (g_input_cursor > 0u && g_input_len > 0u) {
                uint32_t start = shell_utf8_prev_start(g_input, g_input_cursor);
                uint32_t removed = g_input_cursor - start;
                for (uint32_t i = start; i + removed < g_input_len; i++) {
                    g_input[i] = g_input[i + removed];
                }
                g_input_len -= removed;
                g_input_cursor = start;
                g_input[g_input_len] = '\0';
            }
            shell_render_input(1u);
            continue;
        }

        if ((uint8_t)c < 32u || (uint8_t)c == 127u || g_input_len + 1 >= sizeof(g_input)) {
            continue;
        }

        if (g_history_nav >= 0) {
            g_history_nav = -1;
            g_history_saved_valid = 0u;
        }
        for (uint32_t i = g_input_len; i > g_input_cursor; i--) {
            g_input[i] = g_input[i - 1u];
        }
        g_input[g_input_cursor] = c;
        g_input_len++;
        g_input_cursor++;
        g_input[g_input_len] = '\0';
        shell_render_input(1u);
    }
}

int shell_idle_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
