#include "../lib/myaos.h"

#define DBG_ARGV_MAX 24

typedef struct {
    const char* name;
    const char* exec_path;
} dbg_map_t;

static const dbg_map_t g_map[] = {
    { "proc", "/bin/tasks.elf" },
    { "tree", "/bin/proctree.elf" },
    { "mem", "/bin/meminfo.elf" },
    { "net", "/bin/netstat.elf" },
    { "log", "/bin/log.elf" },
    { "sched", "/bin/sched.elf" },
};

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

static const char* map_exec(const char* name) {
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_map) / sizeof(g_map[0])); i++) {
        if (mya_streq(g_map[i].name, name)) {
            return g_map[i].exec_path;
        }
    }
    return NULL;
}

int program_main(int argc, char** argv) {
    const char* exec_path;
    const char* child_argv[DBG_ARGV_MAX];
    int child_argc;
    int32_t pid = -1;

    if (argc < 2) {
        mya_putln("usage: dbg <proc|tree|mem|net|log|sched> [args...]");
        return 1;
    }

    exec_path = map_exec(argv[1]);
    if (!exec_path) {
        mya_putln("dbg: unknown category");
        return 1;
    }

    child_argc = argc - 1;
    if (child_argc <= 0 || child_argc > DBG_ARGV_MAX) {
        mya_putln("dbg: too many arguments");
        return 2;
    }

    for (int i = 0; i < child_argc; i++) {
        child_argv[i] = argv[i + 1];
    }

    if (mya_proc_spawn(exec_path, child_argc, child_argv, 0u, &pid) != 0) {
        mya_putln("dbg: spawn failed");
        return 126;
    }

    return wait_child(pid);
}
