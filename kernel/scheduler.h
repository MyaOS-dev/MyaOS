#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#define SCHED_TASK_NAME_MAX 16

typedef void (*scheduler_task_fn_t)(void* ctx);

typedef struct {
    char name[SCHED_TASK_NAME_MAX];
    uint64_t run_count;
    uint64_t last_run_tick;
} scheduler_task_stats_t;

void scheduler_init(uint32_t slice_ticks);
int scheduler_add_task(const char* name, scheduler_task_fn_t fn, void* ctx);
uint64_t scheduler_on_timer_interrupt(uint64_t current_rsp, uint64_t tick_now);
void scheduler_task_bootstrap(void);
void scheduler_run(void);
uint32_t scheduler_task_count(void);
uint32_t scheduler_current_task(void);
int scheduler_get_task_stats(uint32_t index, scheduler_task_stats_t* out);

#endif
