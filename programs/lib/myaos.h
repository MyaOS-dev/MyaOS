#ifndef PROGRAM_MYAOS_H
#define PROGRAM_MYAOS_H

#include <myaos/syscall.h>
#include <stddef.h>
#include <stdint.h>

static inline int64_t mya_syscall(
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

static inline size_t mya_strlen(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static inline int mya_streq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static inline int mya_strto_u64(const char* text, uint64_t* out) {
    uint64_t value = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }

    for (size_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
    }

    *out = value;
    return 0;
}

static inline void mya_u32_to_dec(uint32_t value, char* out, size_t out_size) {
    char rev[16];
    size_t n = 0;
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static inline void mya_u64_to_dec(uint64_t value, char* out, size_t out_size) {
    char rev[32];
    size_t n = 0;
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static inline void mya_puts(const char* text) {
    (void)mya_syscall(MYAOS_SYS_CONSOLE_WRITE, (uint64_t)(uintptr_t)text, mya_strlen(text), 0, 0, 0);
}

static inline void mya_putln(const char* text) {
    mya_puts(text);
    mya_puts("\n");
}

static inline void mya_put_u32(uint32_t value) {
    char buf[16];
    mya_u32_to_dec(value, buf, sizeof(buf));
    mya_puts(buf);
}

static inline void mya_put_u64(uint64_t value) {
    char buf[32];
    mya_u64_to_dec(value, buf, sizeof(buf));
    mya_puts(buf);
}

static inline int mya_console_clear(void) {
    return (int)mya_syscall(MYAOS_SYS_CONSOLE_CLEAR, 0, 0, 0, 0, 0);
}

static inline int mya_console_readchar(void) {
    return (int)mya_syscall(MYAOS_SYS_CONSOLE_READCHAR, 0, 0, 0, 0, 0);
}

static inline int mya_input_key_state(uint16_t keycode) {
    return (int)mya_syscall(MYAOS_SYS_INPUT_KEY_STATE, (uint64_t)keycode, 0, 0, 0, 0);
}

static inline int mya_input_keyboard_state(uint8_t* out_keys, uint32_t out_size) {
    return (int)mya_syscall(
        MYAOS_SYS_INPUT_KEYBOARD_STATE,
        (uint64_t)(uintptr_t)out_keys,
        (uint64_t)out_size,
        0,
        0,
        0
    );
}

static inline int mya_input_key_event_read(myaos_input_key_event_t* out_event) {
    return (int)mya_syscall(MYAOS_SYS_INPUT_KEY_EVENT_READ, (uint64_t)(uintptr_t)out_event, 0, 0, 0, 0);
}

static inline int mya_input_wait(uint32_t timeout_ticks, uint32_t* out_events) {
    return (int)mya_syscall(
        MYAOS_SYS_INPUT_WAIT,
        (uint64_t)timeout_ticks,
        (uint64_t)(uintptr_t)out_events,
        0,
        0,
        0
    );
}

static inline int mya_event_poll(uint32_t mask, uint8_t clear, uint32_t* out_events) {
    return (int)mya_syscall(
        MYAOS_SYS_EVENT_POLL,
        (uint64_t)mask,
        (uint64_t)clear,
        (uint64_t)(uintptr_t)out_events,
        0,
        0
    );
}

static inline int mya_fs_list(const char* path, myaos_dirent_t* out, uint32_t max_entries, uint32_t* out_count) {
    return (int)mya_syscall(
        MYAOS_SYS_FS_LIST,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)out,
        (uint64_t)max_entries,
        (uint64_t)(uintptr_t)out_count,
        0
    );
}

static inline int mya_fs_read(const char* path, void* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    return (int)mya_syscall(
        MYAOS_SYS_FS_READ,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)out_buf,
        (uint64_t)out_buf_size,
        (uint64_t)(uintptr_t)out_size,
        0
    );
}

static inline int mya_fs_write(const char* path, const void* data, uint32_t size) {
    return (int)mya_syscall(
        MYAOS_SYS_FS_WRITE,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)data,
        (uint64_t)size,
        0,
        0
    );
}

static inline int mya_fs_mkdir(const char* path) {
    return (int)mya_syscall(MYAOS_SYS_FS_MKDIR, (uint64_t)(uintptr_t)path, 0, 0, 0, 0);
}

