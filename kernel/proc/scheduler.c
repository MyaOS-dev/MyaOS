#include "scheduler.h"
#include "elf.h"
#include "gdt.h"
#include "heap.h"
#include "net.h"
#include "paging.h"
#include "swap.h"
#include "timer.h"
#include "vfs.h"

#define SCHED_MAX_PROCS 32u
#define SCHED_STACK_SIZE 65536u
#define SCHED_INVALID_SLOT 0xFFFFFFFFu
#define PAGE_SIZE 4096ULL

#define USER_REGION_BASE 0x0000008000000000ULL
#define USER_REGION_STRIDE 0x0000000010000000ULL
#define USER_TRAMPOLINE_OFFSET 0x00100000ULL
#define USER_IMAGE_BASE_OFFSET 0x00200000ULL
#define USER_ARGV_BASE_OFFSET 0x0C000000ULL
#define USER_ARGV_SIZE 0x00100000ULL
#define USER_STACK_TOP_OFFSET 0x0FF00000ULL
#define USER_STACK_SIZE 0x00100000ULL
#define USER_MMAP_MIN_OFFSET 0x01000000ULL
#define SCHED_MAX_USER_MAPS 32u
#define USER_MAP_FLAG_SWAPPED 0x1u
#define SCHED_MAX_PIPES 32u
#define SCHED_MAX_PIPE_HANDLES 16u
#define SCHED_PIPE_BUFFER_SIZE 1024u
#define SCHED_MAX_USERS 8u
#define SCHED_MAX_SHM_SEGMENTS 32u
#define SCHED_SHM_DATA_MAX 4096u
#define PIPE_MODE_READ 1u
#define PIPE_MODE_WRITE 2u
#define SCHED_WAIT_NOTIFY_SENTINEL ((int32_t)0x80000000u)
#define SCHED_PRIO_BUDGET_LOW 1u
#define SCHED_PRIO_BUDGET_NORMAL 2u
#define SCHED_PRIO_BUDGET_HIGH 4u

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

typedef enum {
    RESCHED_NONE = 0,
    RESCHED_SAVE = 1,
    RESCHED_DROP = 2,
} resched_mode_t;

typedef struct {
    uint8_t used;
    uint8_t writable;
    uint16_t reserved0;
    uint32_t reserved1;
    uint64_t base;
    uint64_t size;
} user_map_t;

typedef struct {
    uint8_t used;
    uint8_t mode;
    uint16_t reserved0;
    uint32_t pipe_slot;
} proc_pipe_handle_t;

typedef struct {
    uint8_t used;
    uint8_t reserved0[3];
    uint32_t readers;
    uint32_t writers;
    uint32_t head;
    uint32_t len;
    uint8_t data[SCHED_PIPE_BUFFER_SIZE];
} sched_pipe_t;

typedef struct {
    uint8_t used;
    uint8_t reserved0[3];
    uint32_t uid;
    char name[MYAOS_NAME_MAX];
} sched_user_t;

typedef struct {
    uint8_t used;
    uint8_t reserved0[3];
    uint32_t owner_uid;
    uint32_t size;
    char name[MYAOS_NAME_MAX];
    uint8_t data[SCHED_SHM_DATA_MAX];
} sched_shm_t;

typedef struct {
    uint8_t used;
    uint8_t user_mode;
    uint8_t thread_mode;
    uint8_t reserved_thread0;
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t thread_group;
    uint64_t thread_arg;
    char name[SCHED_PROC_NAME_MAX];
    uint8_t state;
    uint8_t background;
    uint8_t priority;
    uint8_t sched_budget;
    uint32_t cpu_limit_ticks;
    uint64_t vm_limit_bytes;
    uint64_t cpu_ticks_used;
    uint64_t saved_rsp;
    uint64_t wake_tick;
    uint64_t run_count;
    uint64_t last_run_tick;
    int32_t exit_code;
    int32_t wait_pid;
    int32_t* wait_exit_ptr;
    uint8_t wait_notify_active;
    uint8_t wait_notify_clear;
    uint16_t wait_notify_reserved0;
    uint32_t wait_notify_mask;
    uint32_t* wait_notify_out_ptr;
    uint64_t wait_notify_deadline;
    char cwd[MYAOS_PATH_MAX];
    process_entry_t entry;
    int argc;
    char** argv;
    void* argv_block;
    void* kernel_stack_raw;
    uint64_t kernel_stack_top;
    uint64_t page_table_cr3;
    uint64_t user_image_base;
    uint64_t user_image_size;
    uint64_t user_argv_base;
    uint64_t user_argv_size;
    uint64_t user_stack_base;
    uint64_t user_stack_size;
    uint64_t user_trampoline;
    uint64_t user_entry;
    uint64_t user_rsp;
    uint64_t user_argv_ptr;
    uint64_t user_region_base;
    uint64_t user_region_end;
    uint64_t user_mmap_next;
    user_map_t user_maps[SCHED_MAX_USER_MAPS];
    proc_pipe_handle_t pipe_handles[SCHED_MAX_PIPE_HANDLES];
    uint8_t stdout_redirect;
    uint8_t stdout_append;
    uint8_t stdout_truncated;
    uint8_t ipc_full;
    uint32_t notify_bits;
    uint32_t ipc_from;
    uint32_t ipc_len;
    char stdout_path[MYAOS_PATH_MAX];
    char ipc_data[MYAOS_IPC_MSG_MAX];
} sched_proc_t;

static sched_proc_t g_procs[SCHED_MAX_PROCS];
static sched_pipe_t g_pipes[SCHED_MAX_PIPES];
static sched_user_t g_users[SCHED_MAX_USERS];
static sched_shm_t g_shm[SCHED_MAX_SHM_SEGMENTS];
static uint32_t g_proc_count;
static uint32_t g_current_slot = SCHED_INVALID_SLOT;
static uint32_t g_next_pid = 1;
static uint32_t g_slice_ticks = 1;
static uint64_t g_last_switch_tick;
static uint8_t g_started;
static uint16_t g_kernel_cs;
static uint16_t g_kernel_ss;
static uint16_t g_user_cs;
static uint16_t g_user_ss;
static volatile resched_mode_t g_resched_mode;

