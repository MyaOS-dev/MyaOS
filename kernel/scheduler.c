#include "scheduler.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

#define SCHED_MAX_TASKS 8u
#define SCHED_STACK_SIZE 32768u
#define SCHED_INVALID_TASK 0xFFFFFFFFu

typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} isr_context_t;

typedef struct {
    uint8_t used;
    char name[SCHED_TASK_NAME_MAX];
    scheduler_task_fn_t fn;
    void* ctx;
    uint64_t saved_rsp;
    uint64_t run_count;
    uint64_t last_run_tick;
} scheduler_task_t;

static scheduler_task_t g_tasks[SCHED_MAX_TASKS];
static uint8_t g_task_stacks[SCHED_MAX_TASKS][SCHED_STACK_SIZE] __attribute__((aligned(16)));
static uint32_t g_task_count;
static uint32_t g_current_task;
static uint32_t g_slice_ticks;
static uint64_t g_last_switch_tick;
static uint8_t g_started;
static uint16_t g_kernel_cs;
static uint16_t g_kernel_ss;

static inline uint16_t read_cs(void) {
    uint16_t cs;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static inline uint16_t read_ss(void) {
    uint16_t ss;
    __asm__ __volatile__("mov %%ss, %0" : "=r"(ss));
    return ss;
}

static inline uint64_t read_rflags(void) {
    uint64_t rflags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(rflags));
    return rflags;
}

static void mem_zero(void* ptr, size_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void name_copy(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0) {
        return;
    }

    if (!src || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int task_is_valid(uint32_t idx) {
    return idx < SCHED_MAX_TASKS && g_tasks[idx].used;
}

static uint32_t first_task_index(void) {
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].used) {
            return i;
        }
    }
    return SCHED_INVALID_TASK;
}

static uint32_t next_task_index(uint32_t from) {
    if (g_task_count == 0) {
        return SCHED_INVALID_TASK;
    }

    for (uint32_t step = 1; step <= SCHED_MAX_TASKS; step++) {
        uint32_t idx = (from + step) % SCHED_MAX_TASKS;
        if (g_tasks[idx].used) {
            return idx;
        }
    }

    return SCHED_INVALID_TASK;
}

static int task_rsp_is_valid(uint32_t task_idx, uint64_t rsp) {
    if (task_idx >= SCHED_MAX_TASKS) {
        return 0;
    }

    uint64_t stack_base = (uint64_t)(uintptr_t)&g_task_stacks[task_idx][0];
    uint64_t stack_top = stack_base + (uint64_t)SCHED_STACK_SIZE;
    if (stack_top < stack_base) {
        return 0;
    }

    if ((rsp & 0x7ULL) != 0) {
        return 0;
    }
    if (rsp < stack_base) {
        return 0;
    }
    if (rsp > stack_top - (uint64_t)sizeof(isr_context_t)) {
        return 0;
    }
    return 1;
}

static uint64_t make_initial_context(uint32_t task_idx) {
    if (task_idx >= SCHED_MAX_TASKS) {
        return 0;
    }

    uint64_t stack_base = (uint64_t)(uintptr_t)&g_task_stacks[task_idx][0];
    uint64_t stack_top = stack_base + (uint64_t)SCHED_STACK_SIZE;
    stack_top &= ~0xFULL;

    isr_context_t* frame = (isr_context_t*)(uintptr_t)(stack_top - sizeof(isr_context_t));
    mem_zero(frame, sizeof(*frame));

    frame->vector = 32u;
    frame->error_code = 0u;
    frame->rip = (uint64_t)(uintptr_t)scheduler_task_bootstrap;
    frame->cs = g_kernel_cs;
    frame->rflags = read_rflags() | 0x200ULL;
    frame->rsp = stack_top;
    frame->ss = g_kernel_ss;

    return (uint64_t)(uintptr_t)frame;
}

static __attribute__((noreturn)) void scheduler_restore_and_enter(uint64_t rsp) {
    __asm__ __volatile__(
        "mov %0, %%rsp\n"
        "pop %%rax\n"
        "pop %%rbx\n"
        "pop %%rcx\n"
        "pop %%rdx\n"
        "pop %%rsi\n"
        "pop %%rdi\n"
        "pop %%rbp\n"
        "pop %%r8\n"
        "pop %%r9\n"
        "pop %%r10\n"
        "pop %%r11\n"
        "pop %%r12\n"
        "pop %%r13\n"
        "pop %%r14\n"
        "pop %%r15\n"
        "add $16, %%rsp\n"
        "iretq\n"
        :
        : "r"(rsp)
        : "memory"
    );

    __builtin_unreachable();
}