static inline int mya_fs_touch(const char* path) {
    return (int)mya_syscall(MYAOS_SYS_FS_TOUCH, (uint64_t)(uintptr_t)path, 0, 0, 0, 0);
}

static inline int mya_fs_remove(const char* path) {
    return (int)mya_syscall(MYAOS_SYS_FS_REMOVE, (uint64_t)(uintptr_t)path, 0, 0, 0, 0);
}

static inline int mya_fs_chdir(const char* path) {
    return (int)mya_syscall(MYAOS_SYS_FS_CHDIR, (uint64_t)(uintptr_t)path, 0, 0, 0, 0);
}

static inline int mya_fs_getcwd(char* out, uint32_t out_size) {
    return (int)mya_syscall(MYAOS_SYS_FS_GETCWD, (uint64_t)(uintptr_t)out, (uint64_t)out_size, 0, 0, 0);
}

static inline int mya_fs_mounts(myaos_mount_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    return (int)mya_syscall(
        MYAOS_SYS_FS_MOUNTS,
        (uint64_t)(uintptr_t)out,
        (uint64_t)max_entries,
        (uint64_t)(uintptr_t)out_count,
        0,
        0
    );
}

static inline int mya_fs_mount(const char* source, const char* path, const char* fs_name) {
    return (int)mya_syscall(
        MYAOS_SYS_FS_MOUNT,
        (uint64_t)(uintptr_t)source,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)fs_name,
        0,
        0
    );
}

static inline int mya_fs_chmod(const char* path, uint32_t mode) {
    return (int)mya_syscall(
        MYAOS_SYS_FS_CHMOD,
        (uint64_t)(uintptr_t)path,
        (uint64_t)mode,
        0,
        0,
        0
    );
}

static inline int mya_fs_chown(const char* path, uint32_t owner_uid) {
    return (int)mya_syscall(
        MYAOS_SYS_FS_CHOWN,
        (uint64_t)(uintptr_t)path,
        (uint64_t)owner_uid,
        0,
        0,
        0
    );
}

static inline int mya_posix_open(const char* path, uint32_t flags, int32_t* out_fd) {
    return (int)mya_syscall(
        MYAOS_SYS_POSIX_OPEN,
        (uint64_t)(uintptr_t)path,
        (uint64_t)flags,
        (uint64_t)(uintptr_t)out_fd,
        0,
        0
    );
}

static inline int mya_posix_close(int32_t fd) {
    return (int)mya_syscall(MYAOS_SYS_POSIX_CLOSE, (uint64_t)fd, 0, 0, 0, 0);
}

static inline int mya_posix_read(int32_t fd, void* out_buf, uint32_t max_len, uint32_t* out_read) {
    return (int)mya_syscall(
        MYAOS_SYS_POSIX_READ,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)out_buf,
        (uint64_t)max_len,
        (uint64_t)(uintptr_t)out_read,
        0
    );
}

static inline int mya_posix_write(int32_t fd, const void* data, uint32_t len, uint32_t* out_written) {
    return (int)mya_syscall(
        MYAOS_SYS_POSIX_WRITE,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)data,
        (uint64_t)len,
        (uint64_t)(uintptr_t)out_written,
        0
    );
}

static inline int mya_posix_lseek(int32_t fd, int64_t offset, uint32_t whence, uint64_t* out_offset) {
    return (int)mya_syscall(
        MYAOS_SYS_POSIX_LSEEK,
        (uint64_t)fd,
        (uint64_t)offset,
        (uint64_t)whence,
        (uint64_t)(uintptr_t)out_offset,
        0
    );
}

static inline int mya_posix_fstat(int32_t fd, myaos_posix_stat_t* out) {
    return (int)mya_syscall(
        MYAOS_SYS_POSIX_FSTAT,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)out,
        0,
        0,
        0
    );
}

static inline int mya_posix_dup2(int32_t old_fd, int32_t new_fd) {
    return (int)mya_syscall(MYAOS_SYS_POSIX_DUP2, (uint64_t)old_fd, (uint64_t)new_fd, 0, 0, 0);
}

static inline int mya_posix_poll(myaos_posix_pollfd_t* fds, uint32_t count, uint32_t timeout_ticks, uint32_t* out_ready) {
    return (int)mya_syscall(
        MYAOS_SYS_POSIX_POLL,
        (uint64_t)(uintptr_t)fds,
        (uint64_t)count,
        (uint64_t)timeout_ticks,
        (uint64_t)(uintptr_t)out_ready,
        0
    );
}

