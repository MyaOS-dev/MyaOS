#ifndef SCHEDULER_INTERNAL_H
#define SCHEDULER_INTERNAL_H
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
#define SCHED_MAX_FILE_HANDLES 32u
#define SCHED_POSIX_FILE_MAX (16u * 1024u * 1024u)
#define SCHED_MAX_USERS 8u
#define SCHED_MAX_SHM_SEGMENTS 32u
#define SCHED_SHM_DATA_MAX 4096u
#define PIPE_MODE_READ 1u
#define PIPE_MODE_WRITE 2u
#define FILE_MODE_READ 1u
#define FILE_MODE_WRITE 2u
#define SCHED_WAIT_NOTIFY_SENTINEL ((int32_t)0x80000000u)
#define SCHED_PRIO_BUDGET_LOW 1u
#define SCHED_PRIO_BUDGET_NORMAL 2u
#define SCHED_PRIO_BUDGET_HIGH 4u
#define SCHED_POSIX_S_IFREG 0100000u
#define SCHED_LINUX_ARGV_MAX 64u
#define IA32_FS_BASE_MSR 0xC0000100u
#define LINUX_AT_NULL 0u
#define LINUX_AT_PHDR 3u
#define LINUX_AT_PHENT 4u
#define LINUX_AT_PHNUM 5u
#define LINUX_AT_PAGESZ 6u
#define LINUX_AT_BASE 7u
#define LINUX_AT_FLAGS 8u
#define LINUX_AT_ENTRY 9u
#define LINUX_AT_UID 11u
#define LINUX_AT_EUID 12u
#define LINUX_AT_GID 13u
#define LINUX_AT_EGID 14u
#define LINUX_AT_CLKTCK 17u
#define LINUX_AT_SECURE 23u
#define LINUX_AT_RANDOM 25u
#define LINUX_AT_EXECFN 31u
#define LINUX_CLONE_VM 0x00000100u
#define LINUX_CLONE_SIGHAND 0x00000800u
#define LINUX_CLONE_VFORK 0x00004000u
#define LINUX_CLONE_THREAD 0x00010000u
#define LINUX_CLONE_SETTLS 0x00080000u
#define LINUX_CLONE_PARENT_SETTID 0x00100000u
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000u
#define LINUX_CLONE_CHILD_SETTID 0x01000000u
#define LINUX_CLONE_SIGNAL_MASK 0x000000FFu
#define LINUX_CLONE_SUPPORTED_FLAGS                                                       \
    (LINUX_CLONE_VM | LINUX_CLONE_SIGHAND | LINUX_CLONE_VFORK | LINUX_CLONE_THREAD |     \
     LINUX_CLONE_SETTLS | LINUX_CLONE_PARENT_SETTID | LINUX_CLONE_CHILD_CLEARTID |       \
     LINUX_CLONE_CHILD_SETTID)

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
    uint8_t readable;
    uint8_t writable;
    uint8_t append;
    uint8_t dirty;
    uint8_t reserved0[3];
    uint32_t size;
    uint32_t capacity;
    uint32_t offset;
    char abs_path[MYAOS_PATH_MAX];
    uint8_t* data;
} proc_file_handle_t;

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
    uint8_t linux_compat;
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
    uint8_t wait_vfork;
    uint8_t wait_reserved0[7];
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
    uint64_t user_fs_base;
    uint64_t linux_brk_base;
    uint64_t linux_brk_current;
    uint64_t linux_brk_limit;
    user_map_t user_maps[SCHED_MAX_USER_MAPS];
    proc_pipe_handle_t pipe_handles[SCHED_MAX_PIPE_HANDLES];
    proc_file_handle_t file_handles[SCHED_MAX_FILE_HANDLES];
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


extern sched_proc_t g_procs[SCHED_MAX_PROCS];
extern sched_pipe_t g_pipes[SCHED_MAX_PIPES];
extern sched_user_t g_users[SCHED_MAX_USERS];
extern sched_shm_t g_shm[SCHED_MAX_SHM_SEGMENTS];
extern uint32_t g_proc_count;
extern uint32_t g_current_slot;
extern uint32_t g_next_pid;
extern uint32_t g_slice_ticks;
extern uint64_t g_last_switch_tick;
extern uint8_t g_started;
extern uint16_t g_kernel_cs;
extern uint16_t g_kernel_ss;
extern uint16_t g_user_cs;
extern uint16_t g_user_ss;
extern volatile resched_mode_t g_resched_mode;

