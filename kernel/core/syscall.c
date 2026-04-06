#include "syscall.h"
#include "console.h"
#include "blockio.h"
#include "device.h"
#include "fs_driver.h"
#include "heap.h"
#include "keyboard.h"
#include "module.h"
#include "net.h"
#include "paging.h"
#include "pmm.h"
#include "power.h"
#include "scheduler.h"
#include "swap.h"
#include "timer.h"
#include "vfs.h"
#include <myaos/syscall.h>
#include <stddef.h>
#include <stdint.h>

static boot_info_t* g_boot;

#define SYSCALL_REDIRECT_MAX_FILE (256u * 1024u)
#define SYSCALL_ARGC_MAX 32
#define SYSCALL_USER_STR_MAX 256u

static void mem_copy(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static int user_range_valid(uint64_t addr, uint64_t size, uint8_t writable) {
    uint64_t user_base;
    uint64_t user_end;
    uint64_t range_end;

    if (!scheduler_current_is_user_mode()) {
        return 1;
    }
    if (size == 0u) {
        return 1;
    }
    if (addr == 0u || addr + size < addr) {
        return 0;
    }
    if (scheduler_current_user_region(&user_base, &user_end) != 0) {
        return 0;
    }

    range_end = addr + size;
    if (addr < user_base || range_end > user_end) {
        return 0;
    }
    return paging_user_range_accessible(addr, size, writable);
}

static int user_ptr_readable(const void* ptr, uint64_t size) {
    return user_range_valid((uint64_t)(uintptr_t)ptr, size, 0u);
}

static int user_ptr_writable(void* ptr, uint64_t size) {
    return user_range_valid((uint64_t)(uintptr_t)ptr, size, 1u);
}

static int user_cstr_valid(const char* text, size_t max_len) {
    uint64_t base;

    if (!text || max_len == 0u) {
        return 0;
    }
    if (!scheduler_current_is_user_mode()) {
        return 1;
    }

    base = (uint64_t)(uintptr_t)text;
    for (size_t i = 0; i < max_len; i++) {
        if (!user_range_valid(base + i, 1u, 0u)) {
            return 0;
        }
        if (text[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int user_argv_valid(int argc, const char* const* argv) {
    if (argc < 0 || argc > SYSCALL_ARGC_MAX) {
        return 0;
    }
    if (argc == 0) {
        return 1;
    }
    if (!argv) {
        return 0;
    }
    if (!scheduler_current_is_user_mode()) {
        return 1;
    }
    if (!user_ptr_readable(argv, (uint64_t)(argc + 1) * sizeof(char*))) {
        return 0;
    }

    for (int i = 0; i < argc; i++) {
        const char* arg = argv[i];
        if (arg && !user_cstr_valid(arg, SYSCALL_USER_STR_MAX)) {
            return 0;
        }
    }
    return 1;
}

static int append_file_bytes(const char* cwd, const char* path, const uint8_t* data, uint32_t size, uint8_t truncate_now) {
    uint8_t empty = 0;
    uint8_t* old_buf;
    uint8_t* merged_buf;
    uint32_t old_size = 0;
    uint32_t merged_size;
    int rc;

    if (!cwd || !path || (size != 0 && !data)) {
        return -1;
    }

    (void)vfs_touch(cwd, path);
    if (truncate_now) {
        if (vfs_write_file(cwd, path, &empty, 0) != 0) {
            return -1;
        }
        scheduler_stdout_redirect_mark_truncated();
    }

    if (size == 0) {
        return 0;
    }

    old_buf = (uint8_t*)kmalloc(SYSCALL_REDIRECT_MAX_FILE);
    if (!old_buf) {
        return -1;
    }

    rc = vfs_read_file(cwd, path, old_buf, SYSCALL_REDIRECT_MAX_FILE, &old_size);
    if (rc != 0) {
        old_size = 0;
    }
    if (old_size > SYSCALL_REDIRECT_MAX_FILE || size > SYSCALL_REDIRECT_MAX_FILE ||
        old_size + size > SYSCALL_REDIRECT_MAX_FILE) {
        kfree(old_buf);
        return -1;
    }

    merged_size = old_size + size;
    merged_buf = (uint8_t*)kmalloc(merged_size ? merged_size : 1u);
    if (!merged_buf) {
        kfree(old_buf);
        return -1;
    }

    if (old_size > 0) {
        mem_copy(merged_buf, old_buf, old_size);
    }
    mem_copy(merged_buf + old_size, data, size);
    rc = vfs_write_file(cwd, path, merged_buf, merged_size);

    kfree(merged_buf);
    kfree(old_buf);
    return rc;
}

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
    if (number == MYAOS_SYS_PROC_YIELD) {
        scheduler_yield_current();
        return 0;
    }
    if (number == MYAOS_SYS_PROC_GETPID) {
        return scheduler_current_pid();
    }

    switch (number) {
    case MYAOS_SYS_CONSOLE_WRITE: {
        char out_path[MYAOS_PATH_MAX];
        uint8_t out_append = 0;
        uint8_t out_truncate_now = 0;
        const uint8_t* text = (const uint8_t*)(uintptr_t)arg0;
        uint32_t text_size = (uint32_t)arg1;
        int redirect = scheduler_stdout_redirect_info(
            out_path,
            sizeof(out_path),
            &out_append,
            &out_truncate_now
        );
        if (text_size != 0u && !user_ptr_readable(text, text_size)) {
            return -1;
        }

        if (redirect > 0) {
            int rc = append_file_bytes(
                scheduler_current_cwd(),
                out_path,
                text,
                text_size,
                out_truncate_now
            );
            if (rc != 0) {
                return -1;
            }
            (void)out_append;
            return 0;
        }
        console_write_len((const char*)text, (size_t)text_size);
        return 0;
    }
    case MYAOS_SYS_CONSOLE_CLEAR:
        console_reset();
        return 0;
    case MYAOS_SYS_CONSOLE_READCHAR:
        return (uint8_t)keyboard_read_char();
    case MYAOS_SYS_FS_LIST: {
        size_t count = 0;
        uint32_t out_count = 0;
        if (arg3 == 0 || (arg2 != 0 && arg1 == 0)) {
            return -1;
        }
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        if (arg2 != 0 &&
            !user_ptr_writable((void*)(uintptr_t)arg1, (uint64_t)arg2 * sizeof(myaos_dirent_t))) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        int rc = vfs_list(
            scheduler_current_cwd(),
            (const char*)(uintptr_t)arg0,
            (myaos_dirent_t*)(uintptr_t)arg1,
            (size_t)arg2,
            &count
        );
        if (rc != 0) {
            return -1;
        }
        out_count = (uint32_t)count;
        *((uint32_t*)(uintptr_t)arg3) = out_count;
        return 0;
    }
    case MYAOS_SYS_FS_READ:
        if (arg3 == 0 || (arg2 != 0 && arg1 == 0)) {
            return -1;
        }
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        if (arg2 != 0 && !user_ptr_writable((void*)(uintptr_t)arg1, (uint32_t)arg2)) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        return vfs_read_file(
            scheduler_current_cwd(),
            (const char*)(uintptr_t)arg0,
            (uint8_t*)(uintptr_t)arg1,
            (uint32_t)arg2,
            (uint32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_FS_WRITE:
        if (arg2 != 0 && arg1 == 0) {
            return -1;
        }
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        if (arg2 != 0 && !user_ptr_readable((const void*)(uintptr_t)arg1, (uint32_t)arg2)) {
            return -1;
        }
        return vfs_write_file(
            scheduler_current_cwd(),
            (const char*)(uintptr_t)arg0,
            (const uint8_t*)(uintptr_t)arg1,
            (uint32_t)arg2
        );
    case MYAOS_SYS_FS_MKDIR:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        return vfs_mkdir(scheduler_current_cwd(), (const char*)(uintptr_t)arg0);
    case MYAOS_SYS_FS_TOUCH:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        return vfs_touch(scheduler_current_cwd(), (const char*)(uintptr_t)arg0);
    case MYAOS_SYS_FS_CHDIR: {
        char abs_path[MYAOS_PATH_MAX];
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        int rc = vfs_resolve_cwd(scheduler_current_cwd(), (const char*)(uintptr_t)arg0, abs_path, sizeof(abs_path));
        if (rc != 0 || vfs_is_dir("/", abs_path) <= 0) {
            return -1;
        }
        return scheduler_setcwd_current(abs_path);
    }
    case MYAOS_SYS_FS_GETCWD:
        if (arg1 != 0 && !user_ptr_writable((void*)(uintptr_t)arg0, (uint64_t)arg1)) {
            return -1;
        }
        return scheduler_getcwd_current((char*)(uintptr_t)arg0, (size_t)arg1);
    case MYAOS_SYS_FS_MOUNTS:
        if (arg2 == 0 || (arg1 != 0 && arg0 == 0)) {
            return -1;
        }
        if (arg1 != 0 &&
            !user_ptr_writable((void*)(uintptr_t)arg0, (uint64_t)arg1 * sizeof(myaos_mount_info_t))) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint32_t))) {
            return -1;
        }
        return vfs_list_mounts((myaos_mount_info_t*)(uintptr_t)arg0, (uint32_t)arg1, (uint32_t*)(uintptr_t)arg2);
    case MYAOS_SYS_FS_MOUNT:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX) ||
            !user_cstr_valid((const char*)(uintptr_t)arg1, MYAOS_PATH_MAX) ||
            (arg2 != 0 && !user_cstr_valid((const char*)(uintptr_t)arg2, MYAOS_NAME_MAX))) {
            return -1;
        }
        return fs_driver_mount_source(
            (const char*)(uintptr_t)arg0,
            (const char*)(uintptr_t)arg1,
            (const char*)(uintptr_t)arg2
        );
    case MYAOS_SYS_FS_REMOVE:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        return vfs_remove(scheduler_current_cwd(), (const char*)(uintptr_t)arg0);
    case MYAOS_SYS_PROC_SPAWN: {
        int argc = (int)arg1;
        const char* const* argv = (const char* const*)(uintptr_t)arg2;
        myaos_spawn_opts_t opts;

        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        if (!user_argv_valid(argc, argv)) {
            return -1;
        }
        if (arg4 != 0 && !user_ptr_writable((void*)(uintptr_t)arg4, sizeof(int32_t))) {
            return -1;
        }

        opts.flags = (uint32_t)arg3;
        opts.stdout_append = 0;
        opts.priority = 0;
        opts.reserved0 = 0;
        opts.cpu_limit_ticks = 0;
        opts.stdout_path[0] = '\0';
        return scheduler_spawn_program_ex(
            scheduler_current_cwd(),
            (const char*)(uintptr_t)arg0,
            argc,
            argv,
            &opts,
            (int32_t*)(uintptr_t)arg4
        );
    }
    case MYAOS_SYS_PROC_SPAWN_EX: {
        int argc = (int)arg1;
        const char* const* argv = (const char* const*)(uintptr_t)arg2;
        const myaos_spawn_opts_t* opts_ptr = (const myaos_spawn_opts_t*)(uintptr_t)arg3;
        myaos_spawn_opts_t opts_local;

        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        if (!user_argv_valid(argc, argv)) {
            return -1;
        }
        if (arg4 != 0 && !user_ptr_writable((void*)(uintptr_t)arg4, sizeof(int32_t))) {
            return -1;
        }
        if (opts_ptr) {
            if (!user_ptr_readable(opts_ptr, sizeof(*opts_ptr))) {
                return -1;
            }
            mem_copy(&opts_local, opts_ptr, sizeof(opts_local));
            opts_local.stdout_path[MYAOS_PATH_MAX - 1u] = '\0';
            opts_ptr = &opts_local;
        }

        return scheduler_spawn_program_ex(
            scheduler_current_cwd(),
            (const char*)(uintptr_t)arg0,
            argc,
            argv,
            opts_ptr,
            (int32_t*)(uintptr_t)arg4
        );
    }
    case MYAOS_SYS_PROC_EXEC:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX) ||
            !user_argv_valid((int)arg1, (const char* const*)(uintptr_t)arg2)) {
            return -1;
        }
        return scheduler_exec_current(
            (const char*)(uintptr_t)arg0,
            (int)arg1,
            (const char* const*)(uintptr_t)arg2
        );
    case MYAOS_SYS_PROC_WAIT:
        if (arg1 != 0 && !user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_wait((int32_t)arg0, (int32_t*)(uintptr_t)arg1);
    case MYAOS_SYS_PROC_WAIT_POLL:
        if (arg1 != 0 && !user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_wait_poll((int32_t)arg0, (int32_t*)(uintptr_t)arg1);
    case MYAOS_SYS_PROC_KILL:
        return scheduler_kill_pid((int32_t)arg0, (int32_t)arg1);
    case MYAOS_SYS_MEM_MAP: {
        uint64_t mapped_addr = 0;
        if (!scheduler_current_is_user_mode()) {
            return -1;
        }
        if (scheduler_mem_map_current(arg0, (uint8_t)(arg1 & MYAOS_MEM_MAP_WRITABLE), &mapped_addr) != 0) {
            return -1;
        }
        return (int64_t)mapped_addr;
    }
    case MYAOS_SYS_MEM_UNMAP:
        if (!scheduler_current_is_user_mode()) {
            return -1;
        }
        return scheduler_mem_unmap_current(arg0);
    case MYAOS_SYS_MEM_SWAP_OUT:
        if (!scheduler_current_is_user_mode() || !module_is_loaded("swap")) {
            return -1;
        }
        return scheduler_mem_swap_out_current(arg0);
    case MYAOS_SYS_MEM_SWAP_IN:
        if (!scheduler_current_is_user_mode() || !module_is_loaded("swap")) {
            return -1;
        }
        return scheduler_mem_swap_in_current(arg0);
    case MYAOS_SYS_MEM_SWAP_INFO:
        if (!user_ptr_writable((void*)(uintptr_t)arg0, sizeof(myaos_swap_info_t))) {
            return -1;
        }
        swap_get_info((myaos_swap_info_t*)(uintptr_t)arg0);
        return 0;
    case MYAOS_SYS_PIPE_CREATE:
        if (!user_ptr_writable((void*)(uintptr_t)arg0, sizeof(int32_t)) ||
            !user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_pipe_create((int32_t*)(uintptr_t)arg0, (int32_t*)(uintptr_t)arg1);
    case MYAOS_SYS_PIPE_CLOSE:
        return scheduler_pipe_close((int32_t)arg0);
    case MYAOS_SYS_PIPE_READ:
        if (arg3 == 0 || (arg2 != 0 && arg1 == 0)) {
            return -1;
        }
        if (arg2 != 0 && !user_ptr_writable((void*)(uintptr_t)arg1, (uint32_t)arg2)) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_pipe_read(
            (int32_t)arg0,
            (void*)(uintptr_t)arg1,
            (uint32_t)arg2,
            (uint32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_PIPE_WRITE:
        if (arg3 == 0 || (arg2 != 0 && arg1 == 0)) {
            return -1;
        }
        if (arg2 != 0 && !user_ptr_readable((const void*)(uintptr_t)arg1, (uint32_t)arg2)) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_pipe_write(
            (int32_t)arg0,
            (const void*)(uintptr_t)arg1,
            (uint32_t)arg2,
            (uint32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_THREAD_CREATE:
        if (!scheduler_current_is_user_mode()) {
            return -1;
        }
        if (!user_range_valid(arg0, 1u, 0u)) {
            return -1;
        }
        if (arg2 < sizeof(uint64_t) || !user_range_valid(arg2 - sizeof(uint64_t), sizeof(uint64_t), 1u)) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg3, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_thread_create_current(
            arg0,
            arg1,
            arg2,
            (int32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_THREAD_JOIN:
        if (!scheduler_current_is_user_mode()) {
            return -1;
        }
        if (arg1 != 0u && !user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_thread_join_current(
            (int32_t)arg0,
            (int32_t*)(uintptr_t)arg1
        );
    case MYAOS_SYS_THREAD_JOIN_POLL:
        if (!scheduler_current_is_user_mode()) {
            return -1;
        }
        if (arg1 != 0u && !user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_thread_join_poll_current(
            (int32_t)arg0,
            (int32_t*)(uintptr_t)arg1
        );
    case MYAOS_SYS_SHM_CREATE:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX) ||
            !user_ptr_writable((void*)(uintptr_t)arg2, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_shm_create(
            (const char*)(uintptr_t)arg0,
            (uint32_t)arg1,
            (int32_t*)(uintptr_t)arg2
        );
    case MYAOS_SYS_SHM_OPEN:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX) ||
            !user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_shm_open(
            (const char*)(uintptr_t)arg0,
            (int32_t*)(uintptr_t)arg1
        );
    case MYAOS_SYS_SHM_READ:
        if ((arg3 != 0u && !user_ptr_writable((void*)(uintptr_t)arg2, (uint32_t)arg3)) ||
            !user_ptr_writable((void*)(uintptr_t)arg4, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_shm_read(
            (int32_t)arg0,
            (uint32_t)arg1,
            (void*)(uintptr_t)arg2,
            (uint32_t)arg3,
            (uint32_t*)(uintptr_t)arg4
        );
    case MYAOS_SYS_SHM_WRITE:
        if ((arg3 != 0u && !user_ptr_readable((const void*)(uintptr_t)arg2, (uint32_t)arg3)) ||
            !user_ptr_writable((void*)(uintptr_t)arg4, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_shm_write(
            (int32_t)arg0,
            (uint32_t)arg1,
            (const void*)(uintptr_t)arg2,
            (uint32_t)arg3,
            (uint32_t*)(uintptr_t)arg4
        );
    case MYAOS_SYS_SHM_CLOSE:
        return scheduler_shm_close((int32_t)arg0);
    case MYAOS_SYS_SOCK_OPEN:
        if (!user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t))) {
            return -1;
        }
        return net_socket_open(
            scheduler_current_pid(),
            (uint16_t)arg0,
            (int32_t*)(uintptr_t)arg1
        );
    case MYAOS_SYS_SOCK_OPEN_EX:
        if (!user_ptr_writable((void*)(uintptr_t)arg2, sizeof(int32_t))) {
            return -1;
        }
        return net_socket_open_ex(
            scheduler_current_pid(),
            (uint8_t)arg0,
            (uint16_t)arg1,
            (int32_t*)(uintptr_t)arg2
        );
    case MYAOS_SYS_SOCK_BIND:
        return net_socket_bind(
            scheduler_current_pid(),
            (int32_t)arg0,
            (uint16_t)arg1
        );
    case MYAOS_SYS_SOCK_LISTEN:
        return net_socket_listen(
            scheduler_current_pid(),
            (int32_t)arg0,
            (uint16_t)arg1
        );
    case MYAOS_SYS_SOCK_ACCEPT:
        if (!user_ptr_writable((void*)(uintptr_t)arg1, sizeof(int32_t)) ||
            (arg2 != 0u && !user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint16_t)))) {
            return -1;
        }
        return net_socket_accept(
            scheduler_current_pid(),
            (int32_t)arg0,
            (int32_t*)(uintptr_t)arg1,
            (uint16_t*)(uintptr_t)arg2
        );
    case MYAOS_SYS_SOCK_CONNECT:
        return net_socket_connect(
            scheduler_current_pid(),
            (int32_t)arg0,
            (uint16_t)arg1
        );
    case MYAOS_SYS_SOCK_CLOSE:
        return net_socket_close(scheduler_current_pid(), (int32_t)arg0);
    case MYAOS_SYS_SOCK_SEND:
    case MYAOS_SYS_SOCK_SEND_TO:
        if ((arg3 != 0u && !user_ptr_readable((const void*)(uintptr_t)arg2, (uint32_t)arg3)) ||
            !user_ptr_writable((void*)(uintptr_t)arg4, sizeof(uint32_t))) {
            return -1;
        }
        return net_socket_sendto(
            scheduler_current_pid(),
            (int32_t)arg0,
            (uint16_t)arg1,
            (const void*)(uintptr_t)arg2,
            (uint32_t)arg3,
            (uint32_t*)(uintptr_t)arg4
        );
    case MYAOS_SYS_SOCK_RECV:
    case MYAOS_SYS_SOCK_RECV_FROM:
        if ((arg2 != 0u && !user_ptr_writable((void*)(uintptr_t)arg1, (uint32_t)arg2)) ||
            !user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t)) ||
            (arg4 != 0u && !user_ptr_writable((void*)(uintptr_t)arg4, sizeof(uint16_t)))) {
            return -1;
        }
        return net_socket_recvfrom(
            scheduler_current_pid(),
            (int32_t)arg0,
            (void*)(uintptr_t)arg1,
            (uint32_t)arg2,
            (uint32_t*)(uintptr_t)arg3,
            (uint16_t*)(uintptr_t)arg4
        );
    case MYAOS_SYS_NET_INFO:
        if (!user_ptr_writable((void*)(uintptr_t)arg0, sizeof(myaos_netinfo_t))) {
            return -1;
        }
        return net_info((myaos_netinfo_t*)(uintptr_t)arg0);
    case MYAOS_SYS_NET_SEND_UDP4:
        if (arg4 != 0u && !user_ptr_readable((const void*)(uintptr_t)arg3, (uint32_t)arg4)) {
            return -1;
        }
        return net_send_udp4(
            (uint32_t)arg0,
            (uint16_t)arg1,
            (uint16_t)arg2,
            (const void*)(uintptr_t)arg3,
            (uint32_t)arg4
        );
    case MYAOS_SYS_SEC_WHOAMI:
        return (int64_t)scheduler_current_uid();
    case MYAOS_SYS_SEC_LOGIN:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX)) {
            return -1;
        }
        return scheduler_login_current((const char*)(uintptr_t)arg0);
    case MYAOS_SYS_PROC_EXIT:
        scheduler_exit_current((int32_t)arg0);
        return 0;
    case MYAOS_SYS_PROC_SLEEP:
        scheduler_sleep_current(arg0);
        return 0;
    case MYAOS_SYS_PROC_INFO:
        if (arg2 == 0 || (arg1 != 0 && arg0 == 0)) {
            return -1;
        }
        if (arg1 != 0 &&
            !user_ptr_writable((void*)(uintptr_t)arg0, (uint64_t)arg1 * sizeof(myaos_proc_info_t))) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_list_processes(
            (myaos_proc_info_t*)(uintptr_t)arg0,
            (uint32_t)arg1,
            (uint32_t*)(uintptr_t)arg2
        );
    case MYAOS_SYS_PROC_SCHED: {
        myaos_sched_info_t* out = (myaos_sched_info_t*)(uintptr_t)arg0;
        if (!out || !user_ptr_writable(out, sizeof(*out))) {
            return -1;
        }
        out->timer_hz = timer_hz();
        out->timer_ticks = timer_ticks();
        out->process_count = scheduler_process_count();
        out->current_pid = (uint32_t)scheduler_current_pid();
        return 0;
    }
    case MYAOS_SYS_PROC_SET_LIMITS:
        if (!scheduler_current_is_user_mode() ||
            !user_ptr_readable((const void*)(uintptr_t)arg0, sizeof(myaos_proc_limits_t))) {
            return -1;
        }
        return scheduler_set_limits_current((const myaos_proc_limits_t*)(uintptr_t)arg0);
    case MYAOS_SYS_PROC_GET_LIMITS:
        if (!scheduler_current_is_user_mode() ||
            !user_ptr_writable((void*)(uintptr_t)arg0, sizeof(myaos_proc_limits_t))) {
            return -1;
        }
        return scheduler_get_limits_current((myaos_proc_limits_t*)(uintptr_t)arg0);
    case MYAOS_SYS_PROC_NOTIFY:
        return scheduler_notify((int32_t)arg0, (uint32_t)arg1);
    case MYAOS_SYS_PROC_NOTIFY_POLL:
        if (arg2 == 0 || !user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_notify_poll((uint32_t)arg0, (uint8_t)arg1, (uint32_t*)(uintptr_t)arg2);
    case MYAOS_SYS_PROC_NOTIFY_WAIT:
        if (arg3 == 0 || !user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_notify_wait((uint32_t)arg0, (uint32_t)arg1, (uint8_t)arg2, (uint32_t*)(uintptr_t)arg3);
    case MYAOS_SYS_PROC_MSG_SEND:
        if (arg2 == 0 || !user_ptr_readable((const void*)(uintptr_t)arg1, (uint32_t)arg2)) {
            return -1;
        }
        return scheduler_ipc_send((int32_t)arg0, (const char*)(uintptr_t)arg1, (uint32_t)arg2);
    case MYAOS_SYS_PROC_MSG_RECV:
        if (arg0 == 0 || arg2 == 0 ||
            !user_ptr_writable((void*)(uintptr_t)arg0, (uint32_t)arg1) ||
            !user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint32_t)) ||
            (arg3 != 0 && !user_ptr_writable((void*)(uintptr_t)arg3, sizeof(int32_t)))) {
            return -1;
        }
        return scheduler_ipc_recv(
            (char*)(uintptr_t)arg0,
            (uint32_t)arg1,
            (uint32_t*)(uintptr_t)arg2,
            (int32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_DEV_LIST:
        if (arg2 == 0 || (arg1 != 0 && arg0 == 0)) {
            return -1;
        }
        if (arg1 != 0 &&
            !user_ptr_writable((void*)(uintptr_t)arg0, (uint64_t)arg1 * sizeof(myaos_device_info_t))) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint32_t))) {
            return -1;
        }
        return device_list(
            (myaos_device_info_t*)(uintptr_t)arg0,
            (uint32_t)arg1,
            (uint32_t*)(uintptr_t)arg2
        );
    case MYAOS_SYS_DEV_DISKS: {
        myaos_disk_info_t* out = (myaos_disk_info_t*)(uintptr_t)arg0;
        uint32_t max_entries = (uint32_t)arg1;
        uint32_t* out_count = (uint32_t*)(uintptr_t)arg2;
        uint32_t count = 0;

        if (!out_count || (max_entries != 0 && !out)) {
            return -1;
        }
        if (max_entries != 0 &&
            !user_ptr_writable(out, (uint64_t)max_entries * sizeof(myaos_disk_info_t))) {
            return -1;
        }
        if (!user_ptr_writable(out_count, sizeof(uint32_t))) {
            return -1;
        }
        int rc = blockio_list_disks(out, max_entries, &count);

        if (rc != 0) {
            return rc;
        }
        for (uint32_t i = 0; i < count; i++) {
            (void)fs_driver_probe_disk(out[i].id, out[i].fs_name, sizeof(out[i].fs_name));
        }
        if (out_count) {
            *out_count = count;
        }
        return 0;
    }
    case MYAOS_SYS_DEV_HOTPLUG_RAMDISK: {
        uint32_t disk_id = 0;

        if (!module_is_loaded("hotplug")) {
            return -1;
        }
        if (arg2 == 0 || !user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint32_t))) {
            return -1;
        }
        if (blockio_hotplug_ramdisk(arg0, (uint32_t)arg1, &disk_id) != 0) {
            return -1;
        }
        *((uint32_t*)(uintptr_t)arg2) = disk_id;
        return 0;
    }
    case MYAOS_SYS_MOD_LIST:
        if (arg2 == 0 || (arg1 != 0 && arg0 == 0)) {
            return -1;
        }
        if (arg1 != 0 &&
            !user_ptr_writable((void*)(uintptr_t)arg0, (uint64_t)arg1 * sizeof(myaos_module_info_t))) {
            return -1;
        }
        if (!user_ptr_writable((void*)(uintptr_t)arg2, sizeof(uint32_t))) {
            return -1;
        }
        return module_list(
            (myaos_module_info_t*)(uintptr_t)arg0,
            (uint32_t)arg1,
            (uint32_t*)(uintptr_t)arg2
        );
    case MYAOS_SYS_MOD_LOAD:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX)) {
            return -1;
        }
        return module_set_loaded((const char*)(uintptr_t)arg0, 1u);
    case MYAOS_SYS_MOD_UNLOAD:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX)) {
            return -1;
        }
        return module_set_loaded((const char*)(uintptr_t)arg0, 0u);
    case MYAOS_SYS_SYS_MEMINFO: {
        myaos_meminfo_t* out = (myaos_meminfo_t*)(uintptr_t)arg0;
        heap_stats_t heap;
        paging_stats_t paging;
        pmm_stats_t pmm;
        myaos_swap_info_t swap;

        if (!out || !user_ptr_writable(out, sizeof(*out))) {
            return -1;
        }

        pmm_get_stats(&pmm);
        heap_get_stats(&heap);
        paging_get_stats(&paging);
        swap_get_info(&swap);

        out->timer_ticks = timer_ticks();
        out->pmm_managed_pages = pmm.managed_pages;
        out->pmm_free_pages = pmm.free_pages;
        out->pmm_allocated_pages = pmm.allocated_pages;
        out->heap_page_count = heap.page_count;
        out->heap_alloc_count = heap.alloc_count;
        out->heap_bytes_used = heap.bytes_used;
        out->heap_bytes_capacity = heap.bytes_capacity;
        out->paging_mapped_bytes = paging.mapped_bytes;
        out->paging_table_pages = paging.table_pages;
        out->paging_cr3 = paging.cr3;
        out->swap_total_bytes = swap.total_bytes;
        out->swap_used_bytes = swap.used_bytes;
        out->swap_slots = swap.slot_count;
        out->swap_used_slots = swap.used_slots;
        return 0;
    }
    case MYAOS_SYS_SYS_HALT:
        (void)vfs_sync_all();
        (void)device_sync_all();
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    case MYAOS_SYS_SYS_REBOOT:
        (void)vfs_sync_all();
        (void)device_sync_all();
        if (g_boot) {
            power_reboot(g_boot);
        }
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    case MYAOS_SYS_SYS_SHUTDOWN:
        (void)vfs_sync_all();
        (void)device_sync_all();
        if (g_boot) {
            power_shutdown(g_boot);
        }
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    default:
        return -1;
    }
}