static inline int mya_proc_spawn(const char* path, int argc, const char* const* argv, uint8_t flags, int32_t* out_pid) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_SPAWN,
        (uint64_t)(uintptr_t)path,
        (uint64_t)argc,
        (uint64_t)(uintptr_t)argv,
        (uint64_t)flags,
        (uint64_t)(uintptr_t)out_pid
    );
}

static inline int mya_proc_spawn_ex(
    const char* path,
    int argc,
    const char* const* argv,
    const myaos_spawn_opts_t* opts,
    int32_t* out_pid
) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_SPAWN_EX,
        (uint64_t)(uintptr_t)path,
        (uint64_t)argc,
        (uint64_t)(uintptr_t)argv,
        (uint64_t)(uintptr_t)opts,
        (uint64_t)(uintptr_t)out_pid
    );
}

static inline int mya_proc_spawn_linux(const char* path, int argc, const char* const* argv, int32_t* out_pid) {
    myaos_spawn_opts_t opts;
    opts.flags = MYAOS_SPAWN_LINUX;
    opts.stdout_append = 0u;
    opts.priority = 0u;
    opts.reserved0 = 0u;
    opts.cpu_limit_ticks = 0u;
    opts.stdout_path[0] = '\0';
    return mya_proc_spawn_ex(path, argc, argv, &opts, out_pid);
}

static inline int mya_proc_exec(const char* path, int argc, const char* const* argv) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_EXEC,
        (uint64_t)(uintptr_t)path,
        (uint64_t)argc,
        (uint64_t)(uintptr_t)argv,
        0,
        0
    );
}

static inline int mya_proc_wait(int32_t pid, int32_t* exit_code) {
    return (int)mya_syscall(MYAOS_SYS_PROC_WAIT, (uint64_t)pid, (uint64_t)(uintptr_t)exit_code, 0, 0, 0);
}

static inline int mya_proc_wait_poll(int32_t pid, int32_t* exit_code) {
    return (int)mya_syscall(MYAOS_SYS_PROC_WAIT_POLL, (uint64_t)pid, (uint64_t)(uintptr_t)exit_code, 0, 0, 0);
}

static inline int mya_proc_kill(int32_t pid, int32_t exit_code) {
    return (int)mya_syscall(MYAOS_SYS_PROC_KILL, (uint64_t)pid, (uint64_t)(uint32_t)exit_code, 0, 0, 0);
}

static inline void mya_proc_exit(int32_t code) {
    (void)mya_syscall(MYAOS_SYS_PROC_EXIT, (uint64_t)(uint32_t)code, 0, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("pause");
    }
}

static inline void mya_proc_yield(void) {
    (void)mya_syscall(MYAOS_SYS_PROC_YIELD, 0, 0, 0, 0, 0);
}

static inline void mya_proc_sleep(uint64_t ticks) {
    (void)mya_syscall(MYAOS_SYS_PROC_SLEEP, ticks, 0, 0, 0, 0);
}

static inline uint64_t mya_time_ticks(void) {
    return (uint64_t)mya_syscall(MYAOS_SYS_TIME_TICKS, 0, 0, 0, 0, 0);
}

static inline uint32_t mya_time_freq(void) {
    return (uint32_t)mya_syscall(MYAOS_SYS_TIME_FREQ, 0, 0, 0, 0, 0);
}

static inline int32_t mya_proc_getpid(void) {
    return (int32_t)mya_syscall(MYAOS_SYS_PROC_GETPID, 0, 0, 0, 0, 0);
}

static inline uint32_t mya_sec_whoami(void) {
    return (uint32_t)mya_syscall(MYAOS_SYS_SEC_WHOAMI, 0, 0, 0, 0, 0);
}

static inline int mya_sec_login(const char* user_name) {
    return (int)mya_syscall(MYAOS_SYS_SEC_LOGIN, (uint64_t)(uintptr_t)user_name, 0, 0, 0, 0);
}

static inline int mya_fb_pref_set(uint8_t has_mode, uint32_t mode) {
    return (int)mya_syscall(MYAOS_SYS_FB_PREF_SET, (uint64_t)has_mode, (uint64_t)mode, 0, 0, 0);
}

