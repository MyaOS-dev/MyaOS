#include "../lib/myaos.h"

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
    int32_t pid = -1;
    const char* child_argv[4];
    int child_argc = 2;
    const char* index_path = "/repo/index.pkg";

    if (argc >= 2) {
        index_path = argv[1];
    }

    child_argv[0] = "pkg";
    child_argv[1] = "upgrade";
    child_argv[2] = index_path;
    child_argv[3] = NULL;

    child_argc = 3;
    if (mya_proc_spawn("/bin/pkg.elf", child_argc, child_argv, 0u, &pid) != 0) {
        mya_putln("update: failed to start pkg");
        return 1;
    }

    return wait_child(pid);
}
