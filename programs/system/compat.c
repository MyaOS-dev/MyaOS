#include "../lib/myaos.h"
#include <stdint.h>

#define COMPAT_ARGV_MAX 32

typedef struct {
    const char* name;
    const char* exec;
} compat_map_t;

static const compat_map_t g_map[] = {
    { "ls", "/bin/list.elf" },
    { "cat", "/bin/show.elf" },
    { "pwd", "/bin/whereami.elf" },
    { "mkdir", "/bin/mkfolder.elf" },
    { "rm", "/bin/del.elf" },
    { "touch", "/bin/newfile.elf" },
    { "echo", "/bin/say.elf" },
    { "ps", "/bin/tasks.elf" },
    { "id", "/bin/whoami.elf" },
    { "sleep", "/bin/pause.elf" },
    { "uname", "/bin/uname.elf" },
    { "sh", "/bin/msh.elf" },
};

static const char* map_exec(const char* cmd) {
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_map) / sizeof(g_map[0])); i++) {
        if (mya_streq(g_map[i].name, cmd)) {
            return g_map[i].exec;
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
    const char* exec_path;
    const char* child_argv[COMPAT_ARGV_MAX];
    int32_t pid = -1;
    int child_argc;

    if (argc < 2) {
        mya_putln("usage: compat <command> [args...]");
        return 1;
    }

    exec_path = map_exec(argv[1]);
    if (!exec_path) {
        mya_putln("compat: unsupported command");
        return 1;
    }

    child_argc = argc - 1;
    if (child_argc >= COMPAT_ARGV_MAX) {
        mya_putln("compat: too many arguments");
        return 1;
    }

    for (int i = 0; i < child_argc; i++) {
        child_argv[i] = argv[i + 1];
    }

    if (mya_proc_spawn(exec_path, child_argc, child_argv, 0u, &pid) != 0) {
        mya_putln("compat: spawn failed");
        return 1;
    }

    return wait_child(pid);
}