static inline int mya_fb_pref_get(uint32_t* out_has_mode, uint32_t* out_mode) {
    return (int)mya_syscall(
        MYAOS_SYS_FB_PREF_GET,
        (uint64_t)(uintptr_t)out_has_mode,
        (uint64_t)(uintptr_t)out_mode,
        0,
        0,
        0
    );
}

static inline int mya_gfx_mode_set(uint32_t mode) {
    return (int)mya_syscall(MYAOS_SYS_GFX_MODE_SET, (uint64_t)mode, 0, 0, 0, 0);
}

static inline int mya_gfx_mode_get(uint32_t* out_mode) {
    return (int)mya_syscall(MYAOS_SYS_GFX_MODE_GET, (uint64_t)(uintptr_t)out_mode, 0, 0, 0, 0);
}

static inline int mya_gfx_info_get(myaos_gfx_info_t* out_info) {
    return (int)mya_syscall(MYAOS_SYS_GFX_INFO_GET, (uint64_t)(uintptr_t)out_info, 0, 0, 0, 0);
}

static inline int mya_gfx_draw(const myaos_gfx_object_t* obj) {
    return (int)mya_syscall(MYAOS_SYS_GFX_DRAW, (uint64_t)(uintptr_t)obj, 0, 0, 0, 0);
}

static inline int mya_gfx_draw_batch(const myaos_gfx_object_t* objs, uint32_t count) {
    return (int)mya_syscall(
        MYAOS_SYS_GFX_DRAW_BATCH,
        (uint64_t)(uintptr_t)objs,
        (uint64_t)count,
        0,
        0,
        0
    );
}

static inline int mya_gfx_blit(const myaos_gfx_blit_t* blit) {
    return (int)mya_syscall(MYAOS_SYS_GFX_BLIT, (uint64_t)(uintptr_t)blit, 0, 0, 0, 0);
}

static inline int mya_gfx_present(void) {
    return (int)mya_syscall(MYAOS_SYS_GFX_PRESENT, 0, 0, 0, 0, 0);
}

static inline int mya_gfx_clear(uint32_t color) {
    myaos_gfx_object_t obj;
    obj.type = MYAOS_GFX_OBJ_CLEAR;
    obj.x0 = 0;
    obj.y0 = 0;
    obj.x1 = 0;
    obj.y1 = 0;
    obj.color = color;
    obj.color2 = 0u;
    obj.text_ptr = 0u;
    obj.text_len = 0u;
    obj.flags = 0u;
    return mya_gfx_draw(&obj);
}

static inline int mya_gfx_pixel(int32_t x, int32_t y, uint32_t color) {
    myaos_gfx_object_t obj;
    obj.type = MYAOS_GFX_OBJ_PIXEL;
    obj.x0 = x;
    obj.y0 = y;
    obj.x1 = 0;
    obj.y1 = 0;
    obj.color = color;
    obj.color2 = 0u;
    obj.text_ptr = 0u;
    obj.text_len = 0u;
    obj.flags = 0u;
    return mya_gfx_draw(&obj);
}

static inline int mya_gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    myaos_gfx_object_t obj;
    obj.type = MYAOS_GFX_OBJ_LINE;
    obj.x0 = x0;
    obj.y0 = y0;
    obj.x1 = x1;
    obj.y1 = y1;
    obj.color = color;
    obj.color2 = 0u;
    obj.text_ptr = 0u;
    obj.text_len = 0u;
    obj.flags = 0u;
    return mya_gfx_draw(&obj);
}

static inline int mya_gfx_rect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    myaos_gfx_object_t obj;
    obj.type = MYAOS_GFX_OBJ_RECT;
    obj.x0 = x;
    obj.y0 = y;
    obj.x1 = width;
    obj.y1 = height;
    obj.color = color;
    obj.color2 = 0u;
    obj.text_ptr = 0u;
    obj.text_len = 0u;
    obj.flags = 0u;
    return mya_gfx_draw(&obj);
}

static inline int mya_gfx_frame(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    myaos_gfx_object_t obj;
    obj.type = MYAOS_GFX_OBJ_FRAME;
    obj.x0 = x;
    obj.y0 = y;
    obj.x1 = width;
    obj.y1 = height;
    obj.color = color;
    obj.color2 = 0u;
    obj.text_ptr = 0u;
    obj.text_len = 0u;
    obj.flags = 0u;
    return mya_gfx_draw(&obj);
}