static inline uint64_t read_rflags(void) {
    uint64_t rflags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(rflags));
    return rflags;
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static void mem_zero(void* ptr, size_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void mem_copy(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

    if (dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int str_eq(const char* a, const char* b) {
    size_t i = 0;

    if (!a || !b) {
        return 0;
    }

    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int str_starts_with(const char* text, const char* prefix) {
    size_t i = 0;

    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void basename_copy(char* dst, const char* path, size_t dst_size) {
    const char* last = path;

    if (!path) {
        str_copy(dst, "proc", dst_size);
        return;
    }

    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '/') {
            last = &path[i + 1];
        }
    }

    str_copy(dst, last[0] ? last : "proc", dst_size);
}

static int find_user_slot_by_name(const char* name) {
    if (!name || !name[0]) {
        return -1;
    }
    for (uint32_t i = 0; i < SCHED_MAX_USERS; i++) {
        if (g_users[i].used && g_users[i].name[0]) {
            if (str_eq(g_users[i].name, name)) {
                return (int)i;
            }
        }
    }
    return -1;
}

static int find_shm_slot_by_name(const char* name) {
    if (!name || !name[0]) {
        return -1;
    }
    for (uint32_t i = 0; i < SCHED_MAX_SHM_SEGMENTS; i++) {
        if (g_shm[i].used && str_eq(g_shm[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static sched_shm_t* shm_from_id(int32_t id) {
    uint32_t slot;

    if (id <= 0) {
        return NULL;
    }
    slot = (uint32_t)(id - 1);
    if (slot >= SCHED_MAX_SHM_SEGMENTS || !g_shm[slot].used) {
        return NULL;
    }
    return &g_shm[slot];
}

static uint32_t count_procs_with_cr3(uint64_t cr3) {
    uint32_t count = 0;

    if (cr3 == 0u) {
        return 0u;
    }
    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (g_procs[i].used && g_procs[i].page_table_cr3 == cr3) {
            count++;
        }
    }
    return count;
}

static void proc_write_i32_ptr(sched_proc_t* proc, int32_t* ptr, int32_t value) {
    uint64_t current_cr3;
    uint64_t prev_cr3;

    if (!proc || !ptr) {
        return;
    }

    current_cr3 = paging_current_cr3();
    if (!proc->user_mode || proc->page_table_cr3 == 0u || proc->page_table_cr3 == current_cr3) {
        *ptr = value;
        return;
    }

    prev_cr3 = current_cr3;
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return;
    }
    *ptr = value;
    (void)paging_switch_to(prev_cr3);
}

static void proc_write_u32_ptr(sched_proc_t* proc, uint32_t* ptr, uint32_t value) {
    uint64_t current_cr3;
    uint64_t prev_cr3;

    if (!proc || !ptr) {
        return;
    }

    current_cr3 = paging_current_cr3();
    if (!proc->user_mode || proc->page_table_cr3 == 0u || proc->page_table_cr3 == current_cr3) {
        *ptr = value;
        return;
    }

    prev_cr3 = current_cr3;
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return;
    }
    *ptr = value;
    (void)paging_switch_to(prev_cr3);
}

static uint64_t slot_user_region_base(uint32_t slot) {
    return USER_REGION_BASE + (uint64_t)slot * USER_REGION_STRIDE;
}

static uint8_t ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end) {
    return (uint8_t)(a_start < b_end && b_start < a_end);
}

static uint8_t user_map_is_swapped(const user_map_t* map) {
    return (uint8_t)(map && map->used && (map->reserved0 & USER_MAP_FLAG_SWAPPED) != 0u);
}

static uint64_t proc_user_mapped_bytes(const sched_proc_t* proc) {
    uint64_t total = 0u;

    if (!proc) {
        return 0u;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used || proc->user_maps[i].size == 0u) {
            continue;
        }
        total += proc->user_maps[i].size;
    }
    return total;
}

static sched_proc_t* current_proc(void) {
    if (g_current_slot >= SCHED_MAX_PROCS || !g_procs[g_current_slot].used) {
        return NULL;
    }
    return &g_procs[g_current_slot];
}

static sched_proc_t* find_proc_by_pid(int32_t pid) {
    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (g_procs[i].used && (int32_t)g_procs[i].pid == pid) {
            return &g_procs[i];
        }
    }
    return NULL;
}

static int find_proc_slot_by_pid(int32_t pid) {
    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (g_procs[i].used && (int32_t)g_procs[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

static int pipe_slot_valid(uint32_t pipe_slot) {
    return (pipe_slot < SCHED_MAX_PIPES && g_pipes[pipe_slot].used) ? 1 : 0;
}

static int alloc_pipe_slot(void) {
    for (uint32_t i = 0; i < SCHED_MAX_PIPES; i++) {
        if (!g_pipes[i].used) {
            g_pipes[i].used = 1u;
            g_pipes[i].readers = 0u;
            g_pipes[i].writers = 0u;
            g_pipes[i].head = 0u;
            g_pipes[i].len = 0u;
            return (int)i;
        }
    }
    return -1;
}

static void maybe_release_pipe(uint32_t pipe_slot) {
    if (pipe_slot >= SCHED_MAX_PIPES || !g_pipes[pipe_slot].used) {
        return;
    }
    if (g_pipes[pipe_slot].readers == 0u && g_pipes[pipe_slot].writers == 0u) {
        g_pipes[pipe_slot].used = 0u;
        g_pipes[pipe_slot].head = 0u;
        g_pipes[pipe_slot].len = 0u;
    }
}

static int alloc_proc_pipe_handle(sched_proc_t* proc) {
    if (!proc) {
        return -1;
    }
    for (uint32_t i = 0; i < SCHED_MAX_PIPE_HANDLES; i++) {
        if (!proc->pipe_handles[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int close_proc_pipe_handle(sched_proc_t* proc, int fd) {
    uint32_t slot;
    uint8_t mode;
    uint32_t pipe_slot;

    if (!proc || fd <= 0 || fd > (int)SCHED_MAX_PIPE_HANDLES) {
        return -1;
    }
    slot = (uint32_t)(fd - 1);
    if (!proc->pipe_handles[slot].used) {
        return -1;
    }

    mode = proc->pipe_handles[slot].mode;
    pipe_slot = proc->pipe_handles[slot].pipe_slot;
    proc->pipe_handles[slot].used = 0u;
    proc->pipe_handles[slot].mode = 0u;
    proc->pipe_handles[slot].pipe_slot = 0u;

    if (pipe_slot_valid(pipe_slot)) {
        if ((mode & PIPE_MODE_READ) != 0u && g_pipes[pipe_slot].readers > 0u) {
            g_pipes[pipe_slot].readers--;
        }
        if ((mode & PIPE_MODE_WRITE) != 0u && g_pipes[pipe_slot].writers > 0u) {
            g_pipes[pipe_slot].writers--;
        }
        maybe_release_pipe(pipe_slot);
    }
    return 0;
}

static void close_all_proc_pipe_handles(sched_proc_t* proc) {
    if (!proc) {
        return;
    }
    for (uint32_t i = 0; i < SCHED_MAX_PIPE_HANDLES; i++) {
        if (proc->pipe_handles[i].used) {
            (void)close_proc_pipe_handle(proc, (int)i + 1);
        }
    }
}

static proc_pipe_handle_t* proc_pipe_handle_for_fd(sched_proc_t* proc, int32_t fd, uint8_t required_mode) {
    uint32_t slot;
    proc_pipe_handle_t* handle;

    if (!proc || fd <= 0 || fd > (int32_t)SCHED_MAX_PIPE_HANDLES) {
        return NULL;
    }

    slot = (uint32_t)(fd - 1);
    handle = &proc->pipe_handles[slot];
    if (!handle->used) {
        return NULL;
    }
    if ((handle->mode & required_mode) != required_mode) {
        return NULL;
    }
    if (!pipe_slot_valid(handle->pipe_slot)) {
        return NULL;
    }
    return handle;
}

static int slot_is_runnable(uint32_t slot) {
    return slot < SCHED_MAX_PROCS && g_procs[slot].used && g_procs[slot].state == MYAOS_PROC_READY;
}

static uint8_t sanitize_priority(uint8_t priority) {
    if (priority < MYAOS_PROC_PRIO_LOW || priority > MYAOS_PROC_PRIO_HIGH) {
        return MYAOS_PROC_PRIO_NORMAL;
    }
    return priority;
}

static uint8_t priority_budget(uint8_t priority) {
    priority = sanitize_priority(priority);
    if (priority == MYAOS_PROC_PRIO_HIGH) {
        return SCHED_PRIO_BUDGET_HIGH;
    }
    if (priority == MYAOS_PROC_PRIO_NORMAL) {
        return SCHED_PRIO_BUDGET_NORMAL;
    }
    return SCHED_PRIO_BUDGET_LOW;
}

static void replenish_ready_budgets(void) {
    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (!slot_is_runnable(i)) {
            continue;
        }
        g_procs[i].sched_budget = priority_budget(g_procs[i].priority);
    }
}

static uint32_t find_next_ready_with_budget(uint32_t from) {
    uint32_t best_slot = SCHED_INVALID_SLOT;
    uint8_t best_priority = 0;

    if (g_proc_count == 0) {
        return SCHED_INVALID_SLOT;
    }

    for (uint32_t step = 1; step <= SCHED_MAX_PROCS; step++) {
        uint32_t slot = (from + step) % SCHED_MAX_PROCS;
        uint8_t priority;

        if (!slot_is_runnable(slot)) {
            continue;
        }
        if (g_procs[slot].sched_budget == 0u) {
            continue;
        }

        priority = sanitize_priority(g_procs[slot].priority);
        if (best_slot == SCHED_INVALID_SLOT || priority > best_priority) {
            best_slot = slot;
            best_priority = priority;
        }
    }

    return best_slot;
}

static uint32_t find_next_ready(uint32_t from) {
    uint32_t slot = find_next_ready_with_budget(from);

    if (slot != SCHED_INVALID_SLOT) {
        return slot;
    }

    replenish_ready_budgets();
    return find_next_ready_with_budget(from);
}

static uint32_t first_ready_slot(void) {
    return find_next_ready(SCHED_MAX_PROCS - 1u);
}

static void clear_user_meta(sched_proc_t* proc) {
    if (!proc) {
        return;
    }

    proc->user_mode = 0;
    proc->thread_mode = 0;
    proc->thread_arg = 0u;
    proc->user_image_base = 0u;
    proc->user_image_size = 0u;
    proc->user_argv_base = 0u;
    proc->user_argv_size = 0u;
    proc->user_stack_base = 0u;
    proc->user_stack_size = 0u;
    proc->user_trampoline = 0u;
    proc->user_entry = 0u;
    proc->user_rsp = 0u;
    proc->user_argv_ptr = 0u;
    proc->user_region_base = 0u;
    proc->user_region_end = 0u;
    proc->user_mmap_next = 0u;
    mem_zero(proc->user_maps, sizeof(proc->user_maps));
}

static void free_user_space(sched_proc_t* proc) {
    uint64_t prev_cr3;

    if (!proc) {
        return;
    }
    if (proc->page_table_cr3 == 0u) {
        goto clear_meta;
    }

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        goto clear_meta;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used || proc->user_maps[i].size == 0u) {
            continue;
        }
        if (user_map_is_swapped(&proc->user_maps[i])) {
            (void)swap_free(proc->user_maps[i].reserved1);
        } else {
            paging_free_user_range(proc->user_maps[i].base, proc->user_maps[i].size);
        }
    }
    if (proc->user_image_size) {
        paging_free_user_range(proc->user_image_base, proc->user_image_size);
    }
    if (proc->user_argv_size) {
        paging_free_user_range(proc->user_argv_base, proc->user_argv_size);
    }
    if (proc->user_stack_size) {
        paging_free_user_range(proc->user_stack_base, proc->user_stack_size);
    }
    if (proc->user_trampoline) {
        paging_free_user_range(proc->user_trampoline, PAGE_SIZE);
    }

    (void)paging_switch_to(prev_cr3);

clear_meta:
    clear_user_meta(proc);
}

static uint64_t make_initial_context(uint32_t slot) {
    uint64_t stack_top;
    isr_context_t* frame;
    sched_proc_t* proc;

    if (slot >= SCHED_MAX_PROCS || !g_procs[slot].kernel_stack_raw) {
        return 0;
    }

    proc = &g_procs[slot];
    stack_top = proc->kernel_stack_top;
    frame = (isr_context_t*)(uintptr_t)(stack_top - sizeof(isr_context_t));
    mem_zero(frame, sizeof(*frame));

    frame->vector = 32u;
    frame->error_code = 0u;
    frame->rflags = (read_rflags() | 0x200ULL) & ~(0x3000ULL);

    if (proc->user_mode) {
        frame->rip = proc->user_entry;
        frame->cs = g_user_cs;
        frame->rsp = proc->user_rsp;
        frame->ss = g_user_ss;
        if (proc->thread_mode) {
            frame->rdi = proc->thread_arg;
            frame->rsi = 0u;
        } else {
            frame->rdi = (uint64_t)(uint32_t)proc->argc;
            frame->rsi = proc->user_argv_ptr;
        }
    } else {
        frame->rip = (uint64_t)(uintptr_t)scheduler_task_bootstrap;
        frame->cs = g_kernel_cs;
        /* Match SysV call-entry stack shape (RSP % 16 == 8) for kernel tasks. */
        frame->rsp = stack_top - sizeof(uint64_t);
        frame->ss = g_kernel_ss;
    }

    return (uint64_t)(uintptr_t)frame;
}

static int duplicate_argv(int argc, const char* const* argv, char*** out_argv, void** out_block) {
    size_t bytes = (size_t)(argc + 1) * sizeof(char*);
    char** argv_copy;
    char* strings;

    if (!out_argv || !out_block || argc < 0) {
        return -1;
    }

    for (int i = 0; i < argc; i++) {
        const char* arg = (argv && argv[i]) ? argv[i] : "";
        bytes += str_len(arg) + 1;
    }

    argv_copy = (char**)kmalloc(bytes ? bytes : sizeof(char*));
    if (!argv_copy) {
        return -1;
    }

    strings = (char*)(void*)((uint8_t*)argv_copy + (size_t)(argc + 1) * sizeof(char*));
    for (int i = 0; i < argc; i++) {
        const char* arg = (argv && argv[i]) ? argv[i] : "";
        size_t len = str_len(arg) + 1;
        argv_copy[i] = strings;
        str_copy(strings, arg, len);
        strings += len;
    }
    argv_copy[argc] = NULL;

    *out_argv = argv_copy;
    *out_block = argv_copy;
    return 0;
}

static int build_user_argv(sched_proc_t* proc) {
    char** user_argv;
    uint64_t ptr_bytes;
    uint64_t cursor;
    uint64_t end;

    if (!proc || proc->argc < 0) {
        return -1;
    }

    ptr_bytes = (uint64_t)(proc->argc + 1) * sizeof(uint64_t);
    if (ptr_bytes >= proc->user_argv_size) {
        return -1;
    }

    user_argv = (char**)(uintptr_t)proc->user_argv_base;
    cursor = proc->user_argv_base + ptr_bytes;
    end = proc->user_argv_base + proc->user_argv_size;

    for (int i = 0; i < proc->argc; i++) {
        const char* arg = proc->argv ? proc->argv[i] : "";
        uint64_t len = str_len(arg) + 1;
        if (cursor + len > end) {
            return -1;
        }

        user_argv[i] = (char*)(uintptr_t)cursor;
        mem_copy((void*)(uintptr_t)cursor, arg, (size_t)len);
        cursor += len;
    }
    user_argv[proc->argc] = NULL;
    proc->user_argv_ptr = proc->user_argv_base;
    return 0;
}

static int setup_user_process_image(sched_proc_t* proc, uint32_t slot, const elf_image_t* image) {
    uint64_t base;
    uint64_t image_offset;
    uint64_t image_size;
    uint64_t entry_delta;
    uint64_t stack_top;
    uint64_t initial_rsp;
    uint64_t prev_cr3;
    uint8_t trampoline[11] = {
        0x89, 0xC7,
        0xB8, 0x00, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0xEB, 0xFE,
    };

    if (!proc || !image || !image->load_base || !image->entry) {
        return -1;
    }

    base = slot_user_region_base(slot);
    image_offset = USER_IMAGE_BASE_OFFSET;
    image_size = align_up(image->load_size, PAGE_SIZE);
    if (image_size == 0) {
        return -1;
    }
    if (image_offset + image_size >= USER_ARGV_BASE_OFFSET) {
        return -1;
    }
    if (proc->page_table_cr3 == 0u) {
        return -1;
    }

    proc->user_mode = 1;
    proc->thread_mode = 0u;
    proc->user_image_base = base + image_offset;
    proc->user_image_size = image_size;
    proc->user_trampoline = base + USER_TRAMPOLINE_OFFSET;
    proc->user_argv_base = base + USER_ARGV_BASE_OFFSET;
    proc->user_argv_size = USER_ARGV_SIZE;
    proc->user_stack_base = base + USER_STACK_TOP_OFFSET - USER_STACK_SIZE;
    proc->user_stack_size = USER_STACK_SIZE;
    proc->user_region_base = base;
    proc->user_region_end = base + USER_REGION_STRIDE;
    proc->user_mmap_next = base + USER_MMAP_MIN_OFFSET;
    mem_zero(proc->user_maps, sizeof(proc->user_maps));

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        free_user_space(proc);
        return -1;
    }

    if (paging_alloc_user_range(proc->user_image_base, proc->user_image_size, 1u) != 0) {
        (void)paging_switch_to(prev_cr3);
        free_user_space(proc);
        return -1;
    }
    mem_copy((void*)(uintptr_t)proc->user_image_base, image->load_base, (size_t)image->load_size);

    if (paging_alloc_user_range(proc->user_trampoline, PAGE_SIZE, 1u) != 0) {
        (void)paging_switch_to(prev_cr3);
        free_user_space(proc);
        return -1;
    }
    trampoline[3] = (uint8_t)(MYAOS_SYS_PROC_EXIT & 0xFFu);
    trampoline[4] = (uint8_t)((MYAOS_SYS_PROC_EXIT >> 8) & 0xFFu);
    trampoline[5] = (uint8_t)((MYAOS_SYS_PROC_EXIT >> 16) & 0xFFu);
    trampoline[6] = (uint8_t)((MYAOS_SYS_PROC_EXIT >> 24) & 0xFFu);
    mem_copy((void*)(uintptr_t)proc->user_trampoline, trampoline, sizeof(trampoline));

    if (paging_alloc_user_range(proc->user_argv_base, proc->user_argv_size, 1u) != 0) {
        (void)paging_switch_to(prev_cr3);
        free_user_space(proc);
        return -1;
    }
    if (build_user_argv(proc) != 0) {
        (void)paging_switch_to(prev_cr3);
        free_user_space(proc);
        return -1;
    }

    if (paging_alloc_user_range(proc->user_stack_base, proc->user_stack_size, 1u) != 0) {
        (void)paging_switch_to(prev_cr3);
        free_user_space(proc);
        return -1;
    }

    entry_delta = (uint64_t)(uintptr_t)image->entry - (uint64_t)(uintptr_t)image->load_base;
    if (entry_delta >= image->load_size) {
        (void)paging_switch_to(prev_cr3);
        free_user_space(proc);
        return -1;
    }

    proc->user_entry = proc->user_image_base + entry_delta;

    stack_top = base + USER_STACK_TOP_OFFSET;
    initial_rsp = stack_top - sizeof(uint64_t);
    *((uint64_t*)(uintptr_t)initial_rsp) = proc->user_trampoline;
    proc->user_rsp = initial_rsp;
    (void)paging_switch_to(prev_cr3);
    return 0;
}

static void free_proc_resources(sched_proc_t* proc) {
    uint8_t can_free_user = 1u;

    if (!proc) {
        return;
    }

    if (proc->argv_block) {
        kfree(proc->argv_block);
        proc->argv_block = NULL;
        proc->argv = NULL;
        proc->argc = 0;
    }

    if (proc->user_mode && proc->page_table_cr3 != 0u && proc->page_table_cr3 != paging_kernel_cr3()) {
        if (count_procs_with_cr3(proc->page_table_cr3) > 1u) {
            can_free_user = 0u;
        }
    }

    if (can_free_user) {
        free_user_space(proc);
    } else {
        clear_user_meta(proc);
    }
}

static void release_proc_slot(sched_proc_t* proc) {
    uint64_t proc_cr3;
    uint8_t destroy_space = 0u;

    if (!proc || !proc->used) {
        return;
    }

    proc_cr3 = proc->page_table_cr3;
    close_all_proc_pipe_handles(proc);
    free_proc_resources(proc);
    if (proc_cr3 != 0u && proc_cr3 != paging_kernel_cr3() && count_procs_with_cr3(proc_cr3) <= 1u) {
        destroy_space = 1u;
    }
    if (destroy_space) {
        paging_space_destroy(proc_cr3);
    }
    if (proc->kernel_stack_raw) {
        kfree(proc->kernel_stack_raw);
        proc->kernel_stack_raw = NULL;
    }

    net_on_process_exit((int32_t)proc->pid);

    proc->used = 0;
    proc->pid = 0;
    proc->ppid = 0;
    proc->uid = 0;
    proc->thread_group = 0;
    proc->thread_arg = 0;
    proc->thread_mode = 0;
    proc->reserved_thread0 = 0;
    proc->name[0] = '\0';
    proc->state = MYAOS_PROC_NONE;
    proc->background = 0;
    proc->priority = MYAOS_PROC_PRIO_NORMAL;
    proc->sched_budget = 0;
    proc->cpu_limit_ticks = 0;
    proc->vm_limit_bytes = 0u;
    proc->cpu_ticks_used = 0;
    proc->saved_rsp = 0;
    proc->wake_tick = 0;
    proc->run_count = 0;
    proc->last_run_tick = 0;
    proc->exit_code = 0;
    proc->wait_pid = -1;
    proc->wait_exit_ptr = NULL;
    proc->wait_notify_active = 0u;
    proc->wait_notify_clear = 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = 0u;
    proc->wait_notify_out_ptr = NULL;
    proc->wait_notify_deadline = 0u;
    proc->cwd[0] = '\0';
    proc->entry = NULL;
    proc->kernel_stack_top = 0;
    proc->page_table_cr3 = 0;
    proc->user_region_base = 0;
    proc->user_region_end = 0;
    clear_user_meta(proc);
    mem_zero(proc->pipe_handles, sizeof(proc->pipe_handles));
    proc->stdout_redirect = 0;
    proc->stdout_append = 0;
    proc->stdout_truncated = 0;
    proc->stdout_path[0] = '\0';
    proc->ipc_full = 0;
    proc->notify_bits = 0;
    proc->ipc_from = 0;
    proc->ipc_len = 0;
    proc->ipc_data[0] = '\0';
    if (g_proc_count > 0) {
        g_proc_count--;
    }
}

static int alloc_proc_slot(void) {
    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (!g_procs[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int init_proc_common(
    uint32_t slot,
    const char* name,
    int argc,
    const char* const* argv,
    const char* cwd,
    uint8_t background,
    uint32_t ppid
) {
    sched_proc_t* proc = &g_procs[slot];
    sched_proc_t* parent = current_proc();

    mem_zero(proc, sizeof(*proc));
    proc->used = 1;
    proc->pid = g_next_pid++;
    proc->ppid = ppid;
    proc->uid = parent ? parent->uid : 0u;
    proc->thread_mode = 0u;
    proc->reserved_thread0 = 0u;
    proc->thread_group = proc->pid;
    proc->thread_arg = 0u;
    str_copy(proc->name, name ? name : "proc", sizeof(proc->name));
    proc->state = MYAOS_PROC_READY;
    proc->background = background;
    proc->priority = background ? MYAOS_PROC_PRIO_LOW : MYAOS_PROC_PRIO_NORMAL;
    proc->sched_budget = priority_budget(proc->priority);
    proc->cpu_limit_ticks = 0;
    proc->vm_limit_bytes = 0u;
    proc->cpu_ticks_used = 0;
    proc->exit_code = 0;
    proc->wait_pid = -1;
    proc->wait_exit_ptr = NULL;
    proc->wait_notify_active = 0u;
    proc->wait_notify_clear = 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = 0u;
    proc->wait_notify_out_ptr = NULL;
    proc->wait_notify_deadline = 0u;
    proc->page_table_cr3 = paging_kernel_cr3();
    proc->user_region_base = 0u;
    proc->user_region_end = 0u;
    str_copy(proc->cwd, cwd && cwd[0] ? cwd : "/", sizeof(proc->cwd));
    proc->stdout_redirect = 0;
    proc->stdout_append = 0;
    proc->stdout_truncated = 0;
    proc->stdout_path[0] = '\0';
    proc->ipc_full = 0;
    proc->notify_bits = 0;
    proc->ipc_from = 0;
    proc->ipc_len = 0;

    if (parent) {
        for (uint32_t i = 0; i < SCHED_MAX_PIPE_HANDLES; i++) {
            uint32_t pipe_slot;
            uint8_t mode;

            if (!parent->pipe_handles[i].used) {
                continue;
            }
            pipe_slot = parent->pipe_handles[i].pipe_slot;
            mode = parent->pipe_handles[i].mode;
            if (!pipe_slot_valid(pipe_slot)) {
                continue;
            }

            proc->pipe_handles[i] = parent->pipe_handles[i];
            if ((mode & PIPE_MODE_READ) != 0u) {
                g_pipes[pipe_slot].readers++;
            }
            if ((mode & PIPE_MODE_WRITE) != 0u) {
                g_pipes[pipe_slot].writers++;
            }
        }
    }

    proc->kernel_stack_raw = kmalloc(SCHED_STACK_SIZE + 16u);
    if (!proc->kernel_stack_raw) {
        close_all_proc_pipe_handles(proc);
        proc->used = 0;
        return -1;
    }

    proc->kernel_stack_top = ((uint64_t)(uintptr_t)proc->kernel_stack_raw + (uint64_t)SCHED_STACK_SIZE) & ~0xFULL;

    if (duplicate_argv(argc, argv, &proc->argv, &proc->argv_block) != 0) {
        kfree(proc->kernel_stack_raw);
        proc->kernel_stack_raw = NULL;
        proc->kernel_stack_top = 0;
        close_all_proc_pipe_handles(proc);
        proc->used = 0;
        return -1;
    }
    proc->argc = argc;

    g_proc_count++;
    return 0;
}

static void wake_sleepers(uint64_t tick_now) {
    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        sched_proc_t* proc = &g_procs[i];

        if (!proc->used) {
            continue;
        }
        if (proc->state == MYAOS_PROC_SLEEPING && proc->wake_tick <= tick_now) {
            proc->state = MYAOS_PROC_READY;
            proc->wake_tick = 0;
            continue;
        }
        if (proc->state == MYAOS_PROC_BLOCKED &&
            proc->wait_notify_active &&
            proc->wait_notify_deadline != 0u &&
            proc->wait_notify_deadline <= tick_now) {
            if (proc->wait_notify_out_ptr) {
                proc_write_u32_ptr(proc, proc->wait_notify_out_ptr, 0u);
            }
            proc->wait_notify_active = 0u;
            proc->wait_notify_clear = 0u;
            proc->wait_notify_reserved0 = 0u;
            proc->wait_notify_mask = 0u;
            proc->wait_notify_out_ptr = NULL;
            proc->wait_notify_deadline = 0u;
            proc->wait_pid = -1;
            proc->state = MYAOS_PROC_READY;
        }
    }
}

static void maybe_wake_notify_waiter(sched_proc_t* proc) {
    uint32_t mask;
    uint32_t bits;

    if (!proc || !proc->used || proc->state != MYAOS_PROC_BLOCKED || !proc->wait_notify_active) {
        return;
    }

    mask = proc->wait_notify_mask ? proc->wait_notify_mask : 0xFFFFFFFFu;
    bits = proc->notify_bits & mask;
    if (bits == 0u) {
        return;
    }

    if (proc->wait_notify_out_ptr) {
        proc_write_u32_ptr(proc, proc->wait_notify_out_ptr, bits);
    }
    if (proc->wait_notify_clear) {
        proc->notify_bits &= ~bits;
    }

    proc->wait_notify_active = 0u;
    proc->wait_notify_clear = 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = 0u;
    proc->wait_notify_out_ptr = NULL;
    proc->wait_notify_deadline = 0u;
    proc->wait_pid = -1;
    proc->state = MYAOS_PROC_READY;
}

static void maybe_wake_waiters(uint32_t child_slot) {
    sched_proc_t* child = &g_procs[child_slot];

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        sched_proc_t* proc = &g_procs[i];
        if (!proc->used || proc->state != MYAOS_PROC_BLOCKED) {
            continue;
        }
        if (proc->wait_notify_active) {
            continue;
        }
        if (proc->wait_pid != -1 && proc->wait_pid != (int32_t)child->pid) {
            continue;
        }
        if (proc->wait_exit_ptr) {
            proc_write_i32_ptr(proc, proc->wait_exit_ptr, child->exit_code);
        }
        proc->wait_exit_ptr = NULL;
        proc->wait_pid = -1;
        proc->state = MYAOS_PROC_READY;
    }
}

static int find_child_slot(uint32_t parent_pid, int32_t pid, uint8_t require_zombie) {
    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (!g_procs[i].used || g_procs[i].ppid != parent_pid) {
            continue;
        }
        if (pid != -1 && (int32_t)g_procs[i].pid != pid) {
            continue;
        }
        if (require_zombie && g_procs[i].state != MYAOS_PROC_ZOMBIE) {
            continue;
        }
        return (int)i;
    }
    return -1;
}

static uint64_t schedule_switch(uint64_t current_rsp, uint64_t tick_now, uint8_t timer_preempt) {
    sched_proc_t* current = current_proc();
    uint32_t next;

    if (timer_preempt && current && current->used && current->state == MYAOS_PROC_RUNNING && current->user_mode) {
        current->cpu_ticks_used++;
        if (current->cpu_limit_ticks != 0u && current->cpu_ticks_used >= current->cpu_limit_ticks) {
            current->exit_code = -9;
            current->state = MYAOS_PROC_ZOMBIE;
            current->wake_tick = 0;
            current->wait_pid = -1;
            current->wait_exit_ptr = NULL;
            current->wait_notify_active = 0u;
            current->wait_notify_clear = 0u;
            current->wait_notify_reserved0 = 0u;
            current->wait_notify_mask = 0u;
            current->wait_notify_out_ptr = NULL;
            current->wait_notify_deadline = 0u;
            maybe_wake_waiters(g_current_slot);
            g_resched_mode = RESCHED_DROP;
        }
    }

    wake_sleepers(tick_now);

    if (!g_started || g_proc_count == 0) {
        g_resched_mode = RESCHED_NONE;
        return current_rsp;
    }

    if (current && timer_preempt && g_resched_mode == RESCHED_NONE) {
        if (tick_now - g_last_switch_tick < g_slice_ticks) {
            return current_rsp;
        }
        current->saved_rsp = current_rsp;
        if (current->state == MYAOS_PROC_RUNNING) {
            current->state = MYAOS_PROC_READY;
        }
    } else if (current && g_resched_mode == RESCHED_SAVE) {
        current->saved_rsp = current_rsp;
        if (current->state == MYAOS_PROC_RUNNING) {
            current->state = MYAOS_PROC_READY;
        }
    }

    next = (g_current_slot == SCHED_INVALID_SLOT) ? first_ready_slot() : find_next_ready(g_current_slot);
    if (next == SCHED_INVALID_SLOT) {
        if (current && current->used && (current->state == MYAOS_PROC_READY || current->state == MYAOS_PROC_RUNNING)) {
            current->state = MYAOS_PROC_RUNNING;
            g_last_switch_tick = tick_now;
            g_resched_mode = RESCHED_NONE;
            gdt_set_kernel_stack(current->kernel_stack_top);
            (void)paging_switch_to(current->page_table_cr3);
            return current->saved_rsp ? current->saved_rsp : current_rsp;
        }
        g_resched_mode = RESCHED_NONE;
        return current_rsp;
    }

    g_current_slot = next;
    g_procs[next].state = MYAOS_PROC_RUNNING;
    if (g_procs[next].sched_budget > 0u) {
        g_procs[next].sched_budget--;
    }
    g_procs[next].run_count++;
    g_procs[next].last_run_tick = tick_now;
    g_last_switch_tick = tick_now;
    g_resched_mode = RESCHED_NONE;
    gdt_set_kernel_stack(g_procs[next].kernel_stack_top);
    (void)paging_switch_to(g_procs[next].page_table_cr3);
    return g_procs[next].saved_rsp;
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

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        mem_zero(&g_procs[i], sizeof(g_procs[i]));
        g_procs[i].wait_pid = -1;
    }
    for (uint32_t i = 0; i < SCHED_MAX_PIPES; i++) {
        mem_zero(&g_pipes[i], sizeof(g_pipes[i]));
    }
    for (uint32_t i = 0; i < SCHED_MAX_USERS; i++) {
        mem_zero(&g_users[i], sizeof(g_users[i]));
    }
    for (uint32_t i = 0; i < SCHED_MAX_SHM_SEGMENTS; i++) {
        mem_zero(&g_shm[i], sizeof(g_shm[i]));
    }

    g_users[0].used = 1u;
    g_users[0].uid = 0u;
    str_copy(g_users[0].name, "root", sizeof(g_users[0].name));
    g_users[1].used = 1u;
    g_users[1].uid = 1000u;
    str_copy(g_users[1].name, "user", sizeof(g_users[1].name));
    g_users[2].used = 1u;
    g_users[2].uid = 1001u;
    str_copy(g_users[2].name, "guest", sizeof(g_users[2].name));

    g_proc_count = 0;
    g_current_slot = SCHED_INVALID_SLOT;
    g_next_pid = 1;
    g_slice_ticks = slice_ticks;
    g_last_switch_tick = 0;
    g_started = 0;
    g_kernel_cs = gdt_kernel_code_selector();
    g_kernel_ss = gdt_kernel_data_selector();
    g_user_cs = gdt_user_code_selector();
    g_user_ss = gdt_user_data_selector();
    g_resched_mode = RESCHED_NONE;
}

int scheduler_spawn_kernel(
    const char* name,
    process_entry_t entry,
    int argc,
    const char* const* argv,
    uint8_t background,
    int32_t* out_pid
) {
    int slot = alloc_proc_slot();
    uint32_t ppid = current_proc() ? current_proc()->pid : 0;

    if (slot < 0 || !entry) {
        return -1;
    }
    if (init_proc_common((uint32_t)slot, name, argc, argv, scheduler_current_cwd(), background, ppid) != 0) {
        return -1;
    }

    g_procs[slot].entry = entry;
    g_procs[slot].user_mode = 0;
    g_procs[slot].saved_rsp = make_initial_context((uint32_t)slot);
    if (g_procs[slot].saved_rsp == 0) {
        release_proc_slot(&g_procs[slot]);
        return -1;
    }

    if (out_pid) {
        *out_pid = (int32_t)g_procs[slot].pid;
    }
    return 0;
}

static void apply_spawn_opts(sched_proc_t* proc, uint8_t background, const myaos_spawn_opts_t* opts) {
    if (!proc) {
        return;
    }

    proc->background = background;
    proc->priority = background ? MYAOS_PROC_PRIO_LOW : MYAOS_PROC_PRIO_NORMAL;
    proc->sched_budget = priority_budget(proc->priority);
    proc->cpu_limit_ticks = 0u;
    proc->vm_limit_bytes = 0u;
    proc->cpu_ticks_used = 0u;
    proc->stdout_redirect = 0;
    proc->stdout_append = 0;
    proc->stdout_truncated = 0;
    proc->stdout_path[0] = '\0';

    if (!opts) {
        return;
    }

    if ((opts->flags & MYAOS_SPAWN_BACKGROUND) != 0u) {
        proc->background = 1;
    }
    if (opts->priority != 0u) {
        proc->priority = sanitize_priority(opts->priority);
        proc->sched_budget = priority_budget(proc->priority);
    }
    if (opts->cpu_limit_ticks != 0u) {
        proc->cpu_limit_ticks = opts->cpu_limit_ticks;
    }
    if ((opts->flags & MYAOS_SPAWN_STDOUT_REDIRECT) != 0u && opts->stdout_path[0]) {
        proc->stdout_redirect = 1;
        proc->stdout_append = opts->stdout_append ? 1 : 0;
        proc->stdout_truncated = 0;
        str_copy(proc->stdout_path, opts->stdout_path, sizeof(proc->stdout_path));
    }
}

int scheduler_spawn_program_ex(
    const char* cwd,
    const char* path,
    int argc,
    const char* const* argv,
    const myaos_spawn_opts_t* opts,
    int32_t* out_pid
) {
    int slot = alloc_proc_slot();
    elf_image_t image;
    char name[SCHED_PROC_NAME_MAX];
    char fallback_path[MYAOS_PATH_MAX];
    const char* load_path = path;
    uint32_t ppid = current_proc() ? current_proc()->pid : 0;
    const char* proc_cwd = cwd && cwd[0] ? cwd : scheduler_current_cwd();
    uint8_t background = (opts && (opts->flags & MYAOS_SPAWN_BACKGROUND) != 0u) ? 1u : 0u;

    if (slot < 0) {
        return -1;
    }
    if (vfs_can_exec(proc_cwd, load_path) != 0 || elf_load_from_vfs(proc_cwd, load_path, &image) != 0) {
        if (!str_starts_with(path, "/bin/")) {
            return -1;
        }
        str_copy(fallback_path, "/boot/bin/", sizeof(fallback_path));
        {
            size_t base = str_len(fallback_path);
            const char* suffix = path + 5;
            for (size_t i = 0; suffix[i] && base + 1u < sizeof(fallback_path); i++) {
                fallback_path[base++] = suffix[i];
            }
            fallback_path[base] = '\0';
        }
        load_path = fallback_path;
        if (vfs_can_exec(proc_cwd, load_path) != 0 || elf_load_from_vfs(proc_cwd, load_path, &image) != 0) {
            return -1;
        }
    }

    basename_copy(name, load_path, sizeof(name));
    if (init_proc_common((uint32_t)slot, name, argc, argv, proc_cwd, background, ppid) != 0) {
        elf_unload(&image);
        return -1;
    }
    apply_spawn_opts(&g_procs[slot], background, opts);
    if (paging_space_create(&g_procs[slot].page_table_cr3) != 0) {
        release_proc_slot(&g_procs[slot]);
        elf_unload(&image);
        return -1;
    }

    if (setup_user_process_image(&g_procs[slot], (uint32_t)slot, &image) != 0) {
        release_proc_slot(&g_procs[slot]);
        elf_unload(&image);
        return -1;
    }

    g_procs[slot].saved_rsp = make_initial_context((uint32_t)slot);
    if (g_procs[slot].saved_rsp == 0) {
        release_proc_slot(&g_procs[slot]);
        elf_unload(&image);
        return -1;
    }

    elf_unload(&image);
    if (out_pid) {
        *out_pid = (int32_t)g_procs[slot].pid;
    }
    return 0;
}

int scheduler_spawn_program(
    const char* cwd,
    const char* path,
    int argc,
    const char* const* argv,
    uint8_t background,
    int32_t* out_pid
) {
    myaos_spawn_opts_t opts;

    mem_zero(&opts, sizeof(opts));
    if (background) {
        opts.flags |= MYAOS_SPAWN_BACKGROUND;
    }

    return scheduler_spawn_program_ex(cwd, path, argc, argv, &opts, out_pid);
}

int scheduler_exec_current(const char* path, int argc, const char* const* argv) {
    sched_proc_t* proc = current_proc();
    elf_image_t image;
    char** argv_copy = NULL;
    void* argv_block = NULL;
    uint64_t new_cr3 = 0;
    uint8_t needs_new_space;

    if (!proc) {
        return -1;
    }
    if (proc->thread_mode) {
        return -1;
    }
    if (vfs_can_exec(proc->cwd, path) != 0) {
        return -1;
    }
    if (elf_load_from_vfs(proc->cwd, path, &image) != 0) {
        return -1;
    }
    if (duplicate_argv(argc, argv, &argv_copy, &argv_block) != 0) {
        elf_unload(&image);
        return -1;
    }

    needs_new_space = (uint8_t)(proc->page_table_cr3 == paging_kernel_cr3());
    if (needs_new_space && paging_space_create(&new_cr3) != 0) {
        kfree(argv_block);
        elf_unload(&image);
        return -1;
    }

    free_proc_resources(proc);
    if (needs_new_space) {
        proc->page_table_cr3 = new_cr3;
    }
    proc->argc = argc;
    proc->argv = argv_copy;
    proc->argv_block = argv_block;
    proc->entry = NULL;

    if (setup_user_process_image(proc, g_current_slot, &image) != 0) {
        elf_unload(&image);
        scheduler_exit_current(-1);
        return -1;
    }

    basename_copy(proc->name, path, sizeof(proc->name));
    proc->state = MYAOS_PROC_READY;
    proc->saved_rsp = make_initial_context(g_current_slot);
    if (proc->saved_rsp == 0) {
        elf_unload(&image);
        scheduler_exit_current(-1);
        return -1;
    }

    elf_unload(&image);
    g_resched_mode = RESCHED_DROP;
    return 0;
}

int scheduler_wait(int32_t pid, int32_t* exit_code) {
    sched_proc_t* proc = current_proc();
    int slot;

    if (!proc) {
        return -1;
    }

    slot = find_child_slot(proc->pid, pid, 1);
    if (slot >= 0) {
        if (exit_code) {
            *exit_code = g_procs[slot].exit_code;
        }
        release_proc_slot(&g_procs[slot]);
        return 0;
    }

    slot = find_child_slot(proc->pid, pid, 0);
    if (slot < 0) {
        return -1;
    }

    proc->state = MYAOS_PROC_BLOCKED;
    proc->wait_pid = pid;
    proc->wait_exit_ptr = exit_code;
    proc->wait_notify_active = 0u;
    proc->wait_notify_clear = 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = 0u;
    proc->wait_notify_out_ptr = NULL;
    proc->wait_notify_deadline = 0u;
    g_resched_mode = RESCHED_SAVE;
    return 0;
}

int scheduler_wait_poll(int32_t pid, int32_t* exit_code) {
    sched_proc_t* proc = current_proc();
    int slot;

    if (!proc) {
        return -1;
    }

    slot = find_child_slot(proc->pid, pid, 1);
    if (slot >= 0) {
        if (exit_code) {
            *exit_code = g_procs[slot].exit_code;
        }
        release_proc_slot(&g_procs[slot]);
        return 1;
    }

    slot = find_child_slot(proc->pid, pid, 0);
    if (slot < 0) {
        return -1;
    }

    return 0;
}

int scheduler_kill_pid(int32_t pid, int32_t exit_code) {
    sched_proc_t* caller = current_proc();
    int slot = find_proc_slot_by_pid(pid);

    if (slot < 0) {
        return -1;
    }

    if (caller) {
        if ((int32_t)g_procs[slot].pid != (int32_t)caller->pid && g_procs[slot].ppid != caller->pid) {
            return -1;
        }
    }

    if (g_procs[slot].state == MYAOS_PROC_ZOMBIE || g_procs[slot].state == MYAOS_PROC_NONE) {
        return -1;
    }

    if ((uint32_t)slot == g_current_slot && caller == &g_procs[slot]) {
        scheduler_exit_current(exit_code);
        return 0;
    }

    g_procs[slot].exit_code = exit_code;
    g_procs[slot].state = MYAOS_PROC_ZOMBIE;
    g_procs[slot].wake_tick = 0;
    g_procs[slot].wait_pid = -1;
    g_procs[slot].wait_exit_ptr = NULL;
    g_procs[slot].wait_notify_active = 0u;
    g_procs[slot].wait_notify_clear = 0u;
    g_procs[slot].wait_notify_reserved0 = 0u;
    g_procs[slot].wait_notify_mask = 0u;
    g_procs[slot].wait_notify_out_ptr = NULL;
    g_procs[slot].wait_notify_deadline = 0u;
    maybe_wake_waiters((uint32_t)slot);
    return 0;
}

void scheduler_exit_current(int32_t exit_code) {
    sched_proc_t* proc = current_proc();

    if (!proc) {
        return;
    }

    proc->exit_code = exit_code;
    proc->state = MYAOS_PROC_ZOMBIE;
    proc->wait_pid = -1;
    proc->wait_exit_ptr = NULL;
    proc->wait_notify_active = 0u;
    proc->wait_notify_clear = 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = 0u;
    proc->wait_notify_out_ptr = NULL;
    proc->wait_notify_deadline = 0u;
    maybe_wake_waiters(g_current_slot);
    g_resched_mode = RESCHED_DROP;
}

void scheduler_kill_current(int32_t exit_code) {
    scheduler_exit_current(exit_code);
}

void scheduler_yield_current(void) {
    sched_proc_t* proc = current_proc();
    if (!proc) {
        return;
    }
    if (proc->state == MYAOS_PROC_RUNNING) {
        proc->state = MYAOS_PROC_READY;
    }
    g_resched_mode = RESCHED_SAVE;
}

void scheduler_sleep_current(uint64_t ticks) {
    sched_proc_t* proc = current_proc();

    if (!proc) {
        return;
    }
    if (ticks == 0) {
        scheduler_yield_current();
        return;
    }

    proc->wake_tick = timer_ticks() + ticks;
    proc->state = MYAOS_PROC_SLEEPING;
    g_resched_mode = RESCHED_SAVE;
}

uint64_t scheduler_on_timer_interrupt(uint64_t current_rsp, uint64_t tick_now) {
    return schedule_switch(current_rsp, tick_now, 1);
}

uint64_t scheduler_on_syscall_complete(uint64_t current_rsp, uint64_t tick_now) {
    if (g_resched_mode == RESCHED_NONE) {
        return current_rsp;
    }
    return schedule_switch(current_rsp, tick_now, 0);
}

int scheduler_reschedule_pending(void) {
    return g_resched_mode != RESCHED_NONE;
}

void scheduler_task_bootstrap(void) {
    for (;;) {
        sched_proc_t* proc = current_proc();
        int exit_code = 0;

        if (!proc || !proc->entry) {
            __asm__ __volatile__("hlt");
            continue;
        }

        exit_code = proc->entry(proc->argc, proc->argv);
        scheduler_exit_current(exit_code);
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }
}

void scheduler_run(void) {
    uint32_t first = first_ready_slot();

    if (first == SCHED_INVALID_SLOT) {
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    __asm__ __volatile__("cli");

    g_current_slot = first;
    g_started = 1;
    g_last_switch_tick = timer_ticks();
    g_procs[first].state = MYAOS_PROC_RUNNING;
    if (g_procs[first].sched_budget > 0u) {
        g_procs[first].sched_budget--;
    }
    g_procs[first].run_count++;
    g_procs[first].last_run_tick = g_last_switch_tick;
    gdt_set_kernel_stack(g_procs[first].kernel_stack_top);
    (void)paging_switch_to(g_procs[first].page_table_cr3);

    scheduler_restore_and_enter(g_procs[first].saved_rsp);
}

uint32_t scheduler_process_count(void) {
    return g_proc_count;
}

int32_t scheduler_current_pid(void) {
    sched_proc_t* proc = current_proc();
    return proc ? (int32_t)proc->pid : -1;
}

int scheduler_list_processes(myaos_proc_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t count = 0;

    if (!out_count || (max_entries != 0 && !out)) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (!g_procs[i].used) {
            continue;
        }
        if (count >= max_entries) {
            break;
        }

        out[count].pid = g_procs[i].pid;
        out[count].ppid = g_procs[i].ppid;
        str_copy(out[count].name, g_procs[i].name, sizeof(out[count].name));
        out[count].state = g_procs[i].state;
        out[count].background = g_procs[i].background;
        out[count].priority = sanitize_priority(g_procs[i].priority);
        out[count].reserved0 = 0;
        out[count].exit_code = g_procs[i].exit_code;
        out[count].cpu_limit_ticks = g_procs[i].cpu_limit_ticks;
        out[count].reserved1 = 0;
        out[count].cpu_ticks_used = g_procs[i].cpu_ticks_used;
        out[count].wake_tick = g_procs[i].wake_tick;
        out[count].run_count = g_procs[i].run_count;
        out[count].last_run_tick = g_procs[i].last_run_tick;
        out[count].vm_limit_bytes = g_procs[i].vm_limit_bytes;
        out[count].vm_used_bytes = proc_user_mapped_bytes(&g_procs[i]);
        count++;
    }

    *out_count = count;
    return 0;
}

int scheduler_notify(int32_t pid, uint32_t bits) {
    sched_proc_t* proc = find_proc_by_pid(pid);

    if (!proc || bits == 0) {
        return -1;
    }

    proc->notify_bits |= bits;
    maybe_wake_notify_waiter(proc);
    return 0;
}

int scheduler_notify_poll(uint32_t mask, uint8_t clear, uint32_t* out_bits) {
    sched_proc_t* proc = current_proc();
    uint32_t effective_mask = mask ? mask : 0xFFFFFFFFu;
    uint32_t bits;

    if (!proc || !out_bits) {
        return -1;
    }

    bits = proc->notify_bits & effective_mask;
    *out_bits = bits;
    if (bits == 0) {
        return -1;
    }
    if (clear) {
        proc->notify_bits &= ~bits;
    }
    return 0;
}

int scheduler_notify_wait(uint32_t mask, uint32_t timeout_ticks, uint8_t clear, uint32_t* out_bits) {
    sched_proc_t* proc = current_proc();
    uint32_t effective_mask = mask ? mask : 0xFFFFFFFFu;
    uint32_t bits;
    uint64_t now;
    uint64_t deadline;

    if (!proc || !out_bits) {
        return -1;
    }

    bits = proc->notify_bits & effective_mask;
    if (bits != 0u) {
        *out_bits = bits;
        if (clear) {
            proc->notify_bits &= ~bits;
        }
        return 0;
    }
    if (timeout_ticks == 0u) {
        *out_bits = 0u;
        return -1;
    }

    now = timer_ticks();
    deadline = now + (uint64_t)timeout_ticks;
    if (deadline < now) {
        deadline = UINT64_MAX;
    }

    *out_bits = 0u;
    proc->wait_exit_ptr = NULL;
    proc->wait_pid = SCHED_WAIT_NOTIFY_SENTINEL;
    proc->wait_notify_active = 1u;
    proc->wait_notify_clear = clear ? 1u : 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = mask;
    proc->wait_notify_out_ptr = out_bits;
    proc->wait_notify_deadline = deadline;
    proc->state = MYAOS_PROC_BLOCKED;
    g_resched_mode = RESCHED_SAVE;
    return 0;
}

int scheduler_ipc_send(int32_t pid, const char* data, uint32_t len) {
    sched_proc_t* proc = find_proc_by_pid(pid);
    sched_proc_t* sender = current_proc();
    uint32_t copy_len;

    if (!proc || !data || len == 0) {
        return -1;
    }
    if (proc->ipc_full) {
        return -1;
    }

    copy_len = (len >= MYAOS_IPC_MSG_MAX) ? (MYAOS_IPC_MSG_MAX - 1u) : len;
    for (uint32_t i = 0; i < copy_len; i++) {
        proc->ipc_data[i] = data[i];
    }
    proc->ipc_data[copy_len] = '\0';
    proc->ipc_len = copy_len;
    proc->ipc_from = sender ? sender->pid : 0u;
    proc->ipc_full = 1;
    proc->notify_bits |= 0x1u;
    maybe_wake_notify_waiter(proc);
    return 0;
}

int scheduler_ipc_recv(char* out_buf, uint32_t max_len, uint32_t* out_len, int32_t* out_from) {
    sched_proc_t* proc = current_proc();
    uint32_t copy_len;

    if (!proc || !out_buf || max_len == 0 || !out_len) {
        return -1;
    }
    if (!proc->ipc_full) {
        return -1;
    }

    copy_len = proc->ipc_len;
    if (copy_len + 1 > max_len) {
        copy_len = max_len - 1u;
    }

    for (uint32_t i = 0; i < copy_len; i++) {
        out_buf[i] = proc->ipc_data[i];
    }
    out_buf[copy_len] = '\0';
    *out_len = copy_len;
    if (out_from) {
        *out_from = (int32_t)proc->ipc_from;
    }

    proc->ipc_full = 0;
    proc->ipc_len = 0;
    proc->ipc_from = 0;
    proc->ipc_data[0] = '\0';
    proc->notify_bits &= ~0x1u;
    return 0;
}

int scheduler_pipe_create(int32_t* out_read_fd, int32_t* out_write_fd) {
    sched_proc_t* proc = current_proc();
    int pipe_slot;
    int read_slot;
    int write_slot;

    if (!proc || !out_read_fd || !out_write_fd) {
        return -1;
    }

    pipe_slot = alloc_pipe_slot();
    if (pipe_slot < 0) {
        return -1;
    }

    read_slot = alloc_proc_pipe_handle(proc);
    if (read_slot < 0) {
        g_pipes[pipe_slot].used = 0u;
        return -1;
    }
    proc->pipe_handles[read_slot].used = 1u;
    proc->pipe_handles[read_slot].mode = PIPE_MODE_READ;
    proc->pipe_handles[read_slot].reserved0 = 0u;
    proc->pipe_handles[read_slot].pipe_slot = (uint32_t)pipe_slot;

    write_slot = alloc_proc_pipe_handle(proc);
    if (write_slot < 0) {
        proc->pipe_handles[read_slot].used = 0u;
        proc->pipe_handles[read_slot].mode = 0u;
        proc->pipe_handles[read_slot].pipe_slot = 0u;
        g_pipes[pipe_slot].used = 0u;
        return -1;
    }
    proc->pipe_handles[write_slot].used = 1u;
    proc->pipe_handles[write_slot].mode = PIPE_MODE_WRITE;
    proc->pipe_handles[write_slot].reserved0 = 0u;
    proc->pipe_handles[write_slot].pipe_slot = (uint32_t)pipe_slot;

    g_pipes[pipe_slot].readers = 1u;
    g_pipes[pipe_slot].writers = 1u;
    *out_read_fd = read_slot + 1;
    *out_write_fd = write_slot + 1;
    return 0;
}

int scheduler_pipe_close(int32_t fd) {
    sched_proc_t* proc = current_proc();
    return close_proc_pipe_handle(proc, (int)fd);
}

int scheduler_pipe_read(int32_t fd, void* out_buf, uint32_t max_len, uint32_t* out_read) {
    sched_proc_t* proc = current_proc();
    proc_pipe_handle_t* handle;
    sched_pipe_t* pipe;
    uint8_t* out = (uint8_t*)out_buf;
    uint32_t copy_len;
    uint32_t tail;

    if (!proc || !out_read || (max_len != 0u && !out)) {
        return -1;
    }

    handle = proc_pipe_handle_for_fd(proc, fd, PIPE_MODE_READ);
    if (!handle) {
        return -1;
    }
    pipe = &g_pipes[handle->pipe_slot];

    if (max_len == 0u || pipe->len == 0u) {
        *out_read = 0u;
        return 0;
    }

    copy_len = (pipe->len < max_len) ? pipe->len : max_len;
    tail = (pipe->head + SCHED_PIPE_BUFFER_SIZE - pipe->len) % SCHED_PIPE_BUFFER_SIZE;
    {
        uint32_t first = SCHED_PIPE_BUFFER_SIZE - tail;
        uint32_t second;
        if (first > copy_len) {
            first = copy_len;
        }
        second = copy_len - first;
        if (first > 0u) {
            mem_copy(out, &pipe->data[tail], first);
        }
        if (second > 0u) {
            mem_copy(out + first, &pipe->data[0], second);
        }
    }
    pipe->len -= copy_len;
    *out_read = copy_len;
    return 0;
}

int scheduler_pipe_write(int32_t fd, const void* data, uint32_t len, uint32_t* out_written) {
    sched_proc_t* proc = current_proc();
    proc_pipe_handle_t* handle;
    sched_pipe_t* pipe;
    const uint8_t* in = (const uint8_t*)data;
    uint32_t space;
    uint32_t copy_len;

    if (!proc || !out_written || (len != 0u && !in)) {
        return -1;
    }

    handle = proc_pipe_handle_for_fd(proc, fd, PIPE_MODE_WRITE);
    if (!handle) {
        return -1;
    }
    pipe = &g_pipes[handle->pipe_slot];
    if (pipe->readers == 0u) {
        return -1;
    }

    space = SCHED_PIPE_BUFFER_SIZE - pipe->len;
    copy_len = (len < space) ? len : space;
    {
        uint32_t first = SCHED_PIPE_BUFFER_SIZE - pipe->head;
        uint32_t second;
        if (first > copy_len) {
            first = copy_len;
        }
        second = copy_len - first;
        if (first > 0u) {
            mem_copy(&pipe->data[pipe->head], in, first);
        }
        if (second > 0u) {
            mem_copy(&pipe->data[0], in + first, second);
        }
    }
    pipe->head = (pipe->head + copy_len) % SCHED_PIPE_BUFFER_SIZE;
    pipe->len += copy_len;
    *out_written = copy_len;
    return 0;
}

int scheduler_thread_create_current(uint64_t entry, uint64_t arg, uint64_t stack_top, int32_t* out_tid) {
    sched_proc_t* parent = current_proc();
    sched_proc_t* thread;
    int slot;
    uint64_t start_rsp;
    uint64_t prev_cr3;

    if (!parent || !parent->user_mode || entry == 0u || parent->page_table_cr3 == 0u) {
        return -1;
    }
    if (stack_top <= parent->user_region_base + sizeof(uint64_t) || stack_top > parent->user_region_end) {
        return -1;
    }
    if (parent->user_trampoline == 0u) {
        return -1;
    }

    start_rsp = stack_top - sizeof(uint64_t);
    if (!paging_user_range_accessible(start_rsp, sizeof(uint64_t), 1u)) {
        return -1;
    }

    slot = alloc_proc_slot();
    if (slot < 0) {
        return -1;
    }
    if (init_proc_common(
            (uint32_t)slot,
            "thread",
            0,
            NULL,
            parent->cwd,
            parent->background,
            parent->pid
        ) != 0) {
        return -1;
    }

    thread = &g_procs[slot];
    if (thread->argv_block) {
        kfree(thread->argv_block);
        thread->argv_block = NULL;
        thread->argv = NULL;
        thread->argc = 0;
    }

    thread->entry = NULL;
    thread->thread_mode = 1u;
    thread->thread_group = parent->thread_group ? parent->thread_group : parent->pid;
    thread->thread_arg = arg;
    thread->user_mode = 1u;
    thread->page_table_cr3 = parent->page_table_cr3;
    thread->vm_limit_bytes = parent->vm_limit_bytes;
    thread->user_image_base = parent->user_image_base;
    thread->user_image_size = parent->user_image_size;
    thread->user_argv_base = parent->user_argv_base;
    thread->user_argv_size = parent->user_argv_size;
    thread->user_stack_base = parent->user_stack_base;
    thread->user_stack_size = parent->user_stack_size;
    thread->user_trampoline = parent->user_trampoline;
    thread->user_region_base = parent->user_region_base;
    thread->user_region_end = parent->user_region_end;
    thread->user_mmap_next = parent->user_mmap_next;
    mem_copy(thread->user_maps, parent->user_maps, sizeof(thread->user_maps));
    thread->user_entry = entry;
    thread->user_rsp = start_rsp;
    thread->user_argv_ptr = 0u;
    str_copy(thread->name, "thread", sizeof(thread->name));

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(thread->page_table_cr3) != 0) {
        release_proc_slot(thread);
        return -1;
    }
    *((uint64_t*)(uintptr_t)start_rsp) = thread->user_trampoline;
    (void)paging_switch_to(prev_cr3);

    thread->saved_rsp = make_initial_context((uint32_t)slot);
    if (thread->saved_rsp == 0u) {
        release_proc_slot(thread);
        return -1;
    }

    if (out_tid) {
        *out_tid = (int32_t)thread->pid;
    }
    return 0;
}

static int can_join_thread(sched_proc_t* caller, int32_t tid) {
    sched_proc_t* target;
    uint32_t caller_group;
    uint32_t target_group;

    if (!caller) {
        return -1;
    }

    target = find_proc_by_pid(tid);
    if (!target || !target->thread_mode) {
        return -1;
    }

    caller_group = caller->thread_group ? caller->thread_group : caller->pid;
    target_group = target->thread_group ? target->thread_group : target->pid;
    if (caller_group == 0u || target_group == 0u || caller_group != target_group) {
        return -1;
    }

    return 0;
}

int scheduler_thread_join_current(int32_t tid, int32_t* exit_code) {
    sched_proc_t* caller = current_proc();

    if (!caller || tid <= 0) {
        return -1;
    }
    if (can_join_thread(caller, tid) != 0) {
        return -1;
    }

    return scheduler_wait(tid, exit_code);
}

int scheduler_thread_join_poll_current(int32_t tid, int32_t* exit_code) {
    sched_proc_t* caller = current_proc();

    if (!caller || tid <= 0) {
        return -1;
    }
    if (can_join_thread(caller, tid) != 0) {
        return -1;
    }

    return scheduler_wait_poll(tid, exit_code);
}

int scheduler_set_limits_current(const myaos_proc_limits_t* limits) {
    sched_proc_t* proc = current_proc();
    uint64_t mapped;

    if (!proc || !limits) {
        return -1;
    }

    mapped = proc_user_mapped_bytes(proc);
    if (limits->vm_limit_bytes != 0u && mapped > limits->vm_limit_bytes) {
        return -1;
    }
    if (limits->cpu_limit_ticks != 0u && proc->cpu_ticks_used >= limits->cpu_limit_ticks) {
        return -1;
    }

    proc->cpu_limit_ticks = limits->cpu_limit_ticks;
    proc->vm_limit_bytes = limits->vm_limit_bytes;

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (!g_procs[i].used || &g_procs[i] == proc) {
            continue;
        }
        if (!g_procs[i].user_mode || g_procs[i].page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        g_procs[i].vm_limit_bytes = limits->vm_limit_bytes;
    }

    return 0;
}

int scheduler_get_limits_current(myaos_proc_limits_t* out) {
    sched_proc_t* proc = current_proc();

    if (!proc || !out) {
        return -1;
    }

    out->cpu_limit_ticks = proc->cpu_limit_ticks;
    out->reserved0 = 0u;
    out->vm_limit_bytes = proc->vm_limit_bytes;
    return 0;
}

int scheduler_shm_create(const char* name, uint32_t size, int32_t* out_id) {
    int slot;

    if (!name || !name[0] || !out_id || size == 0u || size > SCHED_SHM_DATA_MAX) {
        return -1;
    }

    slot = find_shm_slot_by_name(name);
    if (slot >= 0) {
        if (size > g_shm[slot].size) {
            return -1;
        }
        *out_id = slot + 1;
        return 0;
    }

    for (uint32_t i = 0; i < SCHED_MAX_SHM_SEGMENTS; i++) {
        if (g_shm[i].used) {
            continue;
        }
        g_shm[i].used = 1u;
        g_shm[i].owner_uid = scheduler_current_uid();
        g_shm[i].size = size;
        str_copy(g_shm[i].name, name, sizeof(g_shm[i].name));
        mem_zero(g_shm[i].data, sizeof(g_shm[i].data));
        *out_id = (int32_t)i + 1;
        return 0;
    }
    return -1;
}

int scheduler_shm_open(const char* name, int32_t* out_id) {
    int slot;

    if (!name || !name[0] || !out_id) {
        return -1;
    }
    slot = find_shm_slot_by_name(name);
    if (slot < 0) {
        return -1;
    }
    *out_id = slot + 1;
    return 0;
}

int scheduler_shm_read(int32_t id, uint32_t offset, void* out_buf, uint32_t max_len, uint32_t* out_len) {
    sched_shm_t* shm = shm_from_id(id);
    uint32_t copy_len;

    if (!shm || !out_len || (max_len != 0u && !out_buf)) {
        return -1;
    }
    if (offset >= shm->size || max_len == 0u) {
        *out_len = 0u;
        return 0;
    }

    copy_len = shm->size - offset;
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    mem_copy(out_buf, &shm->data[offset], copy_len);
    *out_len = copy_len;
    return 0;
}

int scheduler_shm_write(int32_t id, uint32_t offset, const void* data, uint32_t len, uint32_t* out_written) {
    sched_shm_t* shm = shm_from_id(id);
    uint32_t copy_len;
    uint32_t uid = scheduler_current_uid();

    if (!shm || !out_written || (len != 0u && !data)) {
        return -1;
    }
    if (uid != 0u && uid != shm->owner_uid) {
        return -1;
    }
    if (offset >= shm->size || len == 0u) {
        *out_written = 0u;
        return 0;
    }

    copy_len = shm->size - offset;
    if (copy_len > len) {
        copy_len = len;
    }
    mem_copy(&shm->data[offset], data, copy_len);
    *out_written = copy_len;
    return 0;
}

int scheduler_shm_close(int32_t id) {
    return shm_from_id(id) ? 0 : -1;
}

int scheduler_stdout_redirect_info(char* out_path, size_t out_size, uint8_t* out_append, uint8_t* out_truncate_now) {
    sched_proc_t* proc = current_proc();

    if (!proc || !proc->stdout_redirect) {
        return 0;
    }
    if (out_path && out_size > 0) {
        str_copy(out_path, proc->stdout_path, out_size);
    }
    if (out_append) {
        *out_append = proc->stdout_append;
    }
    if (out_truncate_now) {
        *out_truncate_now = (uint8_t)((proc->stdout_append == 0u && proc->stdout_truncated == 0u) ? 1u : 0u);
    }
    return 1;
}

void scheduler_stdout_redirect_mark_truncated(void) {
    sched_proc_t* proc = current_proc();
    if (!proc) {
        return;
    }
    proc->stdout_truncated = 1;
}

int scheduler_mem_map_current(uint64_t size, uint8_t writable, uint64_t* out_addr) {
    sched_proc_t* proc = current_proc();
    uint64_t map_window_start;
    uint64_t map_window_end;
    uint64_t image_end;
    uint64_t alloc_size;
    uint64_t candidate;
    uint64_t prev_cr3;
    int free_entry = -1;
    int found = 0;

    if (!proc || !proc->user_mode || !out_addr || proc->user_region_base == 0u ||
        proc->user_region_end <= proc->user_region_base) {
        return -1;
    }
    if (size == 0u) {
        return -1;
    }

    alloc_size = align_up(size, PAGE_SIZE);
    if (alloc_size == 0u || alloc_size < size) {
        return -1;
    }
    if (proc->vm_limit_bytes != 0u) {
        uint64_t mapped = proc_user_mapped_bytes(proc);
        if (mapped > proc->vm_limit_bytes || alloc_size > (proc->vm_limit_bytes - mapped)) {
            return -1;
        }
    }

    map_window_start = proc->user_region_base + USER_MMAP_MIN_OFFSET;
    map_window_end = proc->user_argv_base;
    if (map_window_end <= map_window_start) {
        return -1;
    }

    image_end = align_up(proc->user_image_base + proc->user_image_size, PAGE_SIZE);
    if (image_end > map_window_start) {
        map_window_start = image_end;
    }
    if (map_window_start >= map_window_end || alloc_size > (map_window_end - map_window_start)) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            free_entry = (int)i;
            break;
        }
    }
    if (free_entry < 0) {
        return -1;
    }

    candidate = proc->user_mmap_next;
    if (candidate < map_window_start || candidate >= map_window_end) {
        candidate = map_window_start;
    }
    candidate = align_up(candidate, PAGE_SIZE);

    while (candidate + alloc_size > candidate && candidate + alloc_size <= map_window_end) {
        uint64_t candidate_end = candidate + alloc_size;
        uint64_t next_candidate = 0u;
        uint8_t conflict = 0u;

        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            uint64_t map_base;
            uint64_t map_end;

            if (!proc->user_maps[i].used) {
                continue;
            }
            map_base = proc->user_maps[i].base;
            map_end = map_base + proc->user_maps[i].size;
            if (map_end <= map_base) {
                continue;
            }
            if (!ranges_overlap(candidate, candidate_end, map_base, map_end)) {
                continue;
            }
            conflict = 1u;
            if (map_end > next_candidate) {
                next_candidate = map_end;
            }
        }

        if (!conflict) {
            found = 1;
            break;
        }
        if (next_candidate <= candidate) {
            break;
        }
        candidate = align_up(next_candidate, PAGE_SIZE);
    }

    if (!found) {
        return -1;
    }

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return -1;
    }
    if (paging_alloc_user_range(candidate, alloc_size, writable ? 1u : 0u) != 0) {
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    (void)paging_switch_to(prev_cr3);

    proc->user_maps[free_entry].used = 1u;
    proc->user_maps[free_entry].writable = writable ? 1u : 0u;
    proc->user_maps[free_entry].reserved0 = 0u;
    proc->user_maps[free_entry].reserved1 = SWAP_SLOT_NONE;
    proc->user_maps[free_entry].base = candidate;
    proc->user_maps[free_entry].size = alloc_size;
    proc->user_mmap_next = candidate + alloc_size;
    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        int peer_slot = -1;

        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used) {
                peer_slot = (int)i;
                break;
            }
        }
        if (peer_slot >= 0) {
            peer->user_maps[peer_slot].used = 1u;
            peer->user_maps[peer_slot].writable = writable ? 1u : 0u;
            peer->user_maps[peer_slot].reserved0 = 0u;
            peer->user_maps[peer_slot].reserved1 = SWAP_SLOT_NONE;
            peer->user_maps[peer_slot].base = candidate;
            peer->user_maps[peer_slot].size = alloc_size;
        }
        if (peer->user_mmap_next == 0u || peer->user_mmap_next < candidate + alloc_size) {
            peer->user_mmap_next = candidate + alloc_size;
        }
    }
    *out_addr = candidate;
    return 0;
}

