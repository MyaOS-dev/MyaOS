#include "../lib/myaos.h"
#include <stdint.h>

#define SUDO_MANIFEST_MAX 256u
#define SUDO_ARGV_MAX 32

static size_t str_len(const char* s) {
    size_t n = 0u;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int str_eq(const char* a, const char* b) {
    size_t i = 0u;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0u;
    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i + 1u < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int str_eq_ci(const char* a, const char* b) {
    uint32_t i = 0u;
    while (a[i] && b[i]) {
        if (to_lower_char(a[i]) != to_lower_char(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
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

static int build_manifest_path(const char* base_dir, const char* cmd, char* out, uint32_t out_size) {
    uint32_t pos = 0u;
    if (!base_dir || !cmd || !out || out_size == 0u) {
        return -1;
    }
    while (base_dir[pos] && pos + 1u < out_size) {
        out[pos] = base_dir[pos];
        pos++;
    }
    if (base_dir[pos] != '\0') {
        return -1;
    }
    if (pos == 0u || out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = '/';
    }
    for (uint32_t i = 0u; cmd[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = cmd[i];
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

static int resolve_manifest_exec(const char* cmd, char* out_exec, uint32_t out_size) {
    char cmd_key[MYAOS_NAME_MAX];
    char path[MYAOS_PATH_MAX];
    uint8_t buf[SUDO_MANIFEST_MAX];
    uint32_t read_size = 0u;
    uint32_t pos = 0u;

    if (!cmd || !out_exec || out_size == 0u) {
        return -1;
    }

    str_copy(cmd_key, cmd, sizeof(cmd_key));
    for (uint32_t i = 0u; cmd_key[i]; i++) {
        cmd_key[i] = to_lower_char(cmd_key[i]);
    }

    if (build_manifest_path("/cmd", cmd_key, path, sizeof(path)) != 0 ||
        mya_fs_read(path, buf, sizeof(buf) - 1u, &read_size) != 0) {
        if (build_manifest_path("/boot/cmd", cmd_key, path, sizeof(path)) != 0 ||
            mya_fs_read(path, buf, sizeof(buf) - 1u, &read_size) != 0) {
            return -1;
        }
    }

    buf[read_size] = 0u;
    while (pos < read_size && buf[pos] != '\n' && pos + 1u < out_size) {
        out_exec[pos] = (char)buf[pos];
        pos++;
    }
    out_exec[pos] = '\0';
    if (str_eq(out_exec, "") || out_exec[0] == '#') {
        return -1;
    }
    if (str_eq(out_exec, "exec=")) {
        return -1;
    }
    if (out_exec[0] == 'e' && out_exec[1] == 'x' && out_exec[2] == 'e' && out_exec[3] == 'c' && out_exec[4] == '=') {
        str_copy(out_exec, out_exec + 5, out_size);
    }
    return out_exec[0] ? 0 : -1;
}

static const char* uid_to_user(uint32_t uid) {
    if (uid == 0u) {
        return "root";
    }
    if (uid == 1000u) {
        return "user";
    }
    if (uid == 1001u) {
        return "guest";
    }
    return "user";
}

static int wait_child(int32_t pid) {
    int32_t exit_code = 0;
    for (;;) {
        int rc = mya_proc_wait_poll(pid, &exit_code);
        if (rc < 0) {
            return 125;
        }
        if (rc > 0) {
            return exit_code;
        }
        mya_proc_yield();
    }
}

int program_main(int argc, char** argv) {
    const char* exec_path = NULL;
    const char* child_argv[SUDO_ARGV_MAX];
    char resolved_exec[MYAOS_PATH_MAX];
    uint32_t old_uid;
    const char* old_user;
    int switched = 0;
    int32_t pid = -1;
    int child_argc;
    int rc;

    if (argc < 2) {
        mya_putln("usage: sudo <command> [args...]");
        return 1;
    }

    old_uid = mya_sec_whoami();
    old_user = uid_to_user(old_uid);
    if (old_uid != 0u) {
        if (mya_sec_login("root") != 0) {
            mya_putln("sudo: failed to switch to root");
            return 1;
        }
        switched = 1;
    }

    if (str_ends_with(argv[1], ".elf") || argv[1][0] == '/' ||
        str_ends_with_ci(argv[1], ".ELF")) {
        exec_path = argv[1];
    } else {
        if (resolve_manifest_exec(argv[1], resolved_exec, sizeof(resolved_exec)) != 0) {
            mya_putln("sudo: command not found");
            if (switched) {
                (void)mya_sec_login(old_user);
            }
            return 127;
        }
        exec_path = resolved_exec;
    }

    child_argc = argc - 1;
    if (child_argc <= 0 || child_argc > SUDO_ARGV_MAX) {
        mya_putln("sudo: too many arguments");
        if (switched) {
            (void)mya_sec_login(old_user);
        }
        return 1;
    }
    for (int i = 0; i < child_argc; i++) {
        child_argv[i] = argv[i + 1];
    }

    if (mya_proc_spawn(exec_path, child_argc, child_argv, 0u, &pid) != 0) {
        mya_putln("sudo: spawn failed");
        if (switched) {
            (void)mya_sec_login(old_user);
        }
        return 126;
    }

    rc = wait_child(pid);
    if (switched) {
        (void)mya_sec_login(old_user);
    }
    return rc;
}