void scheduler_init(uint32_t slice_ticks) {
    if (slice_ticks == 0) {
        slice_ticks = 1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        g_tasks[i].used = 0;
        g_tasks[i].name[0] = '\0';
        g_tasks[i].fn = NULL;
        g_tasks[i].ctx = NULL;
        g_tasks[i].saved_rsp = 0;
        g_tasks[i].run_count = 0;
        g_tasks[i].last_run_tick = 0;
    }

    g_task_count = 0;
    g_current_task = SCHED_INVALID_TASK;
    g_slice_ticks = slice_ticks;
    g_last_switch_tick = 0;
    g_started = 0;
    g_kernel_cs = read_cs();
    g_kernel_ss = read_ss();
}

int scheduler_add_task(const char* name, scheduler_task_fn_t fn, void* ctx) {
    if (!fn || g_task_count >= SCHED_MAX_TASKS) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].used) {
            continue;
        }

        g_tasks[i].used = 1;
        name_copy(g_tasks[i].name, name ? name : "task", sizeof(g_tasks[i].name));
        g_tasks[i].fn = fn;
        g_tasks[i].ctx = ctx;
        g_tasks[i].run_count = 0;
        g_tasks[i].last_run_tick = 0;
        g_tasks[i].saved_rsp = make_initial_context(i);

        if (g_tasks[i].saved_rsp == 0) {
            g_tasks[i].used = 0;
            return -1;
        }

        g_task_count++;
        return (int)i;
    }

    return -1;
}

uint64_t scheduler_on_timer_interrupt(uint64_t current_rsp, uint64_t tick_now) {
    if (!g_started || g_task_count == 0 || current_rsp == 0) {
        return current_rsp;
    }

    if (!task_is_valid(g_current_task)) {
        g_current_task = first_task_index();
        g_last_switch_tick = tick_now;
        return current_rsp;
    }

    if (task_rsp_is_valid(g_current_task, current_rsp)) {
        g_tasks[g_current_task].saved_rsp = current_rsp;
    } else {
        return current_rsp;
    }

    if (tick_now - g_last_switch_tick < g_slice_ticks) {
        return current_rsp;
    }

    uint32_t next = next_task_index(g_current_task);
    if (!task_is_valid(next) || next == g_current_task) {
        g_last_switch_tick = tick_now;
        return current_rsp;
    }

    g_current_task = next;
    g_last_switch_tick = tick_now;
    g_tasks[next].run_count++;
    g_tasks[next].last_run_tick = tick_now;

    if (!task_rsp_is_valid(next, g_tasks[next].saved_rsp)) {
        return current_rsp;
    }

    return g_tasks[next].saved_rsp;
}

void scheduler_task_bootstrap(void) {
    for (;;) {
        uint32_t idx = g_current_task;
        if (!task_is_valid(idx) || !g_tasks[idx].fn) {
            __asm__ __volatile__("hlt");
            continue;
        }

        g_tasks[idx].fn(g_tasks[idx].ctx);
    }
}

void scheduler_run(void) {
    if (g_task_count == 0) {
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    uint32_t first = first_task_index();
    if (!task_is_valid(first)) {
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    __asm__ __volatile__("cli");

    g_current_task = first;
    g_started = 1;
    g_last_switch_tick = timer_ticks();
    g_tasks[first].run_count++;
    g_tasks[first].last_run_tick = g_last_switch_tick;

    if (!task_rsp_is_valid(first, g_tasks[first].saved_rsp)) {
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    scheduler_restore_and_enter(g_tasks[first].saved_rsp);
}

uint32_t scheduler_task_count(void) {
    return g_task_count;
}

uint32_t scheduler_current_task(void) {
    if (!task_is_valid(g_current_task)) {
        return SCHED_INVALID_TASK;
    }
    return g_current_task;
}

int scheduler_get_task_stats(uint32_t index, scheduler_task_stats_t* out) {
    if (!out || !task_is_valid(index)) {
        return -1;
    }

    name_copy(out->name, g_tasks[index].name, sizeof(out->name));
    out->run_count = g_tasks[index].run_count;
    out->last_run_tick = g_tasks[index].last_run_tick;
    return 0;
}
