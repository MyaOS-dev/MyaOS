#include "../lib/myaos.h"
#include <stdint.h>

#define LXRUN_ARGV_MAX 64

static void mem_zero(void* ptr, uint32_t len) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < len; i++) {
        p[i] = 0u;
    }
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

static int str_contains_slash(const char* s) {
    if (!s) {
        return 0;
    }
    for (uint32_t i = 0u; s[i] != '\0'; i++) {
        if (s[i] == '/') {
            return 1;
        }
    }
    return 0;
}

static int file_readable(const char* path) {
    int32_t fd = -1;
    if (mya_posix_open(path, MYAOS_POSIX_O_RDONLY, &fd) != 0 || fd <= 0) {
        return 0;
    }
    (void)mya_posix_close(fd);
    return 1;
}

static int resolve_linux_binary_path(const char* input, char* out_path, uint32_t out_size) {
    static const char* const prefixes[] = {
        "/boot/assets/",
        "/assets/",
        "/",
        "/boot/bin/",
        "/bin/",
    };
    char candidate[MYAOS_PATH_MAX];

    if (!input || !out_path || out_size == 0u) {
        return -1;
    }

    if (str_contains_slash(input)) {
        str_copy(out_path, input, out_size);
        return 0;
    }

    if (file_readable(input)) {
        str_copy(out_path, input, out_size);
        return 0;
    }

    for (uint32_t p = 0u; p < (uint32_t)(sizeof(prefixes) / sizeof(prefixes[0])); p++) {
        uint32_t pos = 0u;
        const char* prefix = prefixes[p];

        mem_zero(candidate, (uint32_t)sizeof(candidate));
        while (prefix[pos] != '\0' && pos + 1u < (uint32_t)sizeof(candidate)) {
            candidate[pos] = prefix[pos];
            pos++;
        }
        for (uint32_t i = 0u; input[i] != '\0' && pos + 1u < (uint32_t)sizeof(candidate); i++) {
            candidate[pos++] = input[i];
        }
        candidate[pos] = '\0';
        if (file_readable(candidate)) {
            str_copy(out_path, candidate, out_size);
            return 0;
        }
    }

    str_copy(out_path, input, out_size);
    return 0;
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

static int maybe_report_linux_exception(int exit_code) {
    int vector;

    if (exit_code > -128 || exit_code < -159) {
        return 0;
    }
    vector = (-exit_code) - 0x80;
    if (vector < 0 || vector >= 32) {
        return 0;
    }

    mya_puts("lxrun: linux task terminated by CPU exception vector ");
    mya_put_u32((uint32_t)vector);
    mya_putln("");
    if (vector == 14) {
        mya_putln("lxrun: page fault in userspace (check resolved binary/interpreter pair)");
    }
    return 1;
}

int program_main(int argc, char** argv) {
    const char* child_argv[LXRUN_ARGV_MAX];
    char exec_path[MYAOS_PATH_MAX];
    myaos_spawn_opts_t opts;
    int32_t pid = -1;
    int child_argc;

    if (argc < 2) {
        mya_putln("usage: lxrun <linux_elf> [args...]");
        return 1;
    }

    child_argc = argc - 1;
    if (child_argc <= 0 || child_argc >= LXRUN_ARGV_MAX) {
        mya_putln("lxrun: too many arguments");
        return 1;
    }

    for (int i = 0; i < child_argc; i++) {
        child_argv[i] = argv[i + 1];
    }
    if (resolve_linux_binary_path(argv[1], exec_path, (uint32_t)sizeof(exec_path)) != 0) {
        mya_putln("lxrun: invalid path");
        return 1;
    }
    child_argv[0] = exec_path;

    mem_zero(&opts, (uint32_t)sizeof(opts));
    opts.flags = MYAOS_SPAWN_LINUX;
    if (mya_proc_spawn_ex(exec_path, child_argc, child_argv, &opts, &pid) != 0 || pid <= 0) {
        mya_putln("lxrun: spawn failed");
        return 1;
    }

    {
        int exit_code = wait_child(pid);
        (void)maybe_report_linux_exception(exit_code);
        return exit_code;
    }
}
