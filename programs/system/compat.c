#include "../lib/myaos.h"
#include <stdint.h>

#define COMPAT_ARGV_MAX 64
#define COMPAT_MANIFEST_MAX 256

static size_t str_len(const char* s) {
    size_t n = 0u;
    while (s && s[n]) {
        n++;
    }
    return n;
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

    while (src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_starts_with(const char* text, const char* prefix) {
    uint32_t i = 0u;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int str_ends_with(const char* text, const char* suffix) {
    size_t text_len;
    size_t suffix_len;

    if (!text || !suffix) {
        return 0;
    }
    text_len = str_len(text);
    suffix_len = str_len(suffix);
    if (suffix_len > text_len) {
        return 0;
    }
    return mya_streq(text + (text_len - suffix_len), suffix);
}

static int str_contains_char(const char* text, char c) {
    uint32_t i = 0u;

    if (!text) {
        return 0;
    }
    while (text[i] != '\0') {
        if (text[i] == c) {
            return 1;
        }
        i++;
    }
    return 0;
}

static int read_manifest_exec_from(const char* base, const char* cmd, char* out_exec, uint32_t out_size) {
    char path[MYAOS_PATH_MAX];
    uint8_t buf[COMPAT_MANIFEST_MAX];
    uint32_t read_size = 0u;
    uint32_t pos = 0u;
    uint32_t out_pos = 0u;
    uint32_t i = 0u;

    if (!base || !cmd || !out_exec || out_size == 0u) {
        return -1;
    }

    path[0] = '\0';
    str_copy(path, base, (uint32_t)sizeof(path));
    pos = (uint32_t)str_len(path);
    if (pos == 0u || path[pos - 1u] != '/') {
        if (pos + 1u >= (uint32_t)sizeof(path)) {
            return -1;
        }
        path[pos++] = '/';
    }
    for (uint32_t j = 0u; cmd[j] != '\0'; j++) {
        if (pos + 1u >= (uint32_t)sizeof(path)) {
            return -1;
        }
        path[pos++] = cmd[j];
    }
    if (pos + 4u >= (uint32_t)sizeof(path)) {
        return -1;
    }
    path[pos++] = '.';
    path[pos++] = 'c';
    path[pos++] = 'm';
    path[pos++] = 'd';
    path[pos] = '\0';

    if (mya_fs_read(path, buf, (uint32_t)sizeof(buf) - 1u, &read_size) != 0) {
        return -1;
    }
    buf[read_size] = 0u;

    while (i < read_size && buf[i] != '\n' && out_pos + 1u < out_size) {
        out_exec[out_pos++] = (char)buf[i++];
    }
    out_exec[out_pos] = '\0';

    if (str_starts_with(out_exec, "exec=")) {
        str_copy(out_exec, out_exec + 5, out_size);
    }
    return out_exec[0] ? 0 : -1;
}

static int resolve_exec(const char* cmd, char* out_exec, uint32_t out_size) {
    if (!cmd || !out_exec || out_size == 0u) {
        return -1;
    }
    out_exec[0] = '\0';

    if (str_contains_char(cmd, '/') || str_ends_with(cmd, ".elf")) {
        str_copy(out_exec, cmd, out_size);
        return out_exec[0] ? 0 : -1;
    }

    if (read_manifest_exec_from("/cmd", cmd, out_exec, out_size) == 0) {
        return 0;
    }
    if (read_manifest_exec_from("/boot/cmd", cmd, out_exec, out_size) == 0) {
        return 0;
    }
    return -1;
}

static int wait_child(int32_t pid) {
    int32_t exit_code = 0;
    for (;;) {
        int rc = mya_proc_wait_poll(pid, &exit_code);
        if (rc < 0) {
            return 1;
        }
        if (rc > 0) {
            return exit_code;
        }
        mya_proc_yield();
    }
}

int program_main(int argc, char** argv) {
    char exec_path[MYAOS_PATH_MAX];
    const char* child_argv[COMPAT_ARGV_MAX];
    int32_t pid = -1;
    int child_argc;

    if (argc < 2) {
        mya_putln("usage: compat <command> [args...]");
        return 1;
    }

    mya_putln("compat: legacy shim (deprecated). use direct /cmd command or lxrun.");

    child_argc = argc - 1;
    if (child_argc >= COMPAT_ARGV_MAX) {
        mya_putln("compat: too many arguments");
        return 1;
    }

    if (resolve_exec(argv[1], exec_path, (uint32_t)sizeof(exec_path)) != 0) {
        mya_putln("compat: command not found (manifest/path)");
        return 1;
    }

    child_argv[0] = exec_path;
    for (int i = 1; i < child_argc; i++) {
        child_argv[i] = argv[i + 1];
    }

    if (mya_proc_spawn(exec_path, child_argc, child_argv, 0u, &pid) != 0) {
        mya_putln("compat: spawn failed");
        return 1;
    }

    return wait_child(pid);
}