uint64_t read_rflags(void);

void write_msr(uint32_t msr, uint64_t value);

uint64_t align_up(uint64_t value, uint64_t align);

void mem_zero(void* ptr, size_t size);

void mem_copy(void* dst, const void* src, size_t size);

void str_copy(char* dst, const char* src, size_t dst_size);

size_t str_len(const char* s);

int str_eq(const char* a, const char* b);

int str_starts_with(const char* text, const char* prefix);

void basename_copy(char* dst, const char* path, size_t dst_size);

int load_linux_interpreter_image(const char* interp_path, elf_image_t* out);

int find_user_slot_by_name(const char* name);

int find_shm_slot_by_name(const char* name);

sched_shm_t* shm_from_id(int32_t id);

uint32_t count_procs_with_cr3(uint64_t cr3);

void proc_write_i32_ptr(sched_proc_t* proc, int32_t* ptr, int32_t value);

void proc_write_u32_ptr(sched_proc_t* proc, uint32_t* ptr, uint32_t value);

uint64_t slot_user_region_base(uint32_t slot);

uint8_t ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end);

uint8_t user_map_is_swapped(const user_map_t* map);

uint64_t proc_user_mapped_bytes(const sched_proc_t* proc);

sched_proc_t* current_proc(void);

sched_proc_t* find_proc_by_pid(int32_t pid);

int find_proc_slot_by_pid(int32_t pid);

int pipe_slot_valid(uint32_t pipe_slot);

int alloc_pipe_slot(void);

void maybe_release_pipe(uint32_t pipe_slot);

int alloc_proc_pipe_handle(sched_proc_t* proc);

int close_proc_pipe_handle(sched_proc_t* proc, int fd);

void close_all_proc_pipe_handles(sched_proc_t* proc);

proc_pipe_handle_t* proc_pipe_handle_for_fd(sched_proc_t* proc, int32_t fd, uint8_t required_mode);

int alloc_proc_file_handle(sched_proc_t* proc);

int ensure_file_capacity(proc_file_handle_t* handle, uint32_t need);

int clone_proc_file_handle(proc_file_handle_t* dst, const proc_file_handle_t* src);

int close_proc_file_handle(sched_proc_t* proc, int fd);

void close_all_proc_file_handles(sched_proc_t* proc);

proc_file_handle_t* proc_file_handle_for_fd(sched_proc_t* proc, int32_t fd, uint8_t required_mode);

int slot_is_runnable(uint32_t slot);

uint8_t sanitize_priority(uint8_t priority);

uint8_t priority_budget(uint8_t priority);

void replenish_ready_budgets(void);

uint32_t find_next_ready_with_budget(uint32_t from);

uint32_t find_next_ready(uint32_t from);

uint32_t first_ready_slot(void);

void load_proc_fs_base(const sched_proc_t* proc);

void clear_user_meta(sched_proc_t* proc);

void free_user_space(sched_proc_t* proc);

int clone_user_range_between_spaces(
    uint64_t src_cr3,
    uint64_t dst_cr3,
    uint64_t base,
    uint64_t size,
    uint8_t writable,
    uint8_t* scratch_page
);

int clone_process_user_space(const sched_proc_t* parent, sched_proc_t* child);

uint64_t make_initial_context(uint32_t slot);

int duplicate_argv(int argc, const char* const* argv, char*** out_argv, void** out_block);

int build_user_argv(sched_proc_t* proc);


int setup_user_process_image(
    sched_proc_t* proc,
    uint32_t slot,
    const elf_image_t* image,
    const elf_image_t* interp_image
);

void free_proc_resources(sched_proc_t* proc);

void release_proc_slot(sched_proc_t* proc);

int alloc_proc_slot(void);

int init_proc_common(
    uint32_t slot,
    const char* name,
    int argc,
    const char* const* argv,
    const char* cwd,
    uint8_t background,
    uint32_t ppid
);

void wake_sleepers(uint64_t tick_now);

void maybe_wake_notify_waiter(sched_proc_t* proc);

void maybe_wake_waiters(uint32_t child_slot);

void wake_vfork_parent(uint32_t parent_pid, uint32_t child_pid);

int find_child_slot(uint32_t parent_pid, int32_t pid, uint8_t require_zombie);

uint64_t schedule_switch(uint64_t current_rsp, uint64_t tick_now, uint8_t timer_preempt);

__attribute__((noreturn)) void scheduler_restore_and_enter(uint64_t rsp);

#endif
