#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <myaos/syscall.h>
#include <stddef.h>
#include <stdint.h>

#define SCHED_PROC_NAME_MAX MYAOS_PROC_NAME_MAX

typedef int (*process_entry_t)(int argc, char** argv);

void scheduler_init(uint32_t slice_ticks);
int scheduler_spawn_kernel(
    const char* name,
    process_entry_t entry,
    int argc,
    const char* const* argv,
    uint8_t background,
    int32_t* out_pid
);
int scheduler_spawn_program(
    const char* cwd,
    const char* path,
    int argc,
    const char* const* argv,
    uint8_t background,
    int32_t* out_pid
);
int scheduler_spawn_program_ex(
    const char* cwd,
    const char* path,
    int argc,
    const char* const* argv,
    const myaos_spawn_opts_t* opts,
    int32_t* out_pid
);
int scheduler_exec_current(const char* path, int argc, const char* const* argv);
int scheduler_wait(int32_t pid, int32_t* exit_code);
int scheduler_wait_poll(int32_t pid, int32_t* exit_code);
int scheduler_kill_pid(int32_t pid, int32_t exit_code);
void scheduler_exit_current(int32_t exit_code);
void scheduler_kill_current(int32_t exit_code);
void scheduler_yield_current(void);
void scheduler_sleep_current(uint64_t ticks);
uint64_t scheduler_on_timer_interrupt(uint64_t current_rsp, uint64_t tick_now);
uint64_t scheduler_on_syscall_complete(uint64_t current_rsp, uint64_t tick_now);
int scheduler_reschedule_pending(void);
void scheduler_task_bootstrap(void);
void scheduler_run(void);
uint32_t scheduler_process_count(void);
int32_t scheduler_current_pid(void);
int scheduler_list_processes(myaos_proc_info_t* out, uint32_t max_entries, uint32_t* out_count);
int scheduler_notify(int32_t pid, uint32_t bits);
int scheduler_notify_poll(uint32_t mask, uint8_t clear, uint32_t* out_bits);
int scheduler_notify_wait(uint32_t mask, uint32_t timeout_ticks, uint8_t clear, uint32_t* out_bits);
int scheduler_ipc_send(int32_t pid, const char* data, uint32_t len);
int scheduler_ipc_recv(char* out_buf, uint32_t max_len, uint32_t* out_len, int32_t* out_from);
int scheduler_pipe_create(int32_t* out_read_fd, int32_t* out_write_fd);
int scheduler_pipe_close(int32_t fd);
int scheduler_pipe_read(int32_t fd, void* out_buf, uint32_t max_len, uint32_t* out_read);
int scheduler_pipe_write(int32_t fd, const void* data, uint32_t len, uint32_t* out_written);
int scheduler_thread_create_current(uint64_t entry, uint64_t arg, uint64_t stack_top, int32_t* out_tid);
int scheduler_thread_join_current(int32_t tid, int32_t* exit_code);
int scheduler_thread_join_poll_current(int32_t tid, int32_t* exit_code);
int scheduler_set_limits_current(const myaos_proc_limits_t* limits);
int scheduler_get_limits_current(myaos_proc_limits_t* out);
int scheduler_shm_create(const char* name, uint32_t size, int32_t* out_id);
int scheduler_shm_open(const char* name, int32_t* out_id);
int scheduler_shm_read(int32_t id, uint32_t offset, void* out_buf, uint32_t max_len, uint32_t* out_len);
int scheduler_shm_write(int32_t id, uint32_t offset, const void* data, uint32_t len, uint32_t* out_written);
int scheduler_shm_close(int32_t id);
int scheduler_stdout_redirect_info(char* out_path, size_t out_size, uint8_t* out_append, uint8_t* out_truncate_now);
void scheduler_stdout_redirect_mark_truncated(void);
int scheduler_mem_map_current(uint64_t size, uint8_t writable, uint64_t* out_addr);
int scheduler_mem_unmap_current(uint64_t addr);
int scheduler_mem_swap_out_current(uint64_t addr);
int scheduler_mem_swap_in_current(uint64_t addr);
uint32_t scheduler_current_uid(void);
int scheduler_login_current(const char* user_name);
int scheduler_current_is_user_mode(void);
int scheduler_current_user_region(uint64_t* out_base, uint64_t* out_end);
const char* scheduler_current_cwd(void);
int scheduler_setcwd_current(const char* abs_path);
int scheduler_getcwd_current(char* out, size_t out_size);

#endif
