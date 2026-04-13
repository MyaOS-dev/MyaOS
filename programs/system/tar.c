#include "../lib/myaos.h"
#include <stdint.h>

#define TAR_ARGV_MAX 128

static int file_readable(const char* path) {
    int32_t fd = -1;
    if (mya_posix_open(path, MYAOS_POSIX_O_RDONLY, &fd) != 0 || fd <= 0) {
        return 0;
    }
    (void)mya_posix_close(fd);
    return 1;
}

static const char* resolve_busybox(void) {
    static const char* const candidates[] = {
        "/boot/assets/busybox",
        "/assets/busybox",
        "/boot/bin/busybox",
        "/bin/busybox",
    };

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (file_readable(candidates[i])) {
            return candidates[i];
        }
    }
    return NULL;
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
    const char* busybox;
    const char* child_argv[TAR_ARGV_MAX];
    int child_argc;
    int32_t pid = -1;

    if (argc < 2) {
        mya_putln("usage: tar <busybox-tar-args...>");
        mya_putln("example: tar -xJf /boot/assets/coreutils-9.5.tar.xz -C /ram");
        return 1;
    }

    busybox = resolve_busybox();
    if (!busybox) {
        mya_putln("tar: busybox not found (/boot/assets/busybox)");
        return 1;
    }

    child_argc = argc + 1;
    if (child_argc >= TAR_ARGV_MAX) {
        mya_putln("tar: too many arguments");
        return 1;
    }

    child_argv[0] = "busybox";
    child_argv[1] = "tar";
    for (int i = 1; i < argc; i++) {
        child_argv[i + 1] = argv[i];
    }

    if (mya_proc_spawn_linux(busybox, child_argc, child_argv, &pid) != 0 || pid <= 0) {
        mya_putln("tar: spawn failed");
        return 1;
    }
    return wait_child(pid);
}
