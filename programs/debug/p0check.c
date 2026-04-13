#include "../lib/myaos.h"
#include <stdint.h>

#define PROC_LIST_MAX 64u
#define WAIT_POLL_TICKS 400u

static int process_present(const myaos_proc_info_t* procs, uint32_t count, int32_t pid) {
    if (!procs || pid <= 0) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if ((int32_t)procs[i].pid == pid) {
            return 1;
        }
    }
    return 0;
}

static int wait_child_exit(int32_t pid, int32_t* out_exit_code) {
    int32_t exit_code = 0;

    for (uint32_t i = 0; i < WAIT_POLL_TICKS; i++) {
        int rc = mya_proc_wait_poll(pid, &exit_code);
        if (rc > 0) {
            if (out_exit_code) {
                *out_exit_code = exit_code;
            }
            return 0;
        }
        if (rc < 0) {
            return -1;
        }
        mya_proc_sleep(1u);
    }
    return -1;
}

int program_main(int argc, char** argv) {
    myaos_proc_info_t procs[PROC_LIST_MAX];
    myaos_meminfo_t mem_before;
    myaos_meminfo_t mem_after;
    const char* child_argv[2] = {"/bin/pause.elf", "2000"};
    uint32_t count = 0u;
    int32_t self_pid = 0;
    int32_t child_pid = -1;
    int32_t child_exit = 0;
    void* mapped = NULL;

    (void)argc;
    (void)argv;

    self_pid = mya_proc_getpid();
    if (self_pid <= 0) {
        mya_putln("p0check: bad current pid");
        return 1;
    }

    if (mya_proc_list(procs, PROC_LIST_MAX, &count) != 0 || !process_present(procs, count, self_pid)) {
        mya_putln("p0check: proc list check failed");
        return 1;
    }

    if (mya_proc_spawn("/bin/pause.elf", 2, child_argv, 0u, &child_pid) != 0 || child_pid <= 0) {
        mya_putln("p0check: failed to spawn pause child");
        return 1;
    }

    if (mya_proc_list(procs, PROC_LIST_MAX, &count) != 0 || !process_present(procs, count, child_pid)) {
        (void)mya_proc_kill(child_pid, 143);
        mya_putln("p0check: spawned child is absent in proc list");
        return 1;
    }

    if (mya_proc_kill(child_pid, 143) != 0) {
        mya_putln("p0check: kill check failed");
        return 1;
    }
    if (wait_child_exit(child_pid, &child_exit) != 0 || child_exit != 143) {
        mya_putln("p0check: wait/exit-code check failed");
        return 1;
    }

    if (mya_meminfo(&mem_before) != 0 || mem_before.pmm_managed_pages == 0u ||
        mem_before.heap_bytes_capacity < mem_before.heap_bytes_used) {
        mya_putln("p0check: meminfo baseline check failed");
        return 1;
    }

    mapped = mya_mem_map(4096u, MYAOS_MEM_MAP_WRITABLE);
    if (!mapped) {
        mya_putln("p0check: mem_map failed");
        return 1;
    }
    ((volatile uint8_t*)mapped)[0] = 0x5Au;

    if (mya_meminfo(&mem_after) != 0 || mem_after.pmm_managed_pages == 0u ||
        mem_after.heap_bytes_capacity < mem_after.heap_bytes_used) {
        (void)mya_mem_unmap(mapped);
        mya_putln("p0check: meminfo after map failed");
        return 1;
    }
    if (mya_mem_unmap(mapped) != 0) {
        mya_putln("p0check: mem_unmap failed");
        return 1;
    }

    mya_putln("p0check: ok");
    return 0;
}
