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
static uint8_t g_prompt_shown;
static uint32_t g_pipe_seq;
static uint8_t g_debug_mode;
static uint8_t g_autorun_done;
static uint32_t g_rendered_input_len;
static uint8_t g_cursor_visible;
static uint64_t g_cursor_last_blink_tick;
static uint32_t g_prompt_col;
static uint32_t g_prompt_row;
static uint8_t g_cursor_overlay_active;
static uint32_t g_cursor_overlay_col;
static uint32_t g_cursor_overlay_row;
static uint32_t g_cursor_overlay_index;
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

typedef struct {
    char* argv[SHELL_ARGV_MAX];
    int argc;
    uint8_t background;
    uint8_t out_append;
    char out_path[MYAOS_PATH_MAX];
} shell_cmd_t;

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

#define HELP_APPEND_TEXT(text_literal)                                                        \
    do {                                                                                      \
        if (!truncated && append_text(out_text, sizeof(out_text), &out_pos, (text_literal)) != 0) { \
            truncated = 1u;                                                                   \
        }                                                                                     \
    } while (0)

    out_text[0] = '\0';
    HELP_APPEND_TEXT("help - show commands\n");
    HELP_APPEND_TEXT("cd - change shell directory\n");
    HELP_APPEND_TEXT("debug - shell debug [on|off|toggle]\n");
    HELP_APPEND_TEXT("autorun - run /autorun.sh, /boot/autorun.sh, or /ram/autorun.sh\n");
    HELP_APPEND_TEXT("load - run ELF directly: load <path.elf> [args...]\n");
    HELP_APPEND_TEXT("./name.sh - execute script from current directory\n");

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

static void shell_cd(int argc, char* const* argv) {
    char abs_path[MYAOS_PATH_MAX];

    if (argc < 2) {
        console_write(scheduler_current_cwd());
        console_put_char('\n');
        return;
    }
    if (vfs_resolve_cwd(scheduler_current_cwd(), argv[1], abs_path, sizeof(abs_path)) != 0) {
        console_write("invalid path\n");
        return;
    }
    if (vfs_is_dir("/", abs_path) <= 0 || scheduler_setcwd_current(abs_path) != 0) {
        console_write("directory not found\n");
    }
}