int scheduler_mem_unmap_current(uint64_t addr) {
    sched_proc_t* proc = current_proc();
    uint64_t prev_cr3;
    uint64_t map_base = 0;
    uint64_t map_size = 0;
    int map_idx = -1;

    if (!proc || !proc->user_mode || addr == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            continue;
        }
        if (proc->user_maps[i].base == addr) {
            map_idx = (int)i;
            break;
        }
    }
    if (map_idx < 0) {
        return -1;
    }

    map_base = proc->user_maps[map_idx].base;
    map_size = proc->user_maps[map_idx].size;
    if (map_size == 0u) {
        return -1;
    }

    if (user_map_is_swapped(&proc->user_maps[map_idx])) {
        (void)swap_free(proc->user_maps[map_idx].reserved1);
    } else {
        prev_cr3 = paging_current_cr3();
        if (paging_switch_to(proc->page_table_cr3) != 0) {
            return -1;
        }
        paging_free_user_range(map_base, map_size);
        (void)paging_switch_to(prev_cr3);
    }

    proc->user_maps[map_idx].used = 0u;
    proc->user_maps[map_idx].writable = 0u;
    proc->user_maps[map_idx].reserved0 = 0u;
    proc->user_maps[map_idx].reserved1 = SWAP_SLOT_NONE;
    proc->user_maps[map_idx].base = 0u;
    proc->user_maps[map_idx].size = 0u;
    if (proc->user_mmap_next == 0u || map_base < proc->user_mmap_next) {
        proc->user_mmap_next = map_base;
    }
    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used || peer->user_maps[i].base != map_base) {
                continue;
            }
            peer->user_maps[i].used = 0u;
            peer->user_maps[i].writable = 0u;
            peer->user_maps[i].reserved0 = 0u;
            peer->user_maps[i].reserved1 = SWAP_SLOT_NONE;
            peer->user_maps[i].base = 0u;
            peer->user_maps[i].size = 0u;
            break;
        }
        if (peer->user_mmap_next == 0u || map_base < peer->user_mmap_next) {
            peer->user_mmap_next = map_base;
        }
    }
    return 0;
}

