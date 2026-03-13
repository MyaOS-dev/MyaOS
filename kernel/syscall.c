#include "syscall.h"
#include "power.h"
#include "shell.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

static boot_info_t* g_boot;

void syscall_set_boot_info(boot_info_t* boot) {
    g_boot = boot;
}

int64_t syscall_dispatch(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
) {
    (void)arg2;
    (void)arg3;
    (void)arg4;

    switch (number) {
    case SYSCALL_GET_MEMINFO: {
        syscall_meminfo_t* out = (syscall_meminfo_t*)(uintptr_t)arg0;
        if (!out) {
            return -1;
        }

        out->timer_ticks = timer_ticks();
        pmm_get_stats(&out->pmm);
        heap_get_stats(&out->heap);
        paging_get_stats(&out->paging);
        return 0;
    }
    case SYSCALL_GET_SCHED_OVERVIEW: {
        syscall_sched_overview_t* out = (syscall_sched_overview_t*)(uintptr_t)arg0;
        if (!out) {
            return -1;
        }

        out->timer_hz = timer_hz();
        out->timer_ticks = timer_ticks();
        out->task_count = scheduler_task_count();
        out->current_task = scheduler_current_task();
        return 0;
    }
    case SYSCALL_GET_TASK_STATS: {
        scheduler_task_stats_t* out = (scheduler_task_stats_t*)(uintptr_t)arg1;
        if (!out) {
            return -1;
        }

        return scheduler_get_task_stats((uint32_t)arg0, out);
    }
    case SYSCALL_HALT:
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    case SYSCALL_REBOOT:
        if (g_boot) {
            power_reboot(g_boot);
        }
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    case SYSCALL_SHUTDOWN:
        if (g_boot) {
            power_shutdown(g_boot);
        }
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    case SYSCALL_RUN_API:
        return shell_syscall_run_api(
            (uint32_t)arg0,
            (uint32_t)arg1,
            (const char*)(uintptr_t)arg2
        );
    default:
        return -1;
    }
}

static int64_t syscall_invoke(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
) {
    uint64_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4)
        : "cc", "memory"
    );
    return (int64_t)ret;
}

int sys_get_meminfo(syscall_meminfo_t* out) {
    return (int)syscall_invoke(SYSCALL_GET_MEMINFO, (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

int sys_get_sched_overview(syscall_sched_overview_t* out) {
    return (int)syscall_invoke(SYSCALL_GET_SCHED_OVERVIEW, (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

int sys_get_task_stats(uint32_t index, scheduler_task_stats_t* out) {
    return (int)syscall_invoke(
        SYSCALL_GET_TASK_STATS,
        (uint64_t)index,
        (uint64_t)(uintptr_t)out,
        0,
        0,
        0
    );
}

int sys_run_api(uint32_t api_addr, uint32_t op_code, const char* args) {
    return (int)syscall_invoke(
        SYSCALL_RUN_API,
        (uint64_t)api_addr,
        (uint64_t)op_code,
        (uint64_t)(uintptr_t)args,
        0,
        0
    );
}

void sys_halt(void) {
    (void)syscall_invoke(SYSCALL_HALT, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

void sys_reboot(void) {
    (void)syscall_invoke(SYSCALL_REBOOT, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

void sys_shutdown(void) {
    (void)syscall_invoke(SYSCALL_SHUTDOWN, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