static inline int mya_gfx_text(int32_t x, int32_t y, uint32_t fg, uint32_t bg, const char* text) {
    myaos_gfx_object_t obj;
    const size_t max_len = 240u;
    size_t len = mya_strlen(text);
    if (len > max_len) {
        len = max_len;
    }
    obj.type = MYAOS_GFX_OBJ_TEXT;
    obj.x0 = x;
    obj.y0 = y;
    obj.x1 = 0;
    obj.y1 = 0;
    obj.color = fg;
    obj.color2 = bg;
    obj.text_ptr = (uint64_t)(uintptr_t)text;
    obj.text_len = (uint32_t)len;
    obj.flags = 0u;
    return mya_gfx_draw(&obj);
}

static inline int mya_proc_list(myaos_proc_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_INFO,
        (uint64_t)(uintptr_t)out,
        (uint64_t)max_entries,
        (uint64_t)(uintptr_t)out_count,
        0,
        0
    );
}

static inline int mya_sched_info(myaos_sched_info_t* out) {
    return (int)mya_syscall(MYAOS_SYS_PROC_SCHED, (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

static inline int mya_proc_set_limits(const myaos_proc_limits_t* limits) {
    return (int)mya_syscall(MYAOS_SYS_PROC_SET_LIMITS, (uint64_t)(uintptr_t)limits, 0, 0, 0, 0);
}

static inline int mya_proc_get_limits(myaos_proc_limits_t* out) {
    return (int)mya_syscall(MYAOS_SYS_PROC_GET_LIMITS, (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

static inline int mya_proc_notify(int32_t pid, uint32_t bits) {
    return (int)mya_syscall(MYAOS_SYS_PROC_NOTIFY, (uint64_t)pid, (uint64_t)bits, 0, 0, 0);
}

static inline int mya_proc_notify_poll(uint32_t mask, uint8_t clear, uint32_t* out_bits) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_NOTIFY_POLL,
        (uint64_t)mask,
        (uint64_t)clear,
        (uint64_t)(uintptr_t)out_bits,
        0,
        0
    );
}

static inline int mya_proc_notify_wait(uint32_t mask, uint32_t timeout_ticks, uint8_t clear, uint32_t* out_bits) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_NOTIFY_WAIT,
        (uint64_t)mask,
        (uint64_t)timeout_ticks,
        (uint64_t)clear,
        (uint64_t)(uintptr_t)out_bits,
        0
    );
}

static inline int mya_proc_msg_send(int32_t pid, const char* msg) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_MSG_SEND,
        (uint64_t)pid,
        (uint64_t)(uintptr_t)msg,
        (uint64_t)mya_strlen(msg),
        0,
        0
    );
}

static inline int mya_proc_msg_recv(char* out_buf, uint32_t max_len, uint32_t* out_len, int32_t* out_from) {
    return (int)mya_syscall(
        MYAOS_SYS_PROC_MSG_RECV,
        (uint64_t)(uintptr_t)out_buf,
        (uint64_t)max_len,
        (uint64_t)(uintptr_t)out_len,
        (uint64_t)(uintptr_t)out_from,
        0
    );
}

static inline int mya_pipe_create(int32_t* out_read_fd, int32_t* out_write_fd) {
    return (int)mya_syscall(
        MYAOS_SYS_PIPE_CREATE,
        (uint64_t)(uintptr_t)out_read_fd,
        (uint64_t)(uintptr_t)out_write_fd,
        0,
        0,
        0
    );
}

static inline int mya_pipe_close(int32_t fd) {
    return (int)mya_syscall(MYAOS_SYS_PIPE_CLOSE, (uint64_t)fd, 0, 0, 0, 0);
}

static inline int mya_pipe_read(int32_t fd, void* out_buf, uint32_t max_len, uint32_t* out_read) {
    return (int)mya_syscall(
        MYAOS_SYS_PIPE_READ,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)out_buf,
        (uint64_t)max_len,
        (uint64_t)(uintptr_t)out_read,
        0
    );
}

static inline int mya_pipe_write(int32_t fd, const void* data, uint32_t len, uint32_t* out_written) {
    return (int)mya_syscall(
        MYAOS_SYS_PIPE_WRITE,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)data,
        (uint64_t)len,
        (uint64_t)(uintptr_t)out_written,
        0
    );
}

typedef void (*mya_thread_entry_t)(void*);