int scheduler_mem_swap_out_current(uint64_t addr) {
    sched_proc_t* proc = current_proc();
    uint64_t prev_cr3;
    uint64_t map_base = 0;
    uint64_t map_size = 0;
    uint32_t slot_id = SWAP_SLOT_NONE;
    int map_idx = -1;

    if (!proc || !proc->user_mode || addr == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            continue;
        }
        if (proc->user_maps[i].base == addr) {
            map_idx = (int)i;
            break;
        }
    }
    if (map_idx < 0 || proc->user_maps[map_idx].size == 0u) {
        return -1;
    }
    if (user_map_is_swapped(&proc->user_maps[map_idx])) {
        return -2;
    }

    map_base = proc->user_maps[map_idx].base;
    map_size = proc->user_maps[map_idx].size;

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return -1;
    }
    if (swap_store((const void*)(uintptr_t)map_base, map_size, &slot_id) != 0) {
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    paging_free_user_range(map_base, map_size);
    (void)paging_switch_to(prev_cr3);

    proc->user_maps[map_idx].reserved0 |= USER_MAP_FLAG_SWAPPED;
    proc->user_maps[map_idx].reserved1 = slot_id;

    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used || peer->user_maps[i].base != map_base) {
                continue;
            }
            peer->user_maps[i].reserved0 |= USER_MAP_FLAG_SWAPPED;
            peer->user_maps[i].reserved1 = slot_id;
            break;
        }
    }

    return 0;
}