static void shell_execute(const char* raw_line);

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
                shell_execute(line);
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
    uint8_t ctrl_c_sent = 0;
    uint8_t ctrl_c_passthrough = 0;

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

    if (syscall_invoke(
            MYAOS_SYS_PROC_SPAWN_EX,
            (uint64_t)(uintptr_t)spawn_exec_path,
            (uint64_t)(uint32_t)argc,
            (uint64_t)(uintptr_t)argv,
            (uint64_t)(uintptr_t)&opts,
            (uint64_t)(uintptr_t)&pid
        ) != 0) {
        if (str_starts_with(exec_path, "/bin/")) {
            str_copy(boot_exec_path, "/boot/bin/", sizeof(boot_exec_path));
            {
                size_t base = str_len(boot_exec_path);
                const char* suffix = exec_path + 5;
                for (size_t i = 0; suffix[i] && base + 1 < sizeof(boot_exec_path); i++) {
                    boot_exec_path[base++] = suffix[i];
                }
                boot_exec_path[base] = '\0';
            }
            spawn_exec_path = boot_exec_path;
            if (str_eq(spawn_exec_path, "/boot/bin/msh.elf")) {
                ctrl_c_passthrough = 1u;
            }
            if (syscall_invoke(
                    MYAOS_SYS_PROC_SPAWN_EX,
                    (uint64_t)(uintptr_t)spawn_exec_path,
                    (uint64_t)(uint32_t)argc,
                    (uint64_t)(uintptr_t)argv,
                    (uint64_t)(uintptr_t)&opts,
                    (uint64_t)(uintptr_t)&pid
                ) != 0) {
                console_write("failed to start program\n");
                return 126;
            }
        } else {
            console_write("failed to start program\n");
            return 126;
        }
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
            console_write("wait failed\n");
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
    if (str_eq(cmd_name, "cd")) {
        if (cmd->out_path[0]) {
            console_write("redirection for builtins is not supported\n");
            return 1;
        }
        shell_cd(cmd->argc, cmd->argv);
        return 0;
    }
    if (str_eq(cmd_name, "debug")) {
        if (cmd->out_path[0]) {
            console_write("redirection for builtins is not supported\n");
            return 1;
        }
        return shell_debug_builtin(cmd->argc, cmd->argv);
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
        console_write("unknown command\n");
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

static void shell_execute(const char* raw_line) {
    char line[SHELL_INPUT_MAX];
    char* tokens[SHELL_TOK_MAX];
    int tok_count;
    int idx = 0;
    int run_policy = 0;
    int32_t last_status = 0;

    str_copy(line, raw_line, sizeof(line));
    trim_in_place(line);
    if (!line[0]) {
        return;
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
        return;
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

    absolute_col = g_prompt_col + g_input_cursor;
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
        g_rendered_input_len = g_input_len;
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

static void shell_restore_last_command(void) {
    if (!g_has_last_command) {
        return;
    }
    shell_clear_input_line();
    str_copy(g_input, g_last_command, sizeof(g_input));
    g_input_len = (uint32_t)str_len(g_input);
    g_input_cursor = g_input_len;
    shell_cursor_activity();
    shell_render_input(1u);
}

void shell_init(void) {
    g_input_len = 0;
    g_input_cursor = 0;
    g_input[0] = '\0';
    g_last_command[0] = '\0';
    g_has_last_command = 0;
    g_prompt_shown = 0;
    g_pipe_seq = 1u;
    g_debug_mode = 0;
    g_autorun_done = 0;
    g_rendered_input_len = 0u;
    g_cursor_visible = 1u;
    g_cursor_last_blink_tick = 0u;
    g_prompt_col = 0u;
    g_prompt_row = 0u;
    g_cursor_overlay_active = 0u;
    g_cursor_overlay_col = 0u;
    g_cursor_overlay_row = 0u;
    g_cursor_overlay_index = 0u;
    for (size_t i = 0; i < SHELL_MANIFEST_CACHE_MAX; i++) {
        g_manifest_cache[i].used = 0u;
        g_manifest_cache[i].name[0] = '\0';
        g_manifest_cache[i].exec_path[0] = '\0';
        g_manifest_cache[i].desc[0] = '\0';
    }
}

int shell_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    console_write("myaos shell\n");
    console_write("type help\n\n");
    if (!g_autorun_done) {
        g_autorun_done = 1;
        (void)shell_run_autorun_default();
    }

    for (;;) {
        char c;
        uint64_t now;

        if (!g_prompt_shown) {
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
            shell_clear_input_line();
            shell_render_input(1u);
            continue;
        }
        if (c == KEYBOARD_KEY_LEFT) {
            if (g_input_cursor > 0u) {
                g_input_cursor--;
            }
            shell_render_input(1u);
            continue;
        }
        if (c == KEYBOARD_KEY_RIGHT) {
            if (g_input_cursor < g_input_len) {
                g_input_cursor++;
            }
            shell_render_input(1u);
            continue;
        }

        if (c == '\n') {
            char committed[SHELL_INPUT_MAX];

            shell_render_input(0u);
            console_put_char('\n');
            g_input[g_input_len] = '\0';
            str_copy(committed, g_input, sizeof(committed));
            trim_in_place(committed);
            if (committed[0]) {
                str_copy(g_last_command, committed, sizeof(g_last_command));
                g_has_last_command = 1u;
            }
            shell_execute(g_input);
            g_input_len = 0u;
            g_input_cursor = 0u;
            g_input[0] = '\0';
            g_rendered_input_len = 0u;
            g_cursor_visible = 1u;
            g_prompt_shown = 0;
            continue;
        }

        if (c == '\b') {
            if (g_input_cursor > 0u && g_input_len > 0u) {
                uint32_t start = g_input_cursor - 1u;
                for (uint32_t i = start; i + 1u < g_input_len; i++) {
                    g_input[i] = g_input[i + 1u];
                }
                g_input_len--;
                g_input_cursor--;
                g_input[g_input_len] = '\0';
            }
            shell_render_input(1u);
            continue;
        }

        if (c < 32 || c > 126 || g_input_len + 1 >= sizeof(g_input)) {
            continue;
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
