#include "../lib/myaos.h"

#define PTREE_MAX 64u

static int has_pid(const myaos_proc_info_t* procs, uint32_t count, uint32_t pid) {
    for (uint32_t i = 0; i < count; i++) {
        if (procs[i].pid == pid) {
            return 1;
        }
    }
    return 0;
}

static void print_indent(uint32_t depth) {
    for (uint32_t i = 0u; i < depth; i++) {
        mya_puts("  ");
    }
}

static void print_node(const myaos_proc_info_t* p, uint32_t depth) {
    print_indent(depth);
    mya_put_u32(p->pid);
    mya_puts(" ");
    mya_puts(p->name);
    mya_puts(" ");
    mya_puts(mya_proc_state_name(p->state));
    mya_puts("\n");
}

static void walk_children(
    const myaos_proc_info_t* procs,
    uint32_t count,
    uint32_t pid,
    uint32_t depth,
    uint8_t* visited
) {
    if (depth > 16u) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (procs[i].ppid != pid) {
            continue;
        }
        if (visited[i]) {
            continue;
        }
        visited[i] = 1u;
        print_node(&procs[i], depth);
        walk_children(procs, count, procs[i].pid, depth + 1u, visited);
    }
}

int program_main(int argc, char** argv) {
    myaos_proc_info_t procs[PTREE_MAX];
    uint8_t visited[PTREE_MAX];
    uint32_t count = 0u;
    (void)argc;
    (void)argv;

    for (uint32_t i = 0; i < PTREE_MAX; i++) {
        visited[i] = 0u;
    }

    if (mya_proc_list(procs, PTREE_MAX, &count) != 0) {
        mya_putln("tree: failed to list processes");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (procs[i].ppid != 0u && has_pid(procs, count, procs[i].ppid)) {
            continue;
        }
        if (visited[i]) {
            continue;
        }
        visited[i] = 1u;
        print_node(&procs[i], 0u);
        walk_children(procs, count, procs[i].pid, 1u, visited);
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!visited[i]) {
            print_node(&procs[i], 0u);
        }
    }

    return 0;
}