static inline int mya_thread_create(mya_thread_entry_t entry, void* arg, void* stack_top, int32_t* out_tid) {
    return (int)mya_syscall(
        MYAOS_SYS_THREAD_CREATE,
        (uint64_t)(uintptr_t)entry,
        (uint64_t)(uintptr_t)arg,
        (uint64_t)(uintptr_t)stack_top,
        (uint64_t)(uintptr_t)out_tid,
        0
    );
}

static inline int mya_thread_join(int32_t tid, int32_t* out_exit_code) {
    return (int)mya_syscall(
        MYAOS_SYS_THREAD_JOIN,
        (uint64_t)tid,
        (uint64_t)(uintptr_t)out_exit_code,
        0,
        0,
        0
    );
}

static inline int mya_thread_join_poll(int32_t tid, int32_t* out_exit_code) {
    return (int)mya_syscall(
        MYAOS_SYS_THREAD_JOIN_POLL,
        (uint64_t)tid,
        (uint64_t)(uintptr_t)out_exit_code,
        0,
        0,
        0
    );
}

static inline int mya_shm_create(const char* name, uint32_t size, int32_t* out_id) {
    return (int)mya_syscall(
        MYAOS_SYS_SHM_CREATE,
        (uint64_t)(uintptr_t)name,
        (uint64_t)size,
        (uint64_t)(uintptr_t)out_id,
        0,
        0
    );
}

static inline int mya_shm_open(const char* name, int32_t* out_id) {
    return (int)mya_syscall(
        MYAOS_SYS_SHM_OPEN,
        (uint64_t)(uintptr_t)name,
        (uint64_t)(uintptr_t)out_id,
        0,
        0,
        0
    );
}

static inline int mya_shm_read(int32_t id, uint32_t offset, void* out_buf, uint32_t max_len, uint32_t* out_len) {
    return (int)mya_syscall(
        MYAOS_SYS_SHM_READ,
        (uint64_t)id,
        (uint64_t)offset,
        (uint64_t)(uintptr_t)out_buf,
        (uint64_t)max_len,
        (uint64_t)(uintptr_t)out_len
    );
}

static inline int mya_shm_write(int32_t id, uint32_t offset, const void* data, uint32_t len, uint32_t* out_written) {
    return (int)mya_syscall(
        MYAOS_SYS_SHM_WRITE,
        (uint64_t)id,
        (uint64_t)offset,
        (uint64_t)(uintptr_t)data,
        (uint64_t)len,
        (uint64_t)(uintptr_t)out_written
    );
}

static inline int mya_shm_close(int32_t id) {
    return (int)mya_syscall(MYAOS_SYS_SHM_CLOSE, (uint64_t)id, 0, 0, 0, 0);
}

static inline int mya_sock_open(uint16_t local_port, int32_t* out_fd) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_OPEN,
        (uint64_t)local_port,
        (uint64_t)(uintptr_t)out_fd,
        0,
        0,
        0
    );
}

static inline int mya_sock_open_ex(uint8_t proto, uint16_t local_port, int32_t* out_fd) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_OPEN_EX,
        (uint64_t)proto,
        (uint64_t)local_port,
        (uint64_t)(uintptr_t)out_fd,
        0,
        0
    );
}

static inline int mya_sock_bind(int32_t fd, uint16_t local_port) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_BIND,
        (uint64_t)fd,
        (uint64_t)local_port,
        0,
        0,
        0
    );
}

static inline int mya_sock_listen(int32_t fd, uint16_t backlog) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_LISTEN,
        (uint64_t)fd,
        (uint64_t)backlog,
        0,
        0,
        0
    );
}

static inline int mya_sock_accept(int32_t fd, int32_t* out_fd, uint16_t* out_peer_port) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_ACCEPT,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)out_fd,
        (uint64_t)(uintptr_t)out_peer_port,
        0,
        0
    );
}

static inline int mya_sock_connect(int32_t fd, uint16_t dst_port) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_CONNECT,
        (uint64_t)fd,
        (uint64_t)dst_port,
        0,
        0,
        0
    );
}

static inline int mya_sock_connect4(int32_t fd, uint32_t dst_ip, uint16_t dst_port) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_CONNECT4,
        (uint64_t)fd,
        (uint64_t)dst_ip,
        (uint64_t)dst_port,
        0,
        0
    );
}