int scheduler_mem_swap_in_current(uint64_t addr) {
    sched_proc_t* proc = current_proc();
    uint64_t prev_cr3;
    uint64_t map_base = 0;
    uint64_t map_size = 0;
    uint32_t slot_id = SWAP_SLOT_NONE;
    uint8_t writable = 0u;
    int map_idx = -1;

    if (!proc || !proc->user_mode || addr == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            continue;
        }
        if (proc->user_maps[i].base == addr) {
            map_idx = (int)i;
            break;
        }
    }
    if (map_idx < 0 || proc->user_maps[map_idx].size == 0u) {
        return -1;
    }
    if (!user_map_is_swapped(&proc->user_maps[map_idx])) {
        return -2;
    }

    map_base = proc->user_maps[map_idx].base;
    map_size = proc->user_maps[map_idx].size;
    slot_id = proc->user_maps[map_idx].reserved1;
    writable = proc->user_maps[map_idx].writable ? 1u : 0u;

    if (!swap_slot_is_used(slot_id)) {
        return -1;
    }

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return -1;
    }
    if (paging_alloc_user_range(map_base, map_size, writable) != 0) {
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    if (swap_load(slot_id, (void*)(uintptr_t)map_base, map_size) != 0) {
        paging_free_user_range(map_base, map_size);
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    (void)paging_switch_to(prev_cr3);
    (void)swap_free(slot_id);

    proc->user_maps[map_idx].reserved0 &= (uint16_t)~USER_MAP_FLAG_SWAPPED;
    proc->user_maps[map_idx].reserved1 = SWAP_SLOT_NONE;

    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used || peer->user_maps[i].base != map_base) {
                continue;
            }
            peer->user_maps[i].reserved0 &= (uint16_t)~USER_MAP_FLAG_SWAPPED;
            peer->user_maps[i].reserved1 = SWAP_SLOT_NONE;
            break;
        }
    }

    return 0;
}

