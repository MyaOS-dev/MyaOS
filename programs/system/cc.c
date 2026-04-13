#include "../lib/myaos.h"

#define CC_ARGV_MAX 32

static int wait_child(int32_t pid) {
    int32_t code = 0;
    for (;;) {
        int rc = mya_proc_wait_poll(pid, &code);
        if (rc < 0) {
            return 125;
        }
        if (rc > 0) {
            return code;
        }
        mya_proc_yield();
    }
}

static int run_compiler(const char* compiler_path, int argc, char** argv) {
    const char* child_argv[CC_ARGV_MAX];
    int32_t pid = -1;
    int child_argc = argc;

    if (child_argc <= 0 || child_argc > CC_ARGV_MAX) {
        return 2;
    }
    for (int i = 0; i < child_argc; i++) {
        child_argv[i] = argv[i];
    }
    if (mya_proc_spawn(compiler_path, child_argc, child_argv, 0u, &pid) != 0) {
        return -1;
    }
    return wait_child(pid);
}

int program_main(int argc, char** argv) {
    int rc;

    if (argc < 2) {
        mya_putln("usage: cc <args...>");
        mya_putln("tries /bin/tcc.elf then /boot/bin/tcc.elf");
        return 1;
    }

    rc = run_compiler("/bin/tcc.elf", argc, argv);
    if (rc >= 0) {
        return rc;
    }
    rc = run_compiler("/boot/bin/tcc.elf", argc, argv);
    if (rc >= 0) {
        return rc;
    }

    mya_putln("cc: no compiler binary found");
    mya_putln("hint: install compiler package providing /bin/tcc.elf");
    return 127;
}
