#ifndef SYSCALL_H
#define SYSCALL_H

#include "boot.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include <stdint.h>

typedef struct {
    uint64_t timer_ticks;
    pmm_stats_t pmm;
    heap_stats_t heap;
    paging_stats_t paging;
} syscall_meminfo_t;

typedef struct {
    uint32_t timer_hz;
    uint64_t timer_ticks;
    uint32_t task_count;
    uint32_t current_task;
} syscall_sched_overview_t;

enum {
    SYSCALL_GET_MEMINFO = 1,
    SYSCALL_GET_SCHED_OVERVIEW = 2,
    SYSCALL_GET_TASK_STATS = 3,
    SYSCALL_HALT = 4,
    SYSCALL_REBOOT = 5,
    SYSCALL_SHUTDOWN = 6,
    SYSCALL_RUN_API = 7,
};

void syscall_set_boot_info(boot_info_t* boot);
int64_t syscall_dispatch(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
);

int sys_get_meminfo(syscall_meminfo_t* out);
int sys_get_sched_overview(syscall_sched_overview_t* out);
int sys_get_task_stats(uint32_t index, scheduler_task_stats_t* out);
int sys_run_api(uint32_t api_addr, uint32_t op_code, const char* args);
void sys_halt(void);
void sys_reboot(void);
void sys_shutdown(void);

#endif