static inline int mya_sock_close(int32_t fd) {
    return (int)mya_syscall(MYAOS_SYS_SOCK_CLOSE, (uint64_t)fd, 0, 0, 0, 0);
}

static inline int mya_sock_send(
    int32_t fd,
    uint16_t dst_port,
    const void* data,
    uint32_t len,
    uint32_t* out_written
) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_SEND,
        (uint64_t)fd,
        (uint64_t)dst_port,
        (uint64_t)(uintptr_t)data,
        (uint64_t)len,
        (uint64_t)(uintptr_t)out_written
    );
}

static inline int mya_sock_sendto(
    int32_t fd,
    uint16_t dst_port,
    const void* data,
    uint32_t len,
    uint32_t* out_written
) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_SEND_TO,
        (uint64_t)fd,
        (uint64_t)dst_port,
        (uint64_t)(uintptr_t)data,
        (uint64_t)len,
        (uint64_t)(uintptr_t)out_written
    );
}

static inline int mya_sock_recv(
    int32_t fd,
    void* out_buf,
    uint32_t max_len,
    uint32_t* out_len,
    uint16_t* out_src_port
) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_RECV,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)out_buf,
        (uint64_t)max_len,
        (uint64_t)(uintptr_t)out_len,
        (uint64_t)(uintptr_t)out_src_port
    );
}

static inline int mya_sock_recvfrom(
    int32_t fd,
    void* out_buf,
    uint32_t max_len,
    uint32_t* out_len,
    uint16_t* out_src_port
) {
    return (int)mya_syscall(
        MYAOS_SYS_SOCK_RECV_FROM,
        (uint64_t)fd,
        (uint64_t)(uintptr_t)out_buf,
        (uint64_t)max_len,
        (uint64_t)(uintptr_t)out_len,
        (uint64_t)(uintptr_t)out_src_port
    );
}