uint32_t scheduler_current_uid(void) {
    sched_proc_t* proc = current_proc();
    return proc ? proc->uid : 0u;
}

int scheduler_login_current(const char* user_name) {
    sched_proc_t* proc = current_proc();
    int user_slot;

    if (!proc || !user_name) {
        return -1;
    }

    user_slot = find_user_slot_by_name(user_name);
    if (user_slot < 0) {
        return -1;
    }

    proc->uid = g_users[user_slot].uid;
    return 0;
}

int scheduler_current_is_user_mode(void) {
    sched_proc_t* proc = current_proc();
    return (proc && proc->user_mode) ? 1 : 0;
}

int scheduler_current_user_region(uint64_t* out_base, uint64_t* out_end) {
    sched_proc_t* proc = current_proc();

    if (!out_base || !out_end) {
        return -1;
    }
    if (!proc || !proc->user_mode || proc->user_region_base == 0u ||
        proc->user_region_end <= proc->user_region_base) {
        return -1;
    }

    *out_base = proc->user_region_base;
    *out_end = proc->user_region_end;
    return 0;
}

const char* scheduler_current_cwd(void) {
    sched_proc_t* proc = current_proc();
    return proc ? proc->cwd : "/";
}

int scheduler_setcwd_current(const char* abs_path) {
    sched_proc_t* proc = current_proc();
    if (!proc || !abs_path) {
        return -1;
    }
    str_copy(proc->cwd, abs_path, sizeof(proc->cwd));
    return 0;
}

int scheduler_getcwd_current(char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return -1;
    }
    str_copy(out, scheduler_current_cwd(), out_size);
    return 0;
}
