#include "../lib/myaos.h"
#include <stdint.h>

#define MSH_INPUT_MAX 256
#define MSH_ARGV_MAX 16
#define MSH_MANIFEST_MAX 256
#define MSH_SCRIPT_MAX 4096
#define MSH_MANIFEST_CACHE_MAX 32

typedef struct {
    char* argv[MSH_ARGV_MAX];
    int argc;
    uint8_t background;
} msh_cmd_t;

typedef struct {
    uint8_t used;
    char name[MYAOS_NAME_MAX];
    char exec_path[MYAOS_PATH_MAX];
} msh_manifest_cache_entry_t;

static msh_manifest_cache_entry_t g_manifest_cache[MSH_MANIFEST_CACHE_MAX];

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

static int build_manifest_path(const char* base_dir, const char* cmd, char* out, size_t out_size) {
    size_t pos = 0;

    if (!base_dir || !cmd || !out || out_size == 0) {
        return -1;
    }

    while (base_dir[pos] && pos + 1 < out_size) {
        out[pos] = base_dir[pos];
        pos++;
    }
    if (base_dir[pos] != '\0') {
        return -1;
    }
    if (pos == 0 || out[pos - 1] != '/') {
        if (pos + 1 >= out_size) {
            return -1;
        }
        out[pos++] = '/';
    }

    for (size_t i = 0; cmd[i]; i++) {
        if (pos + 1 >= out_size) {
            return -1;
        }
        out[pos++] = cmd[i];
    }
    if (pos + 4 >= out_size) {
        return -1;
    }
    out[pos++] = '.';
    out[pos++] = 'c';
    out[pos++] = 'm';
    out[pos++] = 'd';
    out[pos] = '\0';
    return 0;
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

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static size_t utf8_prev_start(const char* text, size_t pos) {
    if (!text || pos == 0u) {
        return 0u;
    }
    pos--;
    while (pos > 0u && (((uint8_t)text[pos] & 0xC0u) == 0x80u)) {
        pos--;
    }
    return pos;
}

static int read_line(char* out, size_t out_size) {
    size_t len = 0;

    if (!out || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    for (;;) {
        int ch = mya_console_readchar();
        if (ch == 0) {
            mya_proc_yield();
            continue;
        }

        if (ch == 3) {
            len = 0;
            out[0] = '\0';
            mya_puts("^C\n");
            return 1;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            out[len] = '\0';
            mya_puts("\n");
            return 0;
        }
        if (ch == '\b' || ch == 127) {
            if (len > 0) {
                len = utf8_prev_start(out, len);
                out[len] = '\0';
                mya_puts("\b");
            }
            continue;
        }
        if (((uint8_t)ch < 32u) || ((uint8_t)ch == 127u)) {
            continue;
        }
        if (len + 1 < out_size) {
            out[len++] = (char)ch;
            out[len] = '\0';
            {
                char tmp[2];
                tmp[0] = (char)ch;
                tmp[1] = '\0';
                mya_puts(tmp);
            }
        }
    }
}

static int tokenize_line(char* line, msh_cmd_t* out) {
    char* p = line;
    int argc = 0;

    if (!out) {
        return -1;
    }
    out->argc = 0;
    out->background = 0;

    while (*p) {
        while (is_space(*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (argc >= MSH_ARGV_MAX) {
            return -1;
        }
        out->argv[argc++] = p;
        while (*p && !is_space(*p)) {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }

    if (argc > 0 && str_eq(out->argv[argc - 1], "&")) {
        out->background = 1;
        argc--;
    }

    out->argc = argc;
    return 0;
}

static int read_manifest_exec(const char* cmd, char* out_exec, size_t out_size) {
    char cmd_key[MYAOS_NAME_MAX];
    char path[MYAOS_PATH_MAX];
    uint8_t buf[MSH_MANIFEST_MAX];
    uint32_t read_size = 0;
    size_t i = 0;
    size_t pos = 0;

    if (!cmd || !out_exec || out_size == 0) {
        return -1;
    }
    str_copy(cmd_key, cmd, sizeof(cmd_key));
    for (size_t ci = 0; cmd_key[ci]; ci++) {
        cmd_key[ci] = to_lower_char(cmd_key[ci]);
    }

    for (uint32_t ci = 0; ci < MSH_MANIFEST_CACHE_MAX; ci++) {
        if (!g_manifest_cache[ci].used || !str_eq(g_manifest_cache[ci].name, cmd_key)) {
            continue;
        }
        str_copy(out_exec, g_manifest_cache[ci].exec_path, out_size);
        return out_exec[0] ? 0 : -1;
    }

    if (build_manifest_path("/cmd", cmd_key, path, sizeof(path)) != 0) {
        return -1;
    }
    if (mya_fs_read(path, buf, sizeof(buf) - 1u, &read_size) != 0) {
        myaos_dirent_t entries[128];
        uint32_t count = 0;
        uint8_t found = 0u;

        if (mya_fs_list("/cmd", entries, 128u, &count) == 0) {
            for (uint32_t ei = 0; ei < count; ei++) {
                size_t name_len;
                if (entries[ei].type != MYAOS_NODE_FILE || !str_ends_with_ci(entries[ei].name, ".cmd")) {
                    continue;
                }
                name_len = str_len(entries[ei].name);
                if (name_len <= 4u) {
                    continue;
                }
                str_copy(path, entries[ei].name, sizeof(path));
                path[name_len - 4u] = '\0';
                if (!str_eq_ci(path, cmd_key)) {
                    continue;
                }
                str_copy(path, "/cmd/", sizeof(path));
                {
                    size_t p = str_len(path);
                    for (size_t j = 0; entries[ei].name[j] && p + 1 < sizeof(path); j++) {
                        path[p++] = entries[ei].name[j];
                    }
                    path[p] = '\0';
                }
                if (mya_fs_read(path, buf, sizeof(buf) - 1u, &read_size) == 0) {
                    found = 1u;
                    break;
                }
            }
        }

        if (!found) {
            if (build_manifest_path("/boot/cmd", cmd_key, path, sizeof(path)) != 0) {
                return -1;
            }
            if (mya_fs_read(path, buf, sizeof(buf) - 1u, &read_size) != 0) {
                count = 0;
                if (mya_fs_list("/boot/cmd", entries, 128u, &count) != 0) {
                    return -1;
                }
                for (uint32_t ei = 0; ei < count; ei++) {
                    size_t name_len;
                    if (entries[ei].type != MYAOS_NODE_FILE || !str_ends_with_ci(entries[ei].name, ".cmd")) {
                        continue;
                    }
                    name_len = str_len(entries[ei].name);
                    if (name_len <= 4u) {
                        continue;
                    }
                    str_copy(path, entries[ei].name, sizeof(path));
                    path[name_len - 4u] = '\0';
                    if (!str_eq_ci(path, cmd_key)) {
                        continue;
                    }
                    str_copy(path, "/boot/cmd/", sizeof(path));
                    {
                        size_t p = str_len(path);
                        for (size_t j = 0; entries[ei].name[j] && p + 1 < sizeof(path); j++) {
                            path[p++] = entries[ei].name[j];
                        }
                        path[p] = '\0';
                    }
                    if (mya_fs_read(path, buf, sizeof(buf) - 1u, &read_size) == 0) {
                        found = 1u;
                        break;
                    }
                }
                if (!found) {
                    return -1;
                }
            }
        }
    }
    buf[read_size] = 0;

    while (i < read_size && buf[i] != '\n' && pos + 1 < out_size) {
        out_exec[pos++] = (char)buf[i++];
    }
    out_exec[pos] = '\0';
    if (str_starts_with(out_exec, "exec=")) {
        str_copy(out_exec, out_exec + 5, out_size);
    }

    if (out_exec[0]) {
        for (uint32_t ci = 0; ci < MSH_MANIFEST_CACHE_MAX; ci++) {
            if (!g_manifest_cache[ci].used || str_eq(g_manifest_cache[ci].name, cmd_key)) {
                g_manifest_cache[ci].used = 1u;
                str_copy(g_manifest_cache[ci].name, cmd_key, sizeof(g_manifest_cache[ci].name));
                str_copy(g_manifest_cache[ci].exec_path, out_exec, sizeof(g_manifest_cache[ci].exec_path));
                break;
            }
        }
    }

    return out_exec[0] ? 0 : -1;
}

static int wait_foreground_child(int32_t pid) {
    int32_t exit_code = 0;
    uint8_t ctrl_c_sent = 0;

    for (;;) {
        int rc = mya_proc_wait_poll(pid, &exit_code);
        if (rc < 0) {
            mya_putln("msh: wait failed");
            return 125;
        }
        if (rc > 0) {
            return exit_code;
        }

        {
            int ch = mya_console_readchar();
            if (!ctrl_c_sent && ch == 3) {
                ctrl_c_sent = 1;
                (void)mya_proc_kill(pid, 130);
                mya_puts("^C\n");
            }
        }

        mya_proc_yield();
    }
}

static int build_boot_exec_path(const char* exec_path, char* out, size_t out_size) {
    const char* suffix;
    size_t pos = 0;

    if (!exec_path || !out || out_size == 0 || !str_starts_with(exec_path, "/bin/")) {
        return -1;
    }

    suffix = exec_path + 5;
    str_copy(out, "/boot/bin/", out_size);
    pos = str_len(out);
    for (size_t i = 0; suffix[i]; i++) {
        if (pos + 1 >= out_size) {
            return -1;
        }
        out[pos++] = suffix[i];
    }
    out[pos] = '\0';
    return 0;
}

static int spawn_command(const msh_cmd_t* cmd) {
    char exec_path[MYAOS_PATH_MAX];
    char resolved_exec[MYAOS_PATH_MAX];
    int32_t pid = -1;
    int rc;
    uint8_t flags = 0;

    if (!cmd || cmd->argc <= 0) {
        return 0;
    }

    if (str_contains_char(cmd->argv[0], '/') || str_ends_with(cmd->argv[0], ".elf")) {
        str_copy(exec_path, cmd->argv[0], sizeof(exec_path));
    } else if (read_manifest_exec(cmd->argv[0], exec_path, sizeof(exec_path)) != 0) {
        mya_puts("msh: unknown command: ");
        mya_putln(cmd->argv[0]);
        return 127;
    }

    if (cmd->background) {
        flags |= MYAOS_SPAWN_BACKGROUND;
    }

    str_copy(resolved_exec, exec_path, sizeof(resolved_exec));
    rc = mya_proc_spawn(exec_path, cmd->argc, (const char* const*)cmd->argv, flags, &pid);
    if (rc != 0 && build_boot_exec_path(exec_path, resolved_exec, sizeof(resolved_exec)) == 0) {
        rc = mya_proc_spawn(resolved_exec, cmd->argc, (const char* const*)cmd->argv, flags, &pid);
    }
    if (rc != 0) {
        mya_putln("msh: spawn failed");
        return 126;
    }

    if (cmd->background) {
        mya_puts("msh: started pid ");
        mya_put_u32((uint32_t)pid);
        mya_puts("\n");
        return 0;
    }

    return wait_foreground_child(pid);
}

static void print_help(void) {
    myaos_dirent_t entries[128];
    uint32_t count = 0;
    const char* manifest_dir = "/cmd";

    mya_putln("msh builtins:");
    mya_putln("  help        show this message");
    mya_putln("  exit        exit msh");
    mya_putln("  go PATH     change directory");
    mya_putln("  cwd         print current directory");
    mya_putln("  load ELF    run explicit ELF path");
    mya_putln("  script.sh   run script file");
    mya_putln("");
    mya_putln("commands from /cmd (or /boot/cmd):");

    if (mya_fs_list(manifest_dir, entries, 128u, &count) != 0 || count == 0u) {
        manifest_dir = "/boot/cmd";
    }
    if (mya_fs_list(manifest_dir, entries, 128u, &count) != 0) {
        mya_putln("  (none)");
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != MYAOS_NODE_FILE) {
            continue;
        }
        {
            char name[MYAOS_NAME_MAX];
            size_t len;
            str_copy(name, entries[i].name, sizeof(name));
            len = str_len(name);
            if (len > 4 && str_ends_with_ci(name, ".cmd")) {
                name[len - 4] = '\0';
            }
            mya_puts("  ");
            mya_putln(name);
        }
    }
}

static int execute_line(char* line, uint8_t* out_should_exit);

static int run_script(const char* path) {
    static uint8_t buf[MSH_SCRIPT_MAX];
    uint32_t read_size = 0;
    size_t line_start = 0;

    if (!path || mya_fs_read(path, buf, sizeof(buf) - 1u, &read_size) != 0) {
        mya_puts("msh: script not found: ");
        mya_putln(path ? path : "(null)");
        return 1;
    }

    buf[read_size] = 0;
    for (size_t i = 0; i <= read_size; i++) {
        if (i == read_size || buf[i] == '\n' || buf[i] == '\r') {
            char line[MSH_INPUT_MAX];
            size_t j = 0;
            uint8_t should_exit = 0;

            while (line_start < i && j + 1 < sizeof(line)) {
                line[j++] = (char)buf[line_start++];
            }
            line[j] = '\0';
            trim_in_place(line);

            if (line[0] && line[0] != '#') {
                int rc = execute_line(line, &should_exit);
                if (should_exit) {
                    return 0;
                }
                if (rc != 0) {
                    return rc;
                }
            }

            while (line_start <= i) {
                line_start++;
            }
        }
    }

    return 0;
}

static int execute_line(char* line, uint8_t* out_should_exit) {
    msh_cmd_t cmd;

    if (!line || !out_should_exit) {
        return -1;
    }
    *out_should_exit = 0;

    trim_in_place(line);
    if (!line[0]) {
        return 0;
    }
    if (tokenize_line(line, &cmd) != 0 || cmd.argc <= 0) {
        mya_putln("msh: parse error");
        return 2;
    }

    if (str_eq(cmd.argv[0], "help")) {
        print_help();
        return 0;
    }
    if (str_eq(cmd.argv[0], "exit")) {
        *out_should_exit = 1;
        return 0;
    }
    if (str_eq(cmd.argv[0], "go")) {
        if (cmd.argc < 2 || mya_fs_chdir(cmd.argv[1]) != 0) {
            mya_putln("msh: go failed");
            return 1;
        }
        return 0;
    }
    if (str_eq(cmd.argv[0], "cwd")) {
        char cwd[MYAOS_PATH_MAX];
        if (mya_fs_getcwd(cwd, sizeof(cwd)) != 0) {
            mya_putln("msh: cwd failed");
            return 1;
        }
        mya_putln(cwd);
        return 0;
    }
    if (str_eq(cmd.argv[0], "load")) {
        msh_cmd_t load_cmd;
        if (cmd.argc < 2) {
            mya_putln("usage: load PATH.elf [args...]");
            return 2;
        }
        load_cmd.background = cmd.background;
        load_cmd.argc = cmd.argc - 1;
        for (int i = 1; i < cmd.argc; i++) {
            load_cmd.argv[i - 1] = cmd.argv[i];
        }
        return spawn_command(&load_cmd);
    }
    if (str_ends_with(cmd.argv[0], ".sh") &&
        (cmd.argv[0][0] == '/' || str_starts_with(cmd.argv[0], "./") || str_starts_with(cmd.argv[0], "../") ||
         str_contains_char(cmd.argv[0], '/'))) {
        return run_script(cmd.argv[0]);
    }

    return spawn_command(&cmd);
}

int program_main(int argc, char** argv) {
    char line[MSH_INPUT_MAX];
    int last_status = 0;
    if (argv == NULL) {
        return 1;
    }

    for (uint32_t i = 0; i < MSH_MANIFEST_CACHE_MAX; i++) {
        g_manifest_cache[i].used = 0u;
        g_manifest_cache[i].name[0] = '\0';
        g_manifest_cache[i].exec_path[0] = '\0';
    }

    mya_putln("msh userspace shell");
    mya_putln("type help");

    if (argc >= 3 && str_eq(argv[1], "-c")) {
        uint8_t should_exit = 0;
        str_copy(line, argv[2], sizeof(line));
        return execute_line(line, &should_exit);
    }
    if (argc >= 2 && str_ends_with(argv[1], ".sh")) {
        return run_script(argv[1]);
    }

    for (;;) {
        char cwd[MYAOS_PATH_MAX];
        uint8_t should_exit = 0;

        if (mya_fs_getcwd(cwd, sizeof(cwd)) != 0) {
            str_copy(cwd, "/", sizeof(cwd));
        }
        mya_puts("[");
        mya_put_u32((uint32_t)last_status);
        mya_puts("] ");
        mya_puts(cwd);
        mya_puts(" msh $ ");

        if (read_line(line, sizeof(line)) < 0) {
            mya_putln("msh: input failed");
            return 1;
        }
        if (line[0] == '\0') {
            continue;
        }

        last_status = execute_line(line, &should_exit);
        if (should_exit) {
            break;
        }
    }

    return 0;
}