static inline int mya_net_info(myaos_netinfo_t* out) {
    return (int)mya_syscall(MYAOS_SYS_NET_INFO, (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

static inline int mya_net_send_udp4(
    uint32_t dst_ip,
    uint16_t dst_port,
    uint16_t src_port,
    const void* data,
    uint32_t len
) {
    return (int)mya_syscall(
        MYAOS_SYS_NET_SEND_UDP4,
        (uint64_t)dst_ip,
        (uint64_t)dst_port,
        (uint64_t)src_port,
        (uint64_t)(uintptr_t)data,
        (uint64_t)len
    );
}

static inline int mya_net_ping4(
    uint32_t dst_ip,
    uint16_t ident,
    uint16_t seq,
    uint32_t timeout_polls,
    uint32_t* out_rtt_ticks
) {
    return (int)mya_syscall(
        MYAOS_SYS_NET_PING4,
        (uint64_t)dst_ip,
        (uint64_t)ident,
        (uint64_t)seq,
        (uint64_t)timeout_polls,
        (uint64_t)(uintptr_t)out_rtt_ticks
    );
}

static inline int mya_dev_list(myaos_device_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    return (int)mya_syscall(
        MYAOS_SYS_DEV_LIST,
        (uint64_t)(uintptr_t)out,
        (uint64_t)max_entries,
        (uint64_t)(uintptr_t)out_count,
        0,
        0
    );
}

static inline int mya_disk_list(myaos_disk_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    return (int)mya_syscall(
        MYAOS_SYS_DEV_DISKS,
        (uint64_t)(uintptr_t)out,
        (uint64_t)max_entries,
        (uint64_t)(uintptr_t)out_count,
        0,
        0
    );
}

static inline int mya_hotplug_ramdisk(uint64_t size_bytes, uint32_t block_size, uint32_t* out_disk_id) {
    return (int)mya_syscall(
        MYAOS_SYS_DEV_HOTPLUG_RAMDISK,
        size_bytes,
        (uint64_t)block_size,
        (uint64_t)(uintptr_t)out_disk_id,
        0,
        0
    );
}

static inline void* mya_mem_map(uint64_t size, uint8_t flags) {
    int64_t rc = mya_syscall(MYAOS_SYS_MEM_MAP, size, (uint64_t)flags, 0, 0, 0);
    if (rc <= 0) {
        return NULL;
    }
    return (void*)(uintptr_t)(uint64_t)rc;
}

static inline int mya_mem_unmap(void* addr) {
    return (int)mya_syscall(MYAOS_SYS_MEM_UNMAP, (uint64_t)(uintptr_t)addr, 0, 0, 0, 0);
}

static inline int mya_mem_swap_out(void* addr) {
    return (int)mya_syscall(MYAOS_SYS_MEM_SWAP_OUT, (uint64_t)(uintptr_t)addr, 0, 0, 0, 0);
}

static inline int mya_mem_swap_in(void* addr) {
    return (int)mya_syscall(MYAOS_SYS_MEM_SWAP_IN, (uint64_t)(uintptr_t)addr, 0, 0, 0, 0);
}

static inline int mya_swap_info(myaos_swap_info_t* out) {
    return (int)mya_syscall(MYAOS_SYS_MEM_SWAP_INFO, (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

static inline int mya_meminfo(myaos_meminfo_t* out) {
    return (int)mya_syscall(MYAOS_SYS_SYS_MEMINFO, (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

static inline int mya_mod_list(myaos_module_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    return (int)mya_syscall(
        MYAOS_SYS_MOD_LIST,
        (uint64_t)(uintptr_t)out,
        (uint64_t)max_entries,
        (uint64_t)(uintptr_t)out_count,
        0,
        0
    );
}

static inline int mya_mod_load(const char* name) {
    return (int)mya_syscall(MYAOS_SYS_MOD_LOAD, (uint64_t)(uintptr_t)name, 0, 0, 0, 0);
}

static inline int mya_mod_load_file(const char* path) {
    return (int)mya_syscall(MYAOS_SYS_MOD_LOAD_FILE, (uint64_t)(uintptr_t)path, 0, 0, 0, 0);
}

static inline int mya_mod_unload(const char* name) {
    return (int)mya_syscall(MYAOS_SYS_MOD_UNLOAD, (uint64_t)(uintptr_t)name, 0, 0, 0, 0);
}

static inline void mya_halt(void) {
    (void)mya_syscall(MYAOS_SYS_SYS_HALT, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("pause");
    }
}

static inline void mya_reboot(void) {
    (void)mya_syscall(MYAOS_SYS_SYS_REBOOT, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("pause");
    }
}

static inline void mya_shutdown(void) {
    (void)mya_syscall(MYAOS_SYS_SYS_SHUTDOWN, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ __volatile__("pause");
    }
}

static inline uint32_t mya_utf8_prev_start(const char* text, uint32_t pos) {
    if (!text || pos == 0u) {
        return 0u;
    }
    pos--;
    while (pos > 0u && (((uint8_t)text[pos] & 0xC0u) == 0x80u)) {
        pos--;
    }
    return pos;
}

static inline int mya_console_readline(char* out, uint32_t out_size, uint8_t echo) {
    uint32_t len = 0;

    if (!out || out_size == 0) {
        return -1;
    }

    out[0] = '\0';
    for (;;) {
        int ch = mya_console_readchar();
        if (ch == 0) {
            mya_proc_yield();
            continue;
        }

        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            out[len] = '\0';
            if (echo) {
                mya_puts("\n");
            }
            return (int)len;
        }
        if (ch == '\b' || ch == 127) {
            if (len > 0) {
                len = mya_utf8_prev_start(out, len);
                out[len] = '\0';
                if (echo) {
                    mya_puts("\b");
                }
            }
            continue;
        }
        if (((uint8_t)ch < 32u) || ((uint8_t)ch == 127u)) {
            continue;
        }
        if (len + 1 < out_size) {
            out[len++] = (char)ch;
            out[len] = '\0';
            if (echo) {
                char tmp[2];
                tmp[0] = (char)ch;
                tmp[1] = '\0';
                mya_puts(tmp);
            }
        }
    }
}

static inline const char* mya_proc_state_name(uint8_t state) {
    switch (state) {
    case MYAOS_PROC_RUNNING:
        return "running";
    case MYAOS_PROC_READY:
        return "ready";
    case MYAOS_PROC_BLOCKED:
        return "blocked";
    case MYAOS_PROC_SLEEPING:
        return "sleeping";
    case MYAOS_PROC_ZOMBIE:
        return "zombie";
    default:
        return "none";
    }
}

static inline const char* mya_node_type_suffix(uint8_t type) {
    switch (type) {
    case MYAOS_NODE_DIR:
        return "/";
    case MYAOS_NODE_MOUNT:
        return "@";
    default:
        return "";
    }
}

#endif
