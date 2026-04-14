#include "syscall.h"
#include "console.h"
#include "blockio.h"
#include "device.h"
#include "fs_driver.h"
#include "graphics.h"
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
#define SYSCALL_GFX_TEXT_MAX 240u
#define SYSCALL_GFX_BATCH_MAX 256u
#define SYSCALL_GFX_BLIT_MAX_BYTES (128u * 1024u * 1024u)

static void mem_copy(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static void mem_zero(void* dst, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    for (size_t i = 0; i < size; i++) {
        out[i] = 0u;
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

static int syscall_console_write_bytes(const uint8_t* text, uint32_t text_size) {
    char out_path[MYAOS_PATH_MAX];
    uint8_t out_append = 0;
    uint8_t out_truncate_now = 0;
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

static int syscall_gfx_draw_one(const myaos_gfx_object_t* obj) {
    char text_buf[SYSCALL_GFX_TEXT_MAX + 1u];

    if (!g_boot || !obj) {
        return -1;
    }

    switch (obj->type) {
    case MYAOS_GFX_OBJ_CLEAR:
        clear_screen(g_boot, obj->color);
        return 0;
    case MYAOS_GFX_OBJ_PIXEL:
        if (obj->x0 >= 0 && obj->y0 >= 0) {
            put_pixel(g_boot, (uint32_t)obj->x0, (uint32_t)obj->y0, obj->color);
        }
        return 0;
    case MYAOS_GFX_OBJ_LINE:
        draw_line(g_boot, obj->x0, obj->y0, obj->x1, obj->y1, obj->color);
        return 0;
    case MYAOS_GFX_OBJ_RECT:
        fill_rect(g_boot, obj->x0, obj->y0, obj->x1, obj->y1, obj->color);
        return 0;
    case MYAOS_GFX_OBJ_FRAME:
        draw_rect_frame(g_boot, obj->x0, obj->y0, obj->x1, obj->y1, obj->color);
        return 0;
    case MYAOS_GFX_OBJ_TEXT:
        if (obj->text_len == 0u) {
            return 0;
        }
        if (obj->text_ptr == 0u || obj->text_len > SYSCALL_GFX_TEXT_MAX ||
            !user_ptr_readable((const void*)(uintptr_t)obj->text_ptr, obj->text_len)) {
            return -1;
        }
        mem_copy(text_buf, (const void*)(uintptr_t)obj->text_ptr, obj->text_len);
        text_buf[obj->text_len] = '\0';
        if (obj->x0 < 0 || obj->y0 < 0) {
            return 0;
        }
        draw_string(
            g_boot,
            (uint32_t)obj->x0,
            (uint32_t)obj->y0,
            text_buf,
            obj->color,
            obj->color2
        );
        return 0;
    default:
        return -1;
    }
}

static uint32_t syscall_gfx_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (g_boot && g_boot->fb.format == 0u) {
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    }
    return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}

static int syscall_gfx_blit(const myaos_gfx_blit_t* blit) {
    const uint8_t* src;
    framebuffer_t* fb;
    uint32_t* dst_base;
    uint64_t row_bytes;
    uint64_t total_bytes;
    uint32_t src_stride;
    uint32_t src_bpp = 0u;

    if (!g_boot || !blit) {
        return -1;
    }
    if (blit->width == 0u || blit->height == 0u) {
        return 0;
    }
    if (blit->src_ptr == 0u) {
        return -1;
    }

    switch (blit->src_format) {
    case MYAOS_GFX_SRC_BGR24:
    case MYAOS_GFX_SRC_RGB24:
        src_bpp = 3u;
        break;
    case MYAOS_GFX_SRC_BGRA32:
    case MYAOS_GFX_SRC_RGBA32:
        src_bpp = 4u;
        break;
    default:
        return -1;
    }

    row_bytes = (uint64_t)blit->width * (uint64_t)src_bpp;
    if (row_bytes > 0xFFFFFFFFull) {
        return -1;
    }

    src_stride = blit->src_stride;
    if (src_stride == 0u) {
        src_stride = (uint32_t)row_bytes;
    }
    if ((uint64_t)src_stride < row_bytes) {
        return -1;
    }

    total_bytes = (uint64_t)src_stride * (uint64_t)blit->height;
    if (total_bytes == 0u || total_bytes > SYSCALL_GFX_BLIT_MAX_BYTES) {
        return -1;
    }

    src = (const uint8_t*)(uintptr_t)blit->src_ptr;
    if (!user_ptr_readable(src, total_bytes)) {
        return -1;
    }

    fb = &g_boot->fb;
    if (fb->base == 0u || fb->width == 0u || fb->height == 0u || fb->pixels_per_scanline == 0u) {
        return -1;
    }
    dst_base = graphics_draw_buffer(g_boot);
    if (!dst_base) {
        return -1;
    }

    for (uint32_t y = 0u; y < blit->height; y++) {
        int64_t dst_y = (int64_t)blit->dst_y + (int64_t)y;
        uint32_t src_row = y;
        const uint8_t* src_row_ptr;
        int64_t clip_x0 = 0;
        int64_t clip_x1 = (int64_t)blit->width;
        uint32_t draw_count;
        uint32_t dst_x;
        const uint8_t* src_px;
        uint32_t* dst_px;

        if (dst_y < 0 || dst_y >= (int64_t)fb->height) {
            continue;
        }
        if ((blit->flags & MYAOS_GFX_BLIT_FLIP_Y) != 0u) {
            src_row = blit->height - 1u - y;
        }
        src_row_ptr = src + (uint64_t)src_row * (uint64_t)src_stride;

        if (blit->dst_x < 0) {
            clip_x0 = -(int64_t)blit->dst_x;
        }
        if ((int64_t)blit->dst_x + clip_x1 > (int64_t)fb->width) {
            clip_x1 = (int64_t)fb->width - (int64_t)blit->dst_x;
        }
        if (clip_x0 >= clip_x1) {
            continue;
        }

        draw_count = (uint32_t)(clip_x1 - clip_x0);
        dst_x = (uint32_t)((int64_t)blit->dst_x + clip_x0);
        src_px = src_row_ptr + (uint64_t)clip_x0 * (uint64_t)src_bpp;
        dst_px = dst_base + (uint32_t)dst_y * fb->pixels_per_scanline + dst_x;

        for (uint32_t x = 0u; x < draw_count; x++) {
            uint8_t r = 0u;
            uint8_t g = 0u;
            uint8_t b = 0u;

            switch (blit->src_format) {
            case MYAOS_GFX_SRC_BGR24:
            case MYAOS_GFX_SRC_BGRA32:
                b = src_px[0];
                g = src_px[1];
                r = src_px[2];
                break;
            case MYAOS_GFX_SRC_RGB24:
            case MYAOS_GFX_SRC_RGBA32:
                r = src_px[0];
                g = src_px[1];
                b = src_px[2];
                break;
            default:
                return -1;
            }

            dst_px[x] = syscall_gfx_pack_rgb(r, g, b);
            src_px += src_bpp;
        }
    }

    return 0;
}

void syscall_set_boot_info(boot_info_t* boot) {
    g_boot = boot;
}

#define LINUX_EPERM 1
#define LINUX_ENOENT 2
#define LINUX_ESRCH 3
#define LINUX_EINTR 4
#define LINUX_EIO 5
#define LINUX_EBADF 9
#define LINUX_ECHILD 10
#define LINUX_EAGAIN 11
#define LINUX_ENOMEM 12
#define LINUX_EACCES 13
#define LINUX_EFAULT 14
#define LINUX_EMFILE 24
#define LINUX_ENOTDIR 20
#define LINUX_EISDIR 21
#define LINUX_EINVAL 22
#define LINUX_ENOTTY 25
#define LINUX_ESPIPE 29
#define LINUX_ENOSYS 38
#define LINUX_EOVERFLOW 75
#define LINUX_ETIMEDOUT 110

#define LINUX_FD_STDIN 0
#define LINUX_FD_STDOUT 1
#define LINUX_FD_STDERR 2
#define LINUX_FD_PIPE_BASE 1000
#define LINUX_FD_DIR_BASE 2000
#define LINUX_AT_FDCWD (-100)
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100u
#define LINUX_AT_EMPTY_PATH 0x1000u

#define LINUX_ARCH_SET_FS 0x1002u
#define LINUX_ARCH_GET_FS 0x1003u

#define LINUX_AT_NULL 0u
#define LINUX_AT_PAGESZ 6u

#define LINUX_O_NONBLOCK 0x800u
#define LINUX_O_CLOEXEC 0x80000u

#define LINUX_MAP_ANONYMOUS 0x20u

#define LINUX_TCGETS 0x5401u
#define LINUX_TCSETS 0x5402u
#define LINUX_TCSETSW 0x5403u
#define LINUX_TCSETSF 0x5404u
#define LINUX_TIOCGPGRP 0x540Fu
#define LINUX_TIOCSPGRP 0x5410u
#define LINUX_TIOCGWINSZ 0x5413u
#define LINUX_TIOCSWINSZ 0x5414u
#define LINUX_FIONREAD 0x541Bu
#define LINUX_TIOCGSID 0x5429u
#define LINUX_VT_OPENQRY 0x5600u
#define LINUX_VT_GETMODE 0x5601u
#define LINUX_VT_SETMODE 0x5602u
#define LINUX_VT_GETSTATE 0x5603u
#define LINUX_VT_RELDISP 0x5605u
#define LINUX_VT_ACTIVATE 0x5606u
#define LINUX_VT_WAITACTIVE 0x5607u
#define LINUX_KDGKBTYPE 0x4B33u
#define LINUX_KDSETMODE 0x4B3Au
#define LINUX_KDGETMODE 0x4B3Bu
#define LINUX_KD_TEXT 0u
#define LINUX_KD_GRAPHICS 1u
#define LINUX_VT_AUTO 0u

#define LINUX_F_DUPFD 0
#define LINUX_F_GETFD 1
#define LINUX_F_SETFD 2
#define LINUX_F_GETFL 3
#define LINUX_F_SETFL 4
#define LINUX_F_DUPFD_CLOEXEC 1030

#define LINUX_FUTEX_WAIT 0u
#define LINUX_FUTEX_WAKE 1u
#define LINUX_FUTEX_WAIT_BITSET 9u
#define LINUX_FUTEX_WAKE_BITSET 10u
#define LINUX_FUTEX_CMD_MASK 0x7Fu

#define LINUX_RLIMIT_STACK 3u
#define LINUX_RLIMIT_NOFILE 7u
#define LINUX_RLIMIT_AS 9u
#define LINUX_RLIM_INFINITY 0xFFFFFFFFFFFFFFFFull

#define LINUX_S_IFDIR 0040000u
#define LINUX_S_IFIFO 0010000u
#define LINUX_S_IFREG 0100000u

#define LINUX_DT_UNKNOWN 0u
#define LINUX_DT_REG 8u
#define LINUX_DT_DIR 4u

#define LINUX_CLONE_VM 0x00000100u
#define LINUX_CLONE_SIGHAND 0x00000800u
#define LINUX_CLONE_VFORK 0x00004000u
#define LINUX_CLONE_THREAD 0x00010000u
#define LINUX_CLONE_SETTLS 0x00080000u
#define LINUX_CLONE_PARENT_SETTID 0x00100000u
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000u
#define LINUX_CLONE_CHILD_SETTID 0x01000000u

#define LINUX_SIGCHLD 17u
#define LINUX_WNOHANG 0x1u
#define LINUX_WAIT_SCAN_MAX 256u

#define LINUX_PR_SET_NAME 15u
#define LINUX_PR_GET_NAME 16u

#define LINUX_STATX_TYPE 0x00000001u
#define LINUX_STATX_MODE 0x00000002u
#define LINUX_STATX_NLINK 0x00000004u
#define LINUX_STATX_UID 0x00000008u
#define LINUX_STATX_GID 0x00000010u
#define LINUX_STATX_ATIME 0x00000020u
#define LINUX_STATX_MTIME 0x00000040u
#define LINUX_STATX_CTIME 0x00000080u
#define LINUX_STATX_INO 0x00000100u
#define LINUX_STATX_SIZE 0x00000200u
#define LINUX_STATX_BLOCKS 0x00000400u
#define LINUX_STATX_BASIC_STATS                                                            \
    (LINUX_STATX_TYPE | LINUX_STATX_MODE | LINUX_STATX_NLINK | LINUX_STATX_UID |         \
     LINUX_STATX_GID | LINUX_STATX_ATIME | LINUX_STATX_MTIME | LINUX_STATX_CTIME |       \
     LINUX_STATX_INO | LINUX_STATX_SIZE | LINUX_STATX_BLOCKS)

#define LINUX_DIR_CACHE_MAX 64u
#define LINUX_DIR_LIST_MAX 128u

typedef struct {
    uint64_t iov_base;
    uint64_t iov_len;
} linux_iovec_t;

typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_line;
    uint8_t c_cc[19];
} linux_termios_t;

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} linux_winsize_t;

typedef struct {
    uint8_t mode;
    uint8_t waitv;
    int16_t relsig;
    int16_t acqsig;
    int16_t frsig;
    int16_t reserved0;
} linux_vt_mode_t;

typedef struct {
    uint16_t v_active;
    uint16_t v_signal;
    uint16_t v_state;
} linux_vt_stat_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} linux_timespec_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} linux_timeval_t;

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    uint64_t st_atime_nsec;
    int64_t st_mtime;
    uint64_t st_mtime_nsec;
    int64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t reserved[3];
} linux_stat_t;

typedef struct {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t reserved;
} linux_statx_timestamp_t;

typedef struct {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t reserved0;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    linux_statx_timestamp_t stx_atime;
    linux_statx_timestamp_t stx_btime;
    linux_statx_timestamp_t stx_ctime;
    linux_statx_timestamp_t stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t stx_mnt_id;
    uint32_t stx_dio_mem_align;
    uint32_t stx_dio_offset_align;
    uint64_t reserved2[12];
} linux_statx_t;

typedef struct {
    int32_t fd;
    int16_t events;
    int16_t revents;
} linux_pollfd_t;

typedef struct {
    uint64_t rlim_cur;
    uint64_t rlim_max;
} linux_rlimit_t;

typedef struct {
    uint8_t used;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t owner_pid;
    uint32_t cursor;
    uint32_t reserved2;
    char abs_path[MYAOS_PATH_MAX];
} linux_dir_cache_t;

typedef struct __attribute__((packed)) {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[1];
} linux_dirent64_t;

static linux_dir_cache_t g_linux_dir_cache[LINUX_DIR_CACHE_MAX];
static linux_vt_mode_t g_linux_vt_mode = {
    .mode = LINUX_VT_AUTO,
    .waitv = 0u,
    .relsig = 0,
    .acqsig = 0,
    .frsig = 0,
    .reserved0 = 0,
};

typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} linux_utsname_t;

#define LINUX_FDSET_MAX_FDS 1024u
#define LINUX_FDSET_MAX_BYTES (LINUX_FDSET_MAX_FDS / 8u)

static int64_t linux_errno(int err) {
    return -(int64_t)err;
}

static uint64_t linux_align_up(uint64_t value, uint64_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static size_t linux_cstr_len(const char* s) {
    size_t n = 0u;
    if (!s) {
        return 0u;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int linux_build_boot_path(const char* abs_path, char* out, size_t out_size) {
    const char* prefix = "/boot";
    size_t pos = 0u;

    if (!abs_path || !out || out_size == 0u || abs_path[0] != '/') {
        return -1;
    }
    if (abs_path[1] == 'b' && abs_path[2] == 'o' && abs_path[3] == 'o' && abs_path[4] == 't' &&
        (abs_path[5] == '/' || abs_path[5] == '\0')) {
        return -1;
    }

    for (size_t i = 0u; prefix[i] != '\0'; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = prefix[i];
    }
    for (size_t i = 0u; abs_path[i] != '\0'; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = abs_path[i];
    }
    out[pos] = '\0';
    return 0;
}

static int linux_resolve_with_boot_fallback(const char* cwd, const char* path, char* out_abs, size_t out_size) {
    char boot_path[MYAOS_PATH_MAX];

    if (!path || !out_abs || out_size == 0u) {
        return -1;
    }
    if (vfs_resolve_cwd(cwd, path, out_abs, out_size) == 0) {
        return 0;
    }
    if (path[0] != '/' || linux_build_boot_path(path, boot_path, sizeof(boot_path)) != 0) {
        return -1;
    }
    return vfs_resolve_cwd(cwd, boot_path, out_abs, out_size);
}

static int linux_streq(const char* a, const char* b) {
    size_t i = 0u;
    if (!a || !b) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int linux_path_is_console_tty(const char* abs_path) {
    const char* rest;

    if (!abs_path) {
        return 0;
    }
    if (linux_streq(abs_path, "/dev/tty") ||
        linux_streq(abs_path, "/dev/console")) {
        return 1;
    }

    rest = "/dev/tty";
    for (size_t i = 0u; rest[i] != '\0'; i++) {
        if (abs_path[i] != rest[i]) {
            return 0;
        }
    }
    abs_path += linux_cstr_len(rest);
    if (*abs_path == '\0') {
        return 1;
    }
    while (*abs_path >= '0' && *abs_path <= '9') {
        abs_path++;
    }
    return *abs_path == '\0';
}

static int32_t linux_fd_to_mya_file(int32_t linux_fd) {
    if (linux_fd < 3 || linux_fd >= LINUX_FD_PIPE_BASE) {
        return -1;
    }
    return linux_fd - 2;
}

static int32_t linux_fd_from_mya_file(int32_t mya_fd) {
    if (mya_fd <= 0) {
        return -1;
    }
    return mya_fd + 2;
}

static int32_t linux_fd_to_mya_pipe(int32_t linux_fd) {
    if (linux_fd < LINUX_FD_PIPE_BASE || linux_fd >= LINUX_FD_DIR_BASE) {
        return -1;
    }
    return linux_fd - LINUX_FD_PIPE_BASE;
}

static int32_t linux_fd_from_mya_pipe(int32_t mya_fd) {
    if (mya_fd <= 0) {
        return -1;
    }
    return LINUX_FD_PIPE_BASE + mya_fd;
}

static int linux_fd_is_dir(int32_t linux_fd) {
    return (linux_fd >= LINUX_FD_DIR_BASE &&
            linux_fd < (int32_t)(LINUX_FD_DIR_BASE + (int32_t)LINUX_DIR_CACHE_MAX));
}

static int linux_dir_cache_slot_from_fd(int32_t linux_fd) {
    if (!linux_fd_is_dir(linux_fd)) {
        return -1;
    }
    return linux_fd - LINUX_FD_DIR_BASE;
}

static linux_dir_cache_t* linux_dir_cache_lookup(uint32_t pid, int32_t linux_fd) {
    int slot = linux_dir_cache_slot_from_fd(linux_fd);
    if (slot < 0 || slot >= (int)LINUX_DIR_CACHE_MAX) {
        return NULL;
    }
    if (!g_linux_dir_cache[(uint32_t)slot].used || g_linux_dir_cache[(uint32_t)slot].owner_pid != pid) {
        return NULL;
    }
    return &g_linux_dir_cache[(uint32_t)slot];
}

static int32_t linux_dir_cache_alloc(uint32_t pid, const char* abs_path) {
    for (uint32_t i = 0u; i < LINUX_DIR_CACHE_MAX; i++) {
        linux_dir_cache_t* d = &g_linux_dir_cache[i];
        if (d->used) {
            continue;
        }
        d->used = 1u;
        d->reserved0 = 0u;
        d->reserved1 = 0u;
        d->owner_pid = pid;
        d->cursor = 0u;
        d->reserved2 = 0u;
        mem_zero(d->abs_path, sizeof(d->abs_path));
        {
            size_t len = linux_cstr_len(abs_path);
            if (len >= sizeof(d->abs_path)) {
                len = sizeof(d->abs_path) - 1u;
            }
            mem_copy(d->abs_path, abs_path, len);
            d->abs_path[len] = '\0';
        }
        return (int32_t)(LINUX_FD_DIR_BASE + (int32_t)i);
    }
    return -1;
}

static void linux_dir_cache_close(uint32_t pid, int32_t linux_fd) {
    linux_dir_cache_t* d = linux_dir_cache_lookup(pid, linux_fd);
    if (!d) {
        return;
    }
    mem_zero(d, sizeof(*d));
}

static void linux_dir_cache_close_all(uint32_t pid) {
    for (uint32_t i = 0u; i < LINUX_DIR_CACHE_MAX; i++) {
        if (g_linux_dir_cache[i].used && g_linux_dir_cache[i].owner_pid == pid) {
            mem_zero(&g_linux_dir_cache[i], sizeof(g_linux_dir_cache[i]));
        }
    }
}

static uint32_t linux_open_flags_to_mya(uint64_t linux_flags) {
    uint32_t out = 0u;
    uint32_t acc = (uint32_t)(linux_flags & 0x3u);

    if (acc == 0u) {
        out |= MYAOS_POSIX_O_RDONLY;
    } else if (acc == 1u) {
        out |= MYAOS_POSIX_O_WRONLY;
    } else {
        out |= MYAOS_POSIX_O_RDWR;
    }
    if ((linux_flags & 0x40u) != 0u) {
        out |= MYAOS_POSIX_O_CREAT;
    }
    if ((linux_flags & 0x200u) != 0u) {
        out |= MYAOS_POSIX_O_TRUNC;
    }
    if ((linux_flags & 0x400u) != 0u) {
        out |= MYAOS_POSIX_O_APPEND;
    }
    return out;
}

static uint8_t linux_path_exists(const char* cwd, const char* path) {
    char abs_path[MYAOS_PATH_MAX];
    uint8_t one = 0u;
    uint32_t read_size = 0u;

    if (linux_resolve_with_boot_fallback(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return 0u;
    }
    if (vfs_is_dir("/", abs_path) > 0) {
        return 1u;
    }
    if (vfs_read_file("/", abs_path, &one, 1u, &read_size) == 0) {
        return 1u;
    }
    return 0u;
}

static void linux_fill_stat_from_mya(linux_stat_t* out, const myaos_posix_stat_t* in) {
    if (!out || !in) {
        return;
    }
    mem_zero(out, sizeof(*out));
    out->st_mode = in->mode;
    out->st_uid = in->uid;
    out->st_gid = 0u;
    out->st_nlink = 1u;
    out->st_size = (int64_t)in->size;
    out->st_blksize = 4096;
    out->st_blocks = (int64_t)((in->size + 511u) / 512u);
}

static void linux_fill_statx_from_mya(
    linux_statx_t* out,
    const myaos_posix_stat_t* in,
    uint64_t ino_hint,
    uint32_t mask
) {
    if (!out || !in) {
        return;
    }
    mem_zero(out, sizeof(*out));
    out->stx_mask = LINUX_STATX_BASIC_STATS;
    if (mask != 0u) {
        out->stx_mask &= mask | LINUX_STATX_BASIC_STATS;
    }
    out->stx_blksize = 4096u;
    out->stx_nlink = 1u;
    out->stx_uid = in->uid;
    out->stx_gid = 0u;
    out->stx_mode = (uint16_t)(in->mode & 0xFFFFu);
    out->stx_ino = ino_hint ? ino_hint : 1u;
    out->stx_size = in->size;
    out->stx_blocks = (in->size + 511u) / 512u;
    out->stx_attributes = 0u;
    out->stx_attributes_mask = 0u;
}

static int linux_join_paths(const char* base, const char* rel, char* out, size_t out_size) {
    size_t pos = 0u;

    if (!base || !rel || !out || out_size == 0u) {
        return -1;
    }

    for (; base[pos] && pos + 1u < out_size; pos++) {
        out[pos] = base[pos];
    }
    if (base[pos] != '\0') {
        return -1;
    }
    if (pos == 0u) {
        out[pos++] = '/';
    }
    if (pos > 0u && out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = '/';
    }
    {
        size_t i = 0u;
        for (; rel[i] && pos + 1u < out_size; i++) {
            out[pos++] = rel[i];
        }
        if (rel[i] != '\0') {
            return -1;
        }
    }
    out[pos] = '\0';
    return 0;
}

static int linux_resolve_at_path(int32_t dirfd, const char* path, char* out_abs, size_t out_size) {
    linux_dir_cache_t* dirh = NULL;
    char merged[MYAOS_PATH_MAX];

    if (!path || !out_abs || out_size == 0u) {
        return -1;
    }
    if (path[0] == '/') {
        return linux_resolve_with_boot_fallback(scheduler_current_cwd(), path, out_abs, out_size);
    }
    if (dirfd == LINUX_AT_FDCWD) {
        return vfs_resolve_cwd(scheduler_current_cwd(), path, out_abs, out_size);
    }
    if (!linux_fd_is_dir(dirfd)) {
        return -1;
    }
    dirh = linux_dir_cache_lookup((uint32_t)scheduler_current_pid(), dirfd);
    if (!dirh) {
        return -1;
    }
    if (linux_join_paths(dirh->abs_path, path, merged, sizeof(merged)) != 0) {
        return -1;
    }
    return linux_resolve_with_boot_fallback(scheduler_current_cwd(), merged, out_abs, out_size);
}

static int64_t linux_fstat_fd(int32_t fd, linux_stat_t* out) {
    int32_t mya_file_fd = linux_fd_to_mya_file(fd);
    int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
    myaos_posix_stat_t st;

    if (!out || !user_ptr_writable(out, sizeof(*out))) {
        return linux_errno(LINUX_EFAULT);
    }
    mem_zero(&st, sizeof(st));

    if (fd == LINUX_FD_STDIN || fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
        st.mode = 0020666u;
        st.uid = scheduler_current_uid();
        st.size = 0u;
        linux_fill_stat_from_mya(out, &st);
        return 0;
    }
    if (linux_fd_is_dir(fd)) {
        st.mode = LINUX_S_IFDIR | 0555u;
        st.uid = scheduler_current_uid();
        st.size = 0u;
        linux_fill_stat_from_mya(out, &st);
        return 0;
    }
    if (mya_file_fd > 0 && scheduler_posix_fstat(mya_file_fd, &st) == 0) {
        linux_fill_stat_from_mya(out, &st);
        return 0;
    }
    if (mya_pipe_fd > 0) {
        st.mode = LINUX_S_IFIFO | 0666u;
        st.uid = scheduler_current_uid();
        st.size = 0u;
        linux_fill_stat_from_mya(out, &st);
        return 0;
    }

    return linux_errno(LINUX_EBADF);
}

static int64_t linux_statx_fd(int32_t fd, linux_statx_t* out, uint32_t mask) {
    linux_stat_t st;
    myaos_posix_stat_t mya;
    int64_t rc;

    if (!out || !user_ptr_writable(out, sizeof(*out))) {
        return linux_errno(LINUX_EFAULT);
    }

    rc = linux_fstat_fd(fd, &st);
    if (rc != 0) {
        return rc;
    }

    mem_zero(&mya, sizeof(mya));
    mya.mode = st.st_mode;
    mya.uid = st.st_uid;
    mya.size = (st.st_size < 0) ? 0u : (uint64_t)st.st_size;
    linux_fill_statx_from_mya(out, &mya, st.st_ino, mask);
    return 0;
}

static void linux_copy_uts_field(char dst[65], const char* src) {
    uint32_t i = 0u;
    while (i + 1u < 65u && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int64_t linux_time_now_seconds(void) {
    uint32_t hz = timer_hz();
    uint64_t ticks = timer_ticks();
    if (hz == 0u) {
        hz = 100u;
    }
    return (int64_t)(ticks / (uint64_t)hz);
}

static int64_t linux_clock_gettime_write(uint64_t user_ptr) {
    linux_timespec_t* out = (linux_timespec_t*)(uintptr_t)user_ptr;
    uint32_t hz = timer_hz();
    uint64_t ticks = timer_ticks();
    uint64_t sec;
    uint64_t rem;

    if (!out || !user_ptr_writable(out, sizeof(*out))) {
        return linux_errno(LINUX_EFAULT);
    }
    if (hz == 0u) {
        hz = 100u;
    }

    sec = ticks / (uint64_t)hz;
    rem = ticks % (uint64_t)hz;
    out->tv_sec = (int64_t)sec;
    out->tv_nsec = (int64_t)((rem * 1000000000ull) / (uint64_t)hz);
    return 0;
}

static uint32_t linux_timer_hz(void) {
    uint32_t hz = timer_hz();
    return (hz == 0u) ? 100u : hz;
}

static int linux_timespec_to_ticks(const linux_timespec_t* ts, uint64_t* out_ticks) {
    uint64_t sec;
    uint64_t nsec;
    uint64_t ticks;
    uint32_t hz;

    if (!ts || !out_ticks || ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000ll) {
        return -1;
    }

    hz = linux_timer_hz();
    sec = (uint64_t)ts->tv_sec;
    nsec = (uint64_t)ts->tv_nsec;
    if (sec > (~0ull) / (uint64_t)hz) {
        return -1;
    }

    ticks = sec * (uint64_t)hz;
    ticks += (nsec * (uint64_t)hz + 999999999ull) / 1000000000ull;
    if (ticks == 0u && (sec != 0u || nsec != 0u)) {
        ticks = 1u;
    }

    *out_ticks = ticks;
    return 0;
}

static uint64_t linux_millis_to_ticks(int64_t millis, uint8_t* out_infinite) {
    uint32_t hz = linux_timer_hz();
    uint64_t ticks;

    if (out_infinite) {
        *out_infinite = 0u;
    }
    if (millis < 0) {
        if (out_infinite) {
            *out_infinite = 1u;
        }
        return 0u;
    }
    ticks = ((uint64_t)millis * (uint64_t)hz + 999ull) / 1000ull;
    if (ticks == 0u && millis > 0) {
        ticks = 1u;
    }
    return ticks;
}

static int linux_timeval_to_ticks(const linux_timeval_t* tv, uint64_t* out_ticks) {
    uint64_t sec;
    uint64_t usec;
    uint64_t ticks;
    uint32_t hz;

    if (!tv || !out_ticks || tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000ll) {
        return -1;
    }

    hz = linux_timer_hz();
    sec = (uint64_t)tv->tv_sec;
    usec = (uint64_t)tv->tv_usec;
    if (sec > (~0ull) / (uint64_t)hz) {
        return -1;
    }

    ticks = sec * (uint64_t)hz;
    ticks += (usec * (uint64_t)hz + 999999ull) / 1000000ull;
    if (ticks == 0u && (sec != 0u || usec != 0u)) {
        ticks = 1u;
    }

    *out_ticks = ticks;
    return 0;
}

static size_t linux_fdset_bytes(uint32_t nfds) {
    return nfds == 0u ? 0u : (size_t)((nfds + 7u) / 8u);
}

static uint8_t linux_fdset_test(const uint8_t* set, uint32_t fd) {
    if (!set) {
        return 0u;
    }
    return (uint8_t)((set[fd / 8u] >> (fd % 8u)) & 1u);
}

static void linux_fdset_set(uint8_t* set, uint32_t fd) {
    if (!set) {
        return;
    }
    set[fd / 8u] |= (uint8_t)(1u << (fd % 8u));
}

static int linux_poll_fd_once(int32_t fd, uint16_t req, int16_t* out_revents) {
    int32_t mya_file_fd = linux_fd_to_mya_file(fd);
    int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
    int16_t revents = 0;
    myaos_posix_pollfd_t pollfd;
    uint32_t poll_ready = 0u;

    if (!out_revents) {
        return -1;
    }
    if (fd < 0) {
        *out_revents = 0;
        return 0;
    }

    if (fd == LINUX_FD_STDIN) {
        if ((req & MYAOS_POSIX_POLLIN) != 0u && keyboard_event_pending() > 0u) {
            revents |= MYAOS_POSIX_POLLIN;
        }
        if ((req & MYAOS_POSIX_POLLOUT) != 0u) {
            revents |= MYAOS_POSIX_POLLOUT;
        }
        *out_revents = revents;
        return 0;
    }
    if (fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
        if ((req & MYAOS_POSIX_POLLOUT) != 0u) {
            revents |= MYAOS_POSIX_POLLOUT;
        }
        *out_revents = revents;
        return 0;
    }
    if (linux_fd_is_dir(fd)) {
        if ((req & MYAOS_POSIX_POLLIN) != 0u) {
            revents |= MYAOS_POSIX_POLLIN;
        }
        *out_revents = revents;
        return 0;
    }
    if (mya_pipe_fd > 0) {
        if ((req & MYAOS_POSIX_POLLIN) != 0u) {
            revents |= MYAOS_POSIX_POLLIN;
        }
        if ((req & MYAOS_POSIX_POLLOUT) != 0u) {
            revents |= MYAOS_POSIX_POLLOUT;
        }
        *out_revents = revents;
        return 0;
    }
    if (mya_file_fd <= 0) {
        *out_revents = MYAOS_POSIX_POLLERR;
        return -1;
    }

    pollfd.fd = mya_file_fd;
    pollfd.events = (int16_t)req;
    pollfd.revents = 0;
    if (scheduler_posix_poll(&pollfd, 1u, 0u, &poll_ready) == 0) {
        revents |= pollfd.revents;
    } else {
        revents |= MYAOS_POSIX_POLLERR;
        *out_revents = revents;
        return -1;
    }
    (void)poll_ready;
    *out_revents = revents;
    return 0;
}

static int64_t linux_select_common(
    int32_t nfds,
    uint8_t* readfds,
    uint8_t* writefds,
    uint8_t* exceptfds,
    uint8_t wait_forever,
    uint64_t timeout_ticks
) {
    uint8_t orig_read[LINUX_FDSET_MAX_BYTES];
    uint8_t orig_write[LINUX_FDSET_MAX_BYTES];
    uint8_t orig_except[LINUX_FDSET_MAX_BYTES];
    uint8_t out_read[LINUX_FDSET_MAX_BYTES];
    uint8_t out_write[LINUX_FDSET_MAX_BYTES];
    uint8_t out_except[LINUX_FDSET_MAX_BYTES];
    uint64_t deadline = 0u;
    size_t bytes;

    if (nfds < 0 || (uint32_t)nfds > LINUX_FDSET_MAX_FDS) {
        return linux_errno(LINUX_EINVAL);
    }

    bytes = linux_fdset_bytes((uint32_t)nfds);
    if (bytes != 0u) {
        if ((readfds && (!user_ptr_readable(readfds, bytes) || !user_ptr_writable(readfds, bytes))) ||
            (writefds && (!user_ptr_readable(writefds, bytes) || !user_ptr_writable(writefds, bytes))) ||
            (exceptfds && (!user_ptr_readable(exceptfds, bytes) || !user_ptr_writable(exceptfds, bytes)))) {
            return linux_errno(LINUX_EFAULT);
        }
    }

    mem_zero(orig_read, sizeof(orig_read));
    mem_zero(orig_write, sizeof(orig_write));
    mem_zero(orig_except, sizeof(orig_except));
    if (readfds && bytes != 0u) {
        mem_copy(orig_read, readfds, bytes);
    }
    if (writefds && bytes != 0u) {
        mem_copy(orig_write, writefds, bytes);
    }
    if (exceptfds && bytes != 0u) {
        mem_copy(orig_except, exceptfds, bytes);
    }

    if (!wait_forever && timeout_ticks > 0u) {
        deadline = timer_ticks() + timeout_ticks;
    }

    for (;;) {
        int64_t ready = 0;

        mem_zero(out_read, sizeof(out_read));
        mem_zero(out_write, sizeof(out_write));
        mem_zero(out_except, sizeof(out_except));

        for (uint32_t fd = 0u; fd < (uint32_t)nfds; fd++) {
            uint16_t req = 0u;
            int16_t revents = 0;
            uint8_t fd_ready = 0u;

            if (linux_fdset_test(orig_read, fd)) {
                req |= MYAOS_POSIX_POLLIN;
            }
            if (linux_fdset_test(orig_write, fd)) {
                req |= MYAOS_POSIX_POLLOUT;
            }
            if (req == 0u && !linux_fdset_test(orig_except, fd)) {
                continue;
            }

            (void)linux_poll_fd_once((int32_t)fd, req, &revents);
            if (linux_fdset_test(orig_read, fd) &&
                (revents & (MYAOS_POSIX_POLLIN | MYAOS_POSIX_POLLERR)) != 0) {
                linux_fdset_set(out_read, fd);
                fd_ready = 1u;
            }
            if (linux_fdset_test(orig_write, fd) &&
                (revents & (MYAOS_POSIX_POLLOUT | MYAOS_POSIX_POLLERR)) != 0) {
                linux_fdset_set(out_write, fd);
                fd_ready = 1u;
            }
            if (linux_fdset_test(orig_except, fd) &&
                (revents & MYAOS_POSIX_POLLERR) != 0) {
                linux_fdset_set(out_except, fd);
                fd_ready = 1u;
            }
            if (fd_ready) {
                ready++;
            }
        }

        if (ready > 0 || (!wait_forever && timeout_ticks == 0u)) {
            if (readfds && bytes != 0u) {
                mem_copy(readfds, out_read, bytes);
            }
            if (writefds && bytes != 0u) {
                mem_copy(writefds, out_write, bytes);
            }
            if (exceptfds && bytes != 0u) {
                mem_copy(exceptfds, out_except, bytes);
            }
            return ready;
        }

        if (!wait_forever) {
            uint64_t now = timer_ticks();
            if (deadline != 0u && now >= deadline) {
                if (readfds && bytes != 0u) {
                    mem_zero(readfds, bytes);
                }
                if (writefds && bytes != 0u) {
                    mem_zero(writefds, bytes);
                }
                if (exceptfds && bytes != 0u) {
                    mem_zero(exceptfds, bytes);
                }
                return 0;
            }
        }

        scheduler_sleep_current(1u);
    }
}

static int linux_mya_fd_used(int32_t mya_fd) {
    myaos_posix_stat_t st;

    if (mya_fd <= 0) {
        return 0;
    }
    mem_zero(&st, sizeof(st));
    return scheduler_posix_fstat(mya_fd, &st) == 0 ? 1 : 0;
}

static int32_t linux_alloc_dup_target_fd(uint8_t pipe_fd_space, int32_t min_linux_fd) {
    int32_t start;
    int32_t end;

    if (pipe_fd_space) {
        start = LINUX_FD_PIPE_BASE + 1;
        end = LINUX_FD_DIR_BASE;
        if (min_linux_fd > start) {
            start = min_linux_fd;
        }
    } else {
        start = 3;
        end = LINUX_FD_PIPE_BASE;
        if (min_linux_fd > start) {
            start = min_linux_fd;
        }
    }

    if (start < 0 || start >= end) {
        return -1;
    }

    for (int32_t candidate = start; candidate < end; candidate++) {
        int32_t mya_fd = pipe_fd_space ? linux_fd_to_mya_pipe(candidate) : linux_fd_to_mya_file(candidate);
        if (mya_fd <= 0) {
            continue;
        }
        if (!linux_mya_fd_used(mya_fd)) {
            return candidate;
        }
    }
    return -1;
}

static int64_t linux_dupfd_impl(int32_t oldfd, int32_t min_linux_fd) {
    uint8_t pipe_fd_space = 0u;
    int32_t old_mya_fd;
    int32_t new_linux_fd;
    int32_t new_mya_fd;

    if (oldfd < 0) {
        return linux_errno(LINUX_EBADF);
    }
    if (min_linux_fd < 0) {
        return linux_errno(LINUX_EINVAL);
    }
    if (oldfd == LINUX_FD_STDIN || oldfd == LINUX_FD_STDOUT || oldfd == LINUX_FD_STDERR) {
        return linux_errno(LINUX_EBADF);
    }
    if (linux_fd_is_dir(oldfd)) {
        return linux_errno(LINUX_EBADF);
    }

    old_mya_fd = linux_fd_to_mya_pipe(oldfd);
    if (old_mya_fd > 0) {
        pipe_fd_space = 1u;
    } else {
        old_mya_fd = linux_fd_to_mya_file(oldfd);
    }
    if (old_mya_fd <= 0) {
        return linux_errno(LINUX_EBADF);
    }

    new_linux_fd = linux_alloc_dup_target_fd(pipe_fd_space, min_linux_fd);
    if (new_linux_fd < 0) {
        return linux_errno(LINUX_EMFILE);
    }
    new_mya_fd = pipe_fd_space ? linux_fd_to_mya_pipe(new_linux_fd) : linux_fd_to_mya_file(new_linux_fd);
    if (new_mya_fd <= 0) {
        return linux_errno(LINUX_EMFILE);
    }
    if (scheduler_posix_dup2(old_mya_fd, new_mya_fd) != 0) {
        return linux_errno(LINUX_EBADF);
    }

    return new_linux_fd;
}

static int64_t linux_stat_abs_path(const char* abs_path, linux_stat_t* out) {
    myaos_posix_stat_t st;
    int32_t fd = -1;

    if (!abs_path || !out || !user_ptr_writable(out, sizeof(*out))) {
        return linux_errno(LINUX_EINVAL);
    }

    mem_zero(&st, sizeof(st));
    if (vfs_is_dir("/", abs_path) > 0) {
        st.mode = LINUX_S_IFDIR | 0555u;
        st.uid = scheduler_current_uid();
        st.size = 0u;
        linux_fill_stat_from_mya(out, &st);
        return 0;
    }
    if (scheduler_posix_open(abs_path, MYAOS_POSIX_O_RDONLY, &fd) != 0 || fd <= 0) {
        return linux_errno(LINUX_ENOENT);
    }
    if (scheduler_posix_fstat(fd, &st) != 0) {
        (void)scheduler_posix_close(fd);
        return linux_errno(LINUX_EIO);
    }
    (void)scheduler_posix_close(fd);
    linux_fill_stat_from_mya(out, &st);
    return 0;
}

static int64_t linux_stat_path(const char* path, linux_stat_t* out) {
    char abs_path[MYAOS_PATH_MAX];

    if (!user_cstr_valid(path, MYAOS_PATH_MAX) || !out || !user_ptr_writable(out, sizeof(*out))) {
        return linux_errno(LINUX_EFAULT);
    }
    if (linux_resolve_at_path(LINUX_AT_FDCWD, path, abs_path, sizeof(abs_path)) != 0) {
        return linux_errno(LINUX_ENOENT);
    }
    return linux_stat_abs_path(abs_path, out);
}

static int64_t linux_statx_abs_path(const char* abs_path, linux_statx_t* out, uint32_t mask) {
    myaos_posix_stat_t st;
    int32_t fd = -1;

    if (!abs_path || !out || !user_ptr_writable(out, sizeof(*out))) {
        return linux_errno(LINUX_EINVAL);
    }

    mem_zero(&st, sizeof(st));
    if (vfs_is_dir("/", abs_path) > 0) {
        st.mode = LINUX_S_IFDIR | 0555u;
        st.uid = scheduler_current_uid();
        st.size = 0u;
        linux_fill_statx_from_mya(out, &st, 1u, mask);
        return 0;
    }
    if (scheduler_posix_open(abs_path, MYAOS_POSIX_O_RDONLY, &fd) != 0 || fd <= 0) {
        return linux_errno(LINUX_ENOENT);
    }
    if (scheduler_posix_fstat(fd, &st) != 0) {
        (void)scheduler_posix_close(fd);
        return linux_errno(LINUX_EIO);
    }
    (void)scheduler_posix_close(fd);
    linux_fill_statx_from_mya(out, &st, 1u, mask);
    return 0;
}

static void linux_rlimit_default(uint32_t resource, linux_rlimit_t* out) {
    if (!out) {
        return;
    }

    out->rlim_cur = LINUX_RLIM_INFINITY;
    out->rlim_max = LINUX_RLIM_INFINITY;

    if (resource == LINUX_RLIMIT_STACK) {
        out->rlim_cur = 8ull * 1024ull * 1024ull;
        out->rlim_max = 8ull * 1024ull * 1024ull;
    } else if (resource == LINUX_RLIMIT_NOFILE) {
        out->rlim_cur = 128ull;
        out->rlim_max = 128ull;
    } else if (resource == LINUX_RLIMIT_AS) {
        out->rlim_cur = 512ull * 1024ull * 1024ull;
        out->rlim_max = 512ull * 1024ull * 1024ull;
    }
}

static uint8_t linux_d_type_from_mya(uint8_t mya_type) {
    if (mya_type == MYAOS_NODE_DIR || mya_type == MYAOS_NODE_MOUNT) {
        return LINUX_DT_DIR;
    }
    if (mya_type == MYAOS_NODE_FILE) {
        return LINUX_DT_REG;
    }
    return LINUX_DT_UNKNOWN;
}

static int linux_scan_argv_user(const char* const* argv, int* out_argc) {
    if (!out_argc) {
        return -1;
    }
    if (!argv) {
        *out_argc = 0;
        return 0;
    }
    for (int i = 0; i < SYSCALL_ARGC_MAX; i++) {
        const char* arg;
        if (!user_ptr_readable(&argv[i], sizeof(argv[i]))) {
            return -1;
        }
        arg = argv[i];
        if (!arg) {
            *out_argc = i;
            return 0;
        }
        if (!user_cstr_valid(arg, SYSCALL_USER_STR_MAX)) {
            return -1;
        }
    }
    return -1;
}

static int32_t linux_wait_status_from_exit(int32_t exit_code) {
    return (exit_code & 0xFF) << 8;
}

static int linux_collect_child_pids(int32_t* out_pids, uint32_t max_pids, uint32_t* out_count) {
    myaos_proc_info_t procs[LINUX_WAIT_SCAN_MAX];
    uint32_t proc_count = 0u;
    int32_t self_pid = scheduler_current_pid();
    uint32_t out_n = 0u;

    if (!out_pids || !out_count || max_pids == 0u || self_pid <= 0) {
        return -1;
    }
    if (scheduler_list_processes(procs, LINUX_WAIT_SCAN_MAX, &proc_count) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < proc_count; i++) {
        if ((int32_t)procs[i].ppid != self_pid || procs[i].pid == 0u) {
            continue;
        }
        out_pids[out_n++] = (int32_t)procs[i].pid;
        if (out_n >= max_pids) {
            break;
        }
    }
    *out_count = out_n;
    return 0;
}

static int64_t linux_wait4_impl(int32_t pid, int32_t* status, uint32_t options) {
    if ((options & ~LINUX_WNOHANG) != 0u) {
        return linux_errno(LINUX_EINVAL);
    }
    if (status && !user_ptr_writable(status, sizeof(*status))) {
        return linux_errno(LINUX_EFAULT);
    }
    if (pid == 0 || pid < -1) {
        return linux_errno(LINUX_EINVAL);
    }

    for (;;) {
        int32_t exit_code = 0;
        if (pid > 0) {
            int rc = scheduler_wait_poll(pid, &exit_code);
            if (rc < 0) {
                return linux_errno(LINUX_ECHILD);
            }
            if (rc > 0) {
                if (status) {
                    *status = linux_wait_status_from_exit(exit_code);
                }
                return pid;
            }
            if ((options & LINUX_WNOHANG) != 0u) {
                return 0;
            }
            scheduler_sleep_current(1u);
            continue;
        } else {
            int32_t child_pids[LINUX_WAIT_SCAN_MAX];
            uint32_t child_count = 0u;
            uint8_t has_running_child = 0u;

            if (linux_collect_child_pids(child_pids, LINUX_WAIT_SCAN_MAX, &child_count) != 0 || child_count == 0u) {
                return linux_errno(LINUX_ECHILD);
            }
            for (uint32_t i = 0u; i < child_count; i++) {
                int rc = scheduler_wait_poll(child_pids[i], &exit_code);
                if (rc > 0) {
                    if (status) {
                        *status = linux_wait_status_from_exit(exit_code);
                    }
                    return child_pids[i];
                }
                if (rc == 0) {
                    has_running_child = 1u;
                }
            }
            if ((options & LINUX_WNOHANG) != 0u) {
                return 0;
            }
            if (!has_running_child) {
                return linux_errno(LINUX_ECHILD);
            }
            scheduler_sleep_current(1u);
        }
    }
}

int64_t syscall_dispatch_linux(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5,
    uint64_t frame_rsp
) {
    switch (number) {
    case 0: { /* read */
        int32_t fd = (int32_t)arg0;
        void* out = (void*)(uintptr_t)arg1;
        uint32_t max_len = (uint32_t)arg2;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
        uint32_t out_read = 0u;

        if (max_len != 0u && !user_ptr_writable(out, max_len)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (fd == LINUX_FD_STDIN) {
            uint8_t* dst = (uint8_t*)out;
            if (max_len == 0u) {
                return 0;
            }
            while (out_read < max_len) {
                char c = keyboard_read_char();
                if (c == 0) {
                    if (out_read > 0u) {
                        break;
                    }
                    scheduler_sleep_current(1u);
                    continue;
                }
                dst[out_read++] = (uint8_t)c;
            }
            return (int64_t)out_read;
        }
        if (fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
            return linux_errno(LINUX_EBADF);
        }
        if (linux_fd_is_dir(fd)) {
            return linux_errno(LINUX_EISDIR);
        }
        if (mya_file_fd <= 0 && mya_pipe_fd <= 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (mya_file_fd > 0 && scheduler_posix_read(mya_file_fd, out, max_len, &out_read) == 0) {
            return (int64_t)out_read;
        }
        if (mya_pipe_fd > 0 && scheduler_pipe_read(mya_pipe_fd, out, max_len, &out_read) == 0) {
            return (int64_t)out_read;
        }
        return linux_errno(LINUX_EBADF);
    }
    case 1: { /* write */
        int32_t fd = (int32_t)arg0;
        const uint8_t* in = (const uint8_t*)(uintptr_t)arg1;
        uint32_t len = (uint32_t)arg2;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
        uint32_t written = 0u;

        if (len != 0u && !user_ptr_readable(in, len)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (fd == LINUX_FD_STDIN || fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
            if (syscall_console_write_bytes(in, len) != 0) {
                return linux_errno(LINUX_EIO);
            }
            return (int64_t)len;
        }
        if (linux_fd_is_dir(fd)) {
            return linux_errno(LINUX_EISDIR);
        }
        if (mya_file_fd <= 0 && mya_pipe_fd <= 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (mya_file_fd > 0 && scheduler_posix_write(mya_file_fd, in, len, &written) == 0) {
            return (int64_t)written;
        }
        if (mya_pipe_fd > 0 && scheduler_pipe_write(mya_pipe_fd, in, len, &written) == 0) {
            return (int64_t)written;
        }
        return linux_errno(LINUX_EBADF);
    }
    case 2: /* open */
    case 257: { /* openat */
        const char* path;
        const char* open_path;
        linux_dir_cache_t* dirh = NULL;
        char dir_path[MYAOS_PATH_MAX];
        char boot_path[MYAOS_PATH_MAX];
        char abs_path[MYAOS_PATH_MAX];
        uint64_t flags;
        uint32_t acc;
        int32_t mya_fd = -1;
        int32_t dir_fd;

        if (number == 257u) {
            path = (const char*)(uintptr_t)arg1;
            flags = arg2;
            if ((int64_t)arg0 == (int64_t)LINUX_AT_FDCWD) {
                open_path = path;
            } else if (linux_fd_is_dir((int32_t)arg0)) {
                dirh = linux_dir_cache_lookup((uint32_t)scheduler_current_pid(), (int32_t)arg0);
                if (!dirh) {
                    return linux_errno(LINUX_EBADF);
                }
                if (path && path[0] == '/') {
                    open_path = path;
                } else {
                    size_t pos = 0u;
                    if (!path) {
                        return linux_errno(LINUX_EFAULT);
                    }
                    for (; dirh->abs_path[pos] && pos + 1u < sizeof(dir_path); pos++) {
                        dir_path[pos] = dirh->abs_path[pos];
                    }
                    if (pos == 0u) {
                        dir_path[pos++] = '/';
                    }
                    if (pos > 0u && dir_path[pos - 1u] != '/' && pos + 1u < sizeof(dir_path)) {
                        dir_path[pos++] = '/';
                    }
                    for (size_t i = 0u; path[i] && pos + 1u < sizeof(dir_path); i++) {
                        dir_path[pos++] = path[i];
                    }
                    dir_path[pos] = '\0';
                    open_path = dir_path;
                }
            } else {
                return linux_errno(LINUX_EBADF);
            }
        } else {
            path = (const char*)(uintptr_t)arg0;
            flags = arg1;
            open_path = path;
        }

        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        acc = (uint32_t)(flags & 0x3u);
        if (vfs_resolve_cwd(scheduler_current_cwd(), open_path, abs_path, sizeof(abs_path)) != 0) {
            if (open_path && open_path[0] == '/') {
                if (linux_path_is_console_tty(open_path)) {
                    return (acc == MYAOS_POSIX_O_WRONLY) ? LINUX_FD_STDOUT : LINUX_FD_STDIN;
                }
                if (linux_build_boot_path(open_path, boot_path, sizeof(boot_path)) == 0 &&
                    vfs_resolve_cwd(scheduler_current_cwd(), boot_path, abs_path, sizeof(abs_path)) == 0) {
                    open_path = boot_path;
                } else {
                    return linux_errno(LINUX_ENOENT);
                }
            } else {
                return linux_errno(LINUX_ENOENT);
            }
        }
        if (linux_path_is_console_tty(abs_path)) {
            return (acc == MYAOS_POSIX_O_WRONLY) ? LINUX_FD_STDOUT : LINUX_FD_STDIN;
        }
        if (vfs_is_dir("/", abs_path) > 0) {
            if (acc == 1u || (flags & (0x40u | 0x200u | 0x400u)) != 0u) {
                return linux_errno(LINUX_EISDIR);
            }
            dir_fd = linux_dir_cache_alloc((uint32_t)scheduler_current_pid(), abs_path);
            if (dir_fd < 0) {
                return linux_errno(LINUX_ENOMEM);
            }
            return (int64_t)dir_fd;
        }
        if (scheduler_posix_open(open_path, linux_open_flags_to_mya(flags), &mya_fd) != 0 || mya_fd <= 0) {
            return linux_errno(LINUX_ENOENT);
        }
        return (int64_t)linux_fd_from_mya_file(mya_fd);
    }
    case 3: { /* close */
        int32_t fd = (int32_t)arg0;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
        if (fd == LINUX_FD_STDIN || fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
            return 0;
        }
        if (linux_fd_is_dir(fd)) {
            linux_dir_cache_close((uint32_t)scheduler_current_pid(), fd);
            return 0;
        }
        if (mya_file_fd > 0 && scheduler_posix_close(mya_file_fd) == 0) {
            return 0;
        }
        if (mya_pipe_fd > 0 && scheduler_pipe_close(mya_pipe_fd) == 0) {
            return 0;
        }
        return linux_errno(LINUX_EBADF);
    }
    case 4: { /* stat */
        const char* path = (const char*)(uintptr_t)arg0;
        linux_stat_t* out = (linux_stat_t*)(uintptr_t)arg1;
        return linux_stat_path(path, out);
    }
    case 6: { /* lstat */
        const char* path = (const char*)(uintptr_t)arg0;
        linux_stat_t* out = (linux_stat_t*)(uintptr_t)arg1;
        return linux_stat_path(path, out);
    }
    case 7: { /* poll */
        linux_pollfd_t* fds = (linux_pollfd_t*)(uintptr_t)arg0;
        uint32_t nfds = (uint32_t)arg1;
        int32_t timeout_ms = (int32_t)arg2;
        uint64_t timeout_ticks;
        uint8_t wait_forever = 0u;
        uint64_t deadline = 0u;
        uint32_t ready = 0u;

        if (nfds > 256u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (nfds != 0u &&
            (!fds || !user_ptr_readable(fds, (uint64_t)nfds * sizeof(*fds)) ||
             !user_ptr_writable(fds, (uint64_t)nfds * sizeof(*fds)))) {
            return linux_errno(LINUX_EFAULT);
        }

        timeout_ticks = linux_millis_to_ticks(timeout_ms, &wait_forever);
        if (!wait_forever && timeout_ticks > 0u) {
            deadline = timer_ticks() + timeout_ticks;
        }

        for (;;) {
            ready = 0u;
            for (uint32_t i = 0u; i < nfds; i++) {
                int32_t fd = fds[i].fd;
                int32_t mya_file_fd = linux_fd_to_mya_file(fd);
                int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
                int16_t revents = 0;
                uint16_t req = (uint16_t)fds[i].events;
                myaos_posix_pollfd_t pollfd;
                uint32_t poll_ready = 0u;

                fds[i].revents = 0;
                if (fd < 0) {
                    continue;
                }

                if (fd == LINUX_FD_STDIN) {
                    if ((req & MYAOS_POSIX_POLLIN) != 0u && keyboard_event_pending() > 0u) {
                        revents |= MYAOS_POSIX_POLLIN;
                    }
                    if ((req & MYAOS_POSIX_POLLOUT) != 0u) {
                        revents |= MYAOS_POSIX_POLLOUT;
                    }
                } else if (fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
                    if ((req & MYAOS_POSIX_POLLOUT) != 0u) {
                        revents |= MYAOS_POSIX_POLLOUT;
                    }
                } else if (linux_fd_is_dir(fd)) {
                    if ((req & MYAOS_POSIX_POLLIN) != 0u) {
                        revents |= MYAOS_POSIX_POLLIN;
                    }
                } else if (mya_pipe_fd > 0) {
                    if ((req & MYAOS_POSIX_POLLIN) != 0u) {
                        revents |= MYAOS_POSIX_POLLIN;
                    }
                    if ((req & MYAOS_POSIX_POLLOUT) != 0u) {
                        revents |= MYAOS_POSIX_POLLOUT;
                    }
                } else if (mya_file_fd <= 0) {
                    revents |= MYAOS_POSIX_POLLERR;
                } else {
                    pollfd.fd = mya_file_fd;
                    pollfd.events = (int16_t)req;
                    pollfd.revents = 0;
                    if (scheduler_posix_poll(&pollfd, 1u, 0u, &poll_ready) == 0) {
                        revents |= pollfd.revents;
                    } else {
                        revents |= MYAOS_POSIX_POLLERR;
                    }
                    (void)poll_ready;
                }

                fds[i].revents = revents;
                if (revents != 0) {
                    ready++;
                }
            }
            if (ready > 0u || timeout_ms == 0) {
                return (int64_t)ready;
            }
            if (!wait_forever) {
                uint64_t now = timer_ticks();
                if (deadline != 0u && now >= deadline) {
                    return 0;
                }
            }
            scheduler_sleep_current(1u);
        }
    }
    case 5: { /* fstat */
        int32_t fd = (int32_t)arg0;
        linux_stat_t* out = (linux_stat_t*)(uintptr_t)arg1;
        return linux_fstat_fd(fd, out);
    }
    case 8: { /* lseek */
        int32_t fd = (int32_t)arg0;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        linux_dir_cache_t* dirh = NULL;
        uint64_t off = 0u;

        if (fd == LINUX_FD_STDIN || fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
            return linux_errno(LINUX_ESPIPE);
        }
        if (fd < 3) {
            return linux_errno(LINUX_EBADF);
        }
        if (linux_fd_is_dir(fd)) {
            int64_t base = 0;
            int64_t next = 0;
            dirh = linux_dir_cache_lookup((uint32_t)scheduler_current_pid(), fd);
            if (!dirh) {
                return linux_errno(LINUX_EBADF);
            }
            if ((uint32_t)arg2 == MYAOS_POSIX_SEEK_SET) {
                base = 0;
            } else if ((uint32_t)arg2 == MYAOS_POSIX_SEEK_CUR) {
                base = (int64_t)dirh->cursor;
            } else {
                return linux_errno(LINUX_EINVAL);
            }
            next = base + (int64_t)arg1;
            if (next < 0) {
                return linux_errno(LINUX_EINVAL);
            }
            dirh->cursor = (uint32_t)next;
            return next;
        }
        if (mya_file_fd <= 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (scheduler_posix_lseek(mya_file_fd, (int64_t)arg1, (uint32_t)arg2, &off) == 0) {
            return (int64_t)off;
        }
        if (linux_fd_to_mya_pipe(fd) > 0) {
            return linux_errno(LINUX_ESPIPE);
        }
        return linux_errno(LINUX_EBADF);
    }
    case 9: { /* mmap */
        uint32_t writable = ((arg2 & 0x2u) != 0u) ? 1u : 0u;
        uint32_t map_writable = writable;
        uint32_t flags = (uint32_t)arg3;
        int32_t fd = (int32_t)arg4;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        uint64_t addr = 0u;
        uint64_t file_off = arg5;
        uint64_t saved_off = 0u;
        uint64_t total = 0u;
        uint32_t chunk_read = 0u;
        (void)arg0;
        if (arg1 == 0u) {
            return linux_errno(LINUX_EINVAL);
        }
        if ((flags & LINUX_MAP_ANONYMOUS) == 0u && fd != -1) {
            /* Loader fills file-backed mappings from kernel side; keep pages
               writable internally (mprotect is currently a no-op anyway). */
            map_writable = 1u;
        }
        if (scheduler_mem_map_current(arg1, map_writable, &addr) != 0) {
            return linux_errno(LINUX_ENOMEM);
        }
        if ((flags & LINUX_MAP_ANONYMOUS) == 0u && fd != -1) {
            if (mya_file_fd <= 0) {
                (void)scheduler_mem_unmap_current(addr);
                return linux_errno(LINUX_EBADF);
            }
            if (scheduler_posix_lseek(mya_file_fd, 0, MYAOS_POSIX_SEEK_CUR, &saved_off) != 0 ||
                scheduler_posix_lseek(mya_file_fd, (int64_t)file_off, MYAOS_POSIX_SEEK_SET, NULL) != 0) {
                (void)scheduler_mem_unmap_current(addr);
                return linux_errno(LINUX_EINVAL);
            }
            while (total < arg1) {
                uint64_t left = arg1 - total;
                uint32_t ask = left > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)left;
                chunk_read = 0u;
                if (scheduler_posix_read(mya_file_fd, (void*)(uintptr_t)(addr + total), ask, &chunk_read) != 0) {
                    (void)scheduler_posix_lseek(mya_file_fd, (int64_t)saved_off, MYAOS_POSIX_SEEK_SET, NULL);
                    (void)scheduler_mem_unmap_current(addr);
                    return linux_errno(LINUX_EIO);
                }
                total += chunk_read;
                if (chunk_read < ask) {
                    break;
                }
            }
            if (total < arg1) {
                mem_zero((void*)(uintptr_t)(addr + total), (size_t)(arg1 - total));
            }
            (void)scheduler_posix_lseek(mya_file_fd, (int64_t)saved_off, MYAOS_POSIX_SEEK_SET, NULL);
        }
        return (int64_t)addr;
    }
    case 10: /* mprotect */
        return 0;
    case 11: /* munmap */
        if (scheduler_mem_unmap_current(arg0) != 0) {
            return linux_errno(LINUX_EINVAL);
        }
        return 0;
    case 12: { /* brk */
        uint64_t brk = 0u;
        if (scheduler_linux_brk(arg0, &brk) != 0) {
            return linux_errno(LINUX_ENOMEM);
        }
        return (int64_t)brk;
    }
    case 13: /* rt_sigaction */
    case 14: /* rt_sigprocmask */
    case 273: /* set_robust_list */
        return 0;
    case 16: { /* ioctl */
        int32_t fd = (int32_t)arg0;
        uint32_t req = (uint32_t)arg1;
        void* argp = (void*)(uintptr_t)arg2;

        if (fd != LINUX_FD_STDIN && fd != LINUX_FD_STDOUT && fd != LINUX_FD_STDERR) {
            return linux_errno(LINUX_ENOTTY);
        }

        if (req == LINUX_TCGETS) {
            linux_termios_t* tio = (linux_termios_t*)argp;
            if (!tio || !user_ptr_writable(tio, sizeof(*tio))) {
                return linux_errno(LINUX_EFAULT);
            }
            mem_zero(tio, sizeof(*tio));
            /* Minimal canonical+echo tty profile for userspace probes (isatty/tcgetattr). */
            tio->c_lflag = 0x0000000Bu; /* ISIG | ICANON | ECHO */
            return 0;
        }
        if (req == LINUX_TCSETS || req == LINUX_TCSETSW || req == LINUX_TCSETSF) {
            const linux_termios_t* tio = (const linux_termios_t*)argp;
            if (!tio || !user_ptr_readable(tio, sizeof(*tio))) {
                return linux_errno(LINUX_EFAULT);
            }
            return 0;
        }
        if (req == LINUX_TIOCGPGRP) {
            int32_t* out = (int32_t*)argp;
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            *out = scheduler_current_pid();
            return 0;
        }
        if (req == LINUX_TIOCSPGRP) {
            const int32_t* in = (const int32_t*)argp;
            if (!in || !user_ptr_readable(in, sizeof(*in))) {
                return linux_errno(LINUX_EFAULT);
            }
            if (*in <= 0) {
                return linux_errno(LINUX_EINVAL);
            }
            return 0;
        }
        if (req == LINUX_TIOCGWINSZ) {
            linux_winsize_t* ws = (linux_winsize_t*)argp;
            uint32_t cols = console_cols();
            uint32_t rows = 25u;
            boot_info_t* boot = console_boot_info();
            if (boot && boot->fb.height >= FONT_HEIGHT) {
                uint32_t r = boot->fb.height / FONT_HEIGHT;
                if (r > 0u) {
                    rows = r;
                }
            }
            if (!ws || !user_ptr_writable(ws, sizeof(*ws))) {
                return linux_errno(LINUX_EFAULT);
            }
            mem_zero(ws, sizeof(*ws));
            ws->ws_col = (uint16_t)((cols == 0u || cols > 65535u) ? 80u : cols);
            ws->ws_row = (uint16_t)((rows == 0u || rows > 65535u) ? 25u : rows);
            return 0;
        }
        if (req == LINUX_TIOCSWINSZ) {
            const linux_winsize_t* ws = (const linux_winsize_t*)argp;
            if (!ws || !user_ptr_readable(ws, sizeof(*ws))) {
                return linux_errno(LINUX_EFAULT);
            }
            return 0;
        }
        if (req == LINUX_FIONREAD) {
            int32_t* out = (int32_t*)argp;
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            *out = keyboard_event_pending() > 0 ? 1 : 0;
            return 0;
        }
        if (req == LINUX_TIOCGSID) {
            int32_t* out = (int32_t*)argp;
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            *out = scheduler_current_pid();
            return 0;
        }
        if (req == LINUX_KDGKBTYPE) {
            uint8_t* out = (uint8_t*)argp;
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            *out = 0x02u; /* KB_101 */
            return 0;
        }
        if (req == LINUX_KDGETMODE) {
            int32_t* out = (int32_t*)argp;
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            *out = (console_get_mode() == CONSOLE_MODE_GRAPHICS) ? (int32_t)LINUX_KD_GRAPHICS : (int32_t)LINUX_KD_TEXT;
            return 0;
        }
        if (req == LINUX_KDSETMODE) {
            uint32_t mode = (uint32_t)arg2;
            if (mode == LINUX_KD_TEXT) {
                console_set_mode(CONSOLE_MODE_TEXT);
                return 0;
            }
            if (mode == LINUX_KD_GRAPHICS) {
                console_set_mode(CONSOLE_MODE_GRAPHICS);
                return 0;
            }
            return linux_errno(LINUX_EINVAL);
        }
        if (req == LINUX_VT_OPENQRY) {
            int32_t* out = (int32_t*)argp;
            uint32_t count = console_tty_count();
            uint32_t active = console_tty_active();
            uint32_t candidate = 1u;

            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            if (count > 1u) {
                candidate = ((active + 1u) % count) + 1u;
            }
            *out = (int32_t)candidate;
            return 0;
        }
        if (req == LINUX_VT_GETMODE) {
            linux_vt_mode_t* out = (linux_vt_mode_t*)argp;
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            *out = g_linux_vt_mode;
            return 0;
        }
        if (req == LINUX_VT_SETMODE) {
            const linux_vt_mode_t* in = (const linux_vt_mode_t*)argp;
            if (!in || !user_ptr_readable(in, sizeof(*in))) {
                return linux_errno(LINUX_EFAULT);
            }
            g_linux_vt_mode = *in;
            return 0;
        }
        if (req == LINUX_VT_GETSTATE) {
            linux_vt_stat_t* out = (linux_vt_stat_t*)argp;
            uint16_t state = 0u;
            uint32_t count = console_tty_count();
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            for (uint32_t i = 0u; i < count && i < 16u; i++) {
                state |= (uint16_t)(1u << i);
            }
            out->v_active = (uint16_t)(console_tty_active() + 1u);
            out->v_signal = 0u;
            out->v_state = state;
            return 0;
        }
        if (req == LINUX_VT_RELDISP) {
            return 0;
        }
        if (req == LINUX_VT_ACTIVATE || req == LINUX_VT_WAITACTIVE) {
            uint32_t vt_num = (uint32_t)arg2;
            if (vt_num == 0u || console_tty_switch(vt_num - 1u) != 0) {
                return linux_errno(LINUX_EINVAL);
            }
            return 0;
        }
        return linux_errno(LINUX_ENOTTY);
    }
    case 17: { /* pread64 */
        int32_t fd = (int32_t)arg0;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        void* out = (void*)(uintptr_t)arg1;
        uint32_t len = (uint32_t)arg2;
        uint64_t off = arg3;
        uint64_t saved_off = 0u;
        uint32_t out_read = 0u;

        if (len != 0u && !user_ptr_writable(out, len)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (mya_file_fd <= 0 || linux_fd_is_dir(fd) || linux_fd_to_mya_pipe(fd) > 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (scheduler_posix_lseek(mya_file_fd, 0, MYAOS_POSIX_SEEK_CUR, &saved_off) != 0 ||
            scheduler_posix_lseek(mya_file_fd, (int64_t)off, MYAOS_POSIX_SEEK_SET, NULL) != 0) {
            return linux_errno(LINUX_EINVAL);
        }
        if (scheduler_posix_read(mya_file_fd, out, len, &out_read) != 0) {
            (void)scheduler_posix_lseek(mya_file_fd, (int64_t)saved_off, MYAOS_POSIX_SEEK_SET, NULL);
            return linux_errno(LINUX_EIO);
        }
        (void)scheduler_posix_lseek(mya_file_fd, (int64_t)saved_off, MYAOS_POSIX_SEEK_SET, NULL);
        return (int64_t)out_read;
    }
    case 18: { /* pwrite64 */
        int32_t fd = (int32_t)arg0;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        const void* in = (const void*)(uintptr_t)arg1;
        uint32_t len = (uint32_t)arg2;
        uint64_t off = arg3;
        uint64_t saved_off = 0u;
        uint32_t out_written = 0u;

        if (len != 0u && !user_ptr_readable(in, len)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (mya_file_fd <= 0 || linux_fd_is_dir(fd) || linux_fd_to_mya_pipe(fd) > 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (scheduler_posix_lseek(mya_file_fd, 0, MYAOS_POSIX_SEEK_CUR, &saved_off) != 0 ||
            scheduler_posix_lseek(mya_file_fd, (int64_t)off, MYAOS_POSIX_SEEK_SET, NULL) != 0) {
            return linux_errno(LINUX_EINVAL);
        }
        if (scheduler_posix_write(mya_file_fd, in, len, &out_written) != 0) {
            (void)scheduler_posix_lseek(mya_file_fd, (int64_t)saved_off, MYAOS_POSIX_SEEK_SET, NULL);
            return linux_errno(LINUX_EIO);
        }
        (void)scheduler_posix_lseek(mya_file_fd, (int64_t)saved_off, MYAOS_POSIX_SEEK_SET, NULL);
        return (int64_t)out_written;
    }
    case 19: { /* readv */
        int32_t fd = (int32_t)arg0;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
        linux_iovec_t* iov = (linux_iovec_t*)(uintptr_t)arg1;
        uint32_t iovcnt = (uint32_t)arg2;
        uint64_t total = 0u;

        if (iovcnt > 256u || (iovcnt != 0u && !user_ptr_readable(iov, (uint64_t)iovcnt * sizeof(*iov)))) {
            return linux_errno(LINUX_EFAULT);
        }
        for (uint32_t i = 0; i < iovcnt; i++) {
            uint8_t* out = (uint8_t*)(uintptr_t)iov[i].iov_base;
            uint32_t len = (uint32_t)iov[i].iov_len;
            uint32_t part = 0u;
            if (len != 0u && !user_ptr_writable(out, len)) {
                return linux_errno(LINUX_EFAULT);
            }
            if (fd == LINUX_FD_STDIN) {
                if (len == 0u) {
                    continue;
                }
                while (part < len) {
                    char c = keyboard_read_char();
                    if (c == 0) {
                        if (part > 0u) {
                            break;
                        }
                        scheduler_sleep_current(1u);
                        continue;
                    }
                    out[part++] = (uint8_t)c;
                }
            } else {
                if (fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
                    return linux_errno(LINUX_EBADF);
                }
                if (linux_fd_is_dir(fd)) {
                    return linux_errno(LINUX_EISDIR);
                }
                if (mya_file_fd <= 0 && mya_pipe_fd <= 0) {
                    return linux_errno(LINUX_EBADF);
                }
                if ((mya_file_fd <= 0 || scheduler_posix_read(mya_file_fd, out, len, &part) != 0) &&
                    (mya_pipe_fd <= 0 || scheduler_pipe_read(mya_pipe_fd, out, len, &part) != 0)) {
                    return linux_errno(LINUX_EBADF);
                }
            }
            total += part;
            if (part < len) {
                break;
            }
        }
        return (int64_t)total;
    }
    case 20: { /* writev */
        int32_t fd = (int32_t)arg0;
        int32_t mya_file_fd = linux_fd_to_mya_file(fd);
        int32_t mya_pipe_fd = linux_fd_to_mya_pipe(fd);
        linux_iovec_t* iov = (linux_iovec_t*)(uintptr_t)arg1;
        uint32_t iovcnt = (uint32_t)arg2;
        uint64_t total = 0u;

        if (iovcnt > 256u || (iovcnt != 0u && !user_ptr_readable(iov, (uint64_t)iovcnt * sizeof(*iov)))) {
            return linux_errno(LINUX_EFAULT);
        }
        for (uint32_t i = 0; i < iovcnt; i++) {
            const uint8_t* in = (const uint8_t*)(uintptr_t)iov[i].iov_base;
            uint32_t len = (uint32_t)iov[i].iov_len;
            uint32_t part = 0u;
            if (len != 0u && !user_ptr_readable(in, len)) {
                return linux_errno(LINUX_EFAULT);
            }
            if (fd == LINUX_FD_STDIN || fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
                if (syscall_console_write_bytes(in, len) != 0) {
                    return linux_errno(LINUX_EIO);
                }
                part = len;
            } else {
                if (linux_fd_is_dir(fd)) {
                    return linux_errno(LINUX_EISDIR);
                }
                if (mya_file_fd <= 0 && mya_pipe_fd <= 0) {
                    return linux_errno(LINUX_EBADF);
                }
                if ((mya_file_fd <= 0 || scheduler_posix_write(mya_file_fd, in, len, &part) != 0) &&
                    (mya_pipe_fd <= 0 || scheduler_pipe_write(mya_pipe_fd, in, len, &part) != 0)) {
                    return linux_errno(LINUX_EBADF);
                }
            }
            total += part;
            if (part < len) {
                break;
            }
        }
        return (int64_t)total;
    }
    case 21: { /* access */
        const char* path = (const char*)(uintptr_t)arg0;
        (void)arg1;
        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        return linux_path_exists(scheduler_current_cwd(), path) ? 0 : linux_errno(LINUX_ENOENT);
    }
    case 22: { /* pipe */
        int32_t* fds = (int32_t*)(uintptr_t)arg0;
        int32_t read_fd = -1;
        int32_t write_fd = -1;

        if (!fds || !user_ptr_writable(fds, sizeof(int32_t) * 2u)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (scheduler_pipe_create(&read_fd, &write_fd) != 0 || read_fd <= 0 || write_fd <= 0) {
            return linux_errno(LINUX_ENOMEM);
        }
        fds[0] = linux_fd_from_mya_pipe(read_fd);
        fds[1] = linux_fd_from_mya_pipe(write_fd);
        return 0;
    }
    case 23: { /* select */
        int32_t nfds = (int32_t)arg0;
        uint8_t* readfds = (uint8_t*)(uintptr_t)arg1;
        uint8_t* writefds = (uint8_t*)(uintptr_t)arg2;
        uint8_t* exceptfds = (uint8_t*)(uintptr_t)arg3;
        linux_timeval_t* timeout = (linux_timeval_t*)(uintptr_t)arg4;
        uint64_t timeout_ticks = 0u;
        uint8_t wait_forever = 1u;
        int64_t rc;

        if (timeout) {
            if (!user_ptr_readable(timeout, sizeof(*timeout)) ||
                !user_ptr_writable(timeout, sizeof(*timeout)) ||
                linux_timeval_to_ticks(timeout, &timeout_ticks) != 0) {
                return linux_errno(LINUX_EINVAL);
            }
            wait_forever = 0u;
        }

        rc = linux_select_common(nfds, readfds, writefds, exceptfds, wait_forever, timeout_ticks);
        if (timeout && user_ptr_writable(timeout, sizeof(*timeout))) {
            timeout->tv_sec = 0;
            timeout->tv_usec = 0;
        }
        return rc;
    }
    case 24: /* sched_yield */
        scheduler_yield_current();
        return 0;
    case 28: /* madvise */
        (void)arg0;
        (void)arg1;
        (void)arg2;
        return 0;
    case 32: { /* dup */
        return linux_dupfd_impl((int32_t)arg0, 0);
    }
    case 33: { /* dup2 */
        int32_t oldfd = (int32_t)arg0;
        int32_t newfd = (int32_t)arg1;
        uint8_t old_is_pipe = 0u;
        int32_t old_mya;
        int32_t new_mya;

        if (oldfd < 0 || newfd < 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (oldfd == newfd) {
            return newfd;
        }
        if (oldfd < 3 || newfd < 3 || linux_fd_is_dir(oldfd) || linux_fd_is_dir(newfd)) {
            return linux_errno(LINUX_EBADF);
        }

        old_mya = linux_fd_to_mya_pipe(oldfd);
        if (old_mya > 0) {
            old_is_pipe = 1u;
        } else {
            old_mya = linux_fd_to_mya_file(oldfd);
        }
        if (old_mya <= 0) {
            return linux_errno(LINUX_EBADF);
        }

        if (old_is_pipe) {
            if (newfd < LINUX_FD_PIPE_BASE || newfd >= LINUX_FD_DIR_BASE) {
                return linux_errno(LINUX_EBADF);
            }
            new_mya = linux_fd_to_mya_pipe(newfd);
        } else {
            if (newfd >= LINUX_FD_PIPE_BASE) {
                return linux_errno(LINUX_EBADF);
            }
            new_mya = linux_fd_to_mya_file(newfd);
        }
        if (new_mya <= 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (scheduler_posix_dup2(old_mya, new_mya) != 0) {
            return linux_errno(LINUX_EBADF);
        }
        return newfd;
    }
    case 35: { /* nanosleep */
        const linux_timespec_t* req = (const linux_timespec_t*)(uintptr_t)arg0;
        linux_timespec_t* rem = (linux_timespec_t*)(uintptr_t)arg1;
        uint64_t ticks = 0u;

        if (!req || !user_ptr_readable(req, sizeof(*req)) || linux_timespec_to_ticks(req, &ticks) != 0) {
            return linux_errno(LINUX_EINVAL);
        }
        if (rem && user_ptr_writable(rem, sizeof(*rem))) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        scheduler_sleep_current(ticks);
        return 0;
    }
    case 39: /* getpid */
        return scheduler_current_pid();
    case 56: { /* clone */
        uint64_t flags = arg0;
        uint64_t child_stack = arg1;
        int32_t* parent_tid = (int32_t*)(uintptr_t)arg2;
        int32_t* child_tid = (int32_t*)(uintptr_t)arg3;
        uint64_t tls = arg4;
        int32_t tid = 0;

        if (scheduler_linux_clone_current(frame_rsp, flags, child_stack, parent_tid, child_tid, tls, &tid) != 0) {
            return linux_errno(LINUX_EINVAL);
        }
        return tid;
    }
    case 57: { /* fork */
        int32_t child_pid = 0;
        if (scheduler_linux_clone_current(frame_rsp, LINUX_SIGCHLD, 0u, NULL, NULL, 0u, &child_pid) != 0) {
            return linux_errno(LINUX_EAGAIN);
        }
        return child_pid;
    }
    case 58: { /* vfork */
        uint64_t flags = (uint64_t)(LINUX_CLONE_VM | LINUX_CLONE_VFORK | LINUX_SIGCHLD);
        int32_t child_pid = 0;
        if (scheduler_linux_clone_current(frame_rsp, flags, 0u, NULL, NULL, 0u, &child_pid) != 0) {
            return linux_errno(LINUX_EAGAIN);
        }
        return child_pid;
    }
    case 59: { /* execve */
        const char* path = (const char*)(uintptr_t)arg0;
        const char* const* argv = (const char* const*)(uintptr_t)arg1;
        int argc = 0;
        (void)arg2; /* envp */

        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (linux_scan_argv_user(argv, &argc) != 0) {
            return linux_errno(LINUX_EFAULT);
        }
        if (scheduler_exec_current(path, argc, argv) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        return 0;
    }
    case 60: /* exit */
    case 231: /* exit_group */
        linux_dir_cache_close_all((uint32_t)scheduler_current_pid());
        scheduler_exit_current((int32_t)arg0);
        return 0;
    case 61: { /* wait4 */
        int32_t pid = (int32_t)arg0;
        int32_t* status = (int32_t*)(uintptr_t)arg1;
        uint32_t options = (uint32_t)arg2;
        (void)arg3; /* rusage */
        return linux_wait4_impl(pid, status, options);
    }
    case 62: { /* kill */
        int32_t pid = (int32_t)arg0;
        int32_t sig = (int32_t)arg1;

        if (pid <= 0) {
            return linux_errno(LINUX_EINVAL);
        }
        if (sig < 0 || sig > 64) {
            return linux_errno(LINUX_EINVAL);
        }
        if (sig == 0) {
            return (pid == scheduler_current_pid()) ? 0 : linux_errno(LINUX_ESRCH);
        }
        if (scheduler_kill_pid(pid, 128 + sig) != 0) {
            return linux_errno(LINUX_ESRCH);
        }
        return 0;
    }
    case 63: { /* uname */
        linux_utsname_t* out = (linux_utsname_t*)(uintptr_t)arg0;
        if (!out || !user_ptr_writable(out, sizeof(*out))) {
            return linux_errno(LINUX_EFAULT);
        }
        mem_zero(out, sizeof(*out));
        linux_copy_uts_field(out->sysname, "Linux");
        linux_copy_uts_field(out->nodename, "myaos");
        linux_copy_uts_field(out->release, "5.10.0-myaos");
        linux_copy_uts_field(out->version, "#1 MyaOS");
        linux_copy_uts_field(out->machine, "x86_64");
        linux_copy_uts_field(out->domainname, "localdomain");
        return 0;
    }
    case 72: { /* fcntl */
        int32_t fd = (int32_t)arg0;
        uint32_t cmd = (uint32_t)arg1;
        uint64_t val = arg2;
        int32_t mya_file_fd;
        myaos_posix_stat_t st;

        if (fd < 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (fd == LINUX_FD_STDIN || fd == LINUX_FD_STDOUT || fd == LINUX_FD_STDERR) {
            switch (cmd) {
            case LINUX_F_GETFD:
            case LINUX_F_SETFD:
                (void)val;
                return 0;
            case LINUX_F_GETFL:
                return (fd == LINUX_FD_STDIN) ? 2 : 1;
            case LINUX_F_SETFL:
                (void)val;
                return 0;
            case LINUX_F_DUPFD:
            case LINUX_F_DUPFD_CLOEXEC:
                return linux_dupfd_impl(fd, (int32_t)val);
            default:
                return linux_errno(LINUX_EINVAL);
            }
        }

        if (linux_fd_is_dir(fd)) {
            switch (cmd) {
            case LINUX_F_GETFD:
            case LINUX_F_SETFD:
                (void)val;
                return 0;
            case LINUX_F_GETFL:
                return 0;
            case LINUX_F_SETFL:
                (void)val;
                return 0;
            default:
                return linux_errno(LINUX_EINVAL);
            }
        }
        if (linux_fd_to_mya_pipe(fd) > 0) {
            switch (cmd) {
            case LINUX_F_GETFD:
            case LINUX_F_SETFD:
                (void)val;
                return 0;
            case LINUX_F_GETFL:
                return 2;
            case LINUX_F_SETFL:
                (void)val;
                return 0;
            case LINUX_F_DUPFD:
            case LINUX_F_DUPFD_CLOEXEC:
                return linux_dupfd_impl(fd, (int32_t)val);
            default:
                return linux_errno(LINUX_EINVAL);
            }
        }

        mya_file_fd = linux_fd_to_mya_file(fd);
        if (mya_file_fd <= 0) {
            return linux_errno(LINUX_EBADF);
        }

        switch (cmd) {
        case LINUX_F_GETFD:
        case LINUX_F_SETFD:
            (void)val;
            return 0;
        case LINUX_F_GETFL:
            mem_zero(&st, sizeof(st));
            if (scheduler_posix_fstat(mya_file_fd, &st) != 0) {
                return linux_errno(LINUX_EBADF);
            }
            if ((st.mode & 0222u) != 0u && (st.mode & 0444u) != 0u) {
                return 2;
            }
            if ((st.mode & 0222u) != 0u) {
                return 1;
            }
            return 0;
        case LINUX_F_SETFL:
            (void)val;
            return 0;
        case LINUX_F_DUPFD:
        case LINUX_F_DUPFD_CLOEXEC:
            return linux_dupfd_impl(fd, (int32_t)val);
        default:
            return linux_errno(LINUX_EINVAL);
        }
    }
    case 79: { /* getcwd */
        char* out = (char*)(uintptr_t)arg0;
        size_t out_size = (size_t)arg1;
        if (!out || out_size == 0u || !user_ptr_writable(out, out_size)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (scheduler_getcwd_current(out, out_size) != 0) {
            return linux_errno(LINUX_EOVERFLOW);
        }
        return (int64_t)(uintptr_t)out;
    }
    case 80: { /* chdir */
        const char* path = (const char*)(uintptr_t)arg0;
        char abs_path[MYAOS_PATH_MAX];

        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (vfs_resolve_cwd(scheduler_current_cwd(), path, abs_path, sizeof(abs_path)) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        if (vfs_is_dir("/", abs_path) <= 0) {
            return linux_errno(LINUX_ENOTDIR);
        }
        if (scheduler_setcwd_current(abs_path) != 0) {
            return linux_errno(LINUX_EIO);
        }
        return 0;
    }
    case 87: { /* unlink */
        const char* path = (const char*)(uintptr_t)arg0;
        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (vfs_remove(scheduler_current_cwd(), path) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        return 0;
    }
    case 89: { /* readlink */
        const char* path = (const char*)(uintptr_t)arg0;
        char* out = (char*)(uintptr_t)arg1;
        uint64_t out_size = arg2;
        const char* target = NULL;
        size_t len;

        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (!out || out_size == 0u || !user_ptr_writable(out, out_size)) {
            return linux_errno(LINUX_EFAULT);
        }

        if (linux_cstr_len(path) == 14u &&
            path[0] == '/' && path[1] == 'p' && path[2] == 'r' && path[3] == 'o' && path[4] == 'c' &&
            path[5] == '/' && path[6] == 's' && path[7] == 'e' && path[8] == 'l' && path[9] == 'f' &&
            path[10] == '/' && path[11] == 'e' && path[12] == 'x' && path[13] == 'e') {
            target = "/proc/self/exe";
        } else {
            return linux_errno(LINUX_EINVAL);
        }

        len = linux_cstr_len(target);
        if ((uint64_t)len > out_size) {
            len = (size_t)out_size;
        }
        if (len > 0u) {
            mem_copy(out, target, len);
        }
        return (int64_t)len;
    }
    case 96: { /* gettimeofday */
        linux_timeval_t* tv = (linux_timeval_t*)(uintptr_t)arg0;
        (void)arg1;
        if (!tv || !user_ptr_writable(tv, sizeof(*tv))) {
            return linux_errno(LINUX_EFAULT);
        }
        tv->tv_sec = linux_time_now_seconds();
        tv->tv_usec = 0;
        return 0;
    }
    case 97: { /* getrlimit */
        uint32_t resource = (uint32_t)arg0;
        linux_rlimit_t* out = (linux_rlimit_t*)(uintptr_t)arg1;

        if (!out || !user_ptr_writable(out, sizeof(*out))) {
            return linux_errno(LINUX_EFAULT);
        }
        linux_rlimit_default(resource, out);
        return 0;
    }
    case 102: /* getuid */
    case 107: /* geteuid */
        return (int64_t)scheduler_current_uid();
    case 104: /* getgid */
    case 108: /* getegid */
        return 0;
    case 109: { /* setpgid */
        int32_t pid = (int32_t)arg0;
        int32_t pgid = (int32_t)arg1;
        int32_t self = scheduler_current_pid();

        if (pid != 0 && pid != self) {
            return linux_errno(LINUX_ESRCH);
        }
        if (pgid == 0) {
            pgid = self;
        }
        if (pgid <= 0) {
            return linux_errno(LINUX_EINVAL);
        }
        return 0;
    }
    case 110: /* getppid */
        return 1;
    case 111: /* getpgrp */
        return scheduler_current_pid();
    case 112: /* setsid */
        return scheduler_current_pid();
    case 121: { /* getpgid */
        int32_t pid = (int32_t)arg0;
        int32_t self = scheduler_current_pid();
        if (pid != 0 && pid != self) {
            return linux_errno(LINUX_ESRCH);
        }
        return self;
    }
    case 124: { /* getsid */
        int32_t pid = (int32_t)arg0;
        int32_t self = scheduler_current_pid();
        if (pid != 0 && pid != self) {
            return linux_errno(LINUX_ESRCH);
        }
        return self;
    }
    case 157: { /* prctl */
        uint32_t op = (uint32_t)arg0;

        if (op == LINUX_PR_SET_NAME) {
            const char* in = (const char*)(uintptr_t)arg1;
            char name[16];
            size_t len = 0u;

            if (!in || !user_ptr_readable(in, sizeof(name))) {
                return linux_errno(LINUX_EFAULT);
            }
            while (len + 1u < sizeof(name) && in[len] != '\0') {
                name[len] = in[len];
                len++;
            }
            name[len] = '\0';
            if (len == 0u) {
                return linux_errno(LINUX_EINVAL);
            }
            return (scheduler_proc_name_set_current(name) == 0) ? 0 : linux_errno(LINUX_EINVAL);
        }
        if (op == LINUX_PR_GET_NAME) {
            char* out = (char*)(uintptr_t)arg1;
            char name[MYAOS_PROC_NAME_MAX];
            size_t len = 0u;

            if (!out || !user_ptr_writable(out, 16u)) {
                return linux_errno(LINUX_EFAULT);
            }
            if (scheduler_proc_name_get_current(name, sizeof(name)) != 0) {
                return linux_errno(LINUX_EINVAL);
            }
            mem_zero(out, 16u);
            while (len < 15u && name[len] != '\0') {
                out[len] = name[len];
                len++;
            }
            return 0;
        }

        return linux_errno(LINUX_EINVAL);
    }
    case 158: { /* arch_prctl */
        if ((uint32_t)arg0 == LINUX_ARCH_SET_FS) {
            return scheduler_linux_fs_base_set(arg1) == 0 ? 0 : linux_errno(LINUX_EINVAL);
        }
        if ((uint32_t)arg0 == LINUX_ARCH_GET_FS) {
            uint64_t* out = (uint64_t*)(uintptr_t)arg1;
            uint64_t fs = 0u;
            if (!out || !user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            if (scheduler_linux_fs_base_get(&fs) != 0) {
                return linux_errno(LINUX_EINVAL);
            }
            *out = fs;
            return 0;
        }
        return linux_errno(LINUX_EINVAL);
    }
    case 160: { /* setrlimit */
        const linux_rlimit_t* in = (const linux_rlimit_t*)(uintptr_t)arg1;
        (void)arg0;
        if (in && !user_ptr_readable(in, sizeof(*in))) {
            return linux_errno(LINUX_EFAULT);
        }
        return 0;
    }
    case 186: /* gettid */
        return scheduler_current_pid();
    case 201: { /* time */
        int64_t sec = linux_time_now_seconds();
        int64_t* out = (int64_t*)(uintptr_t)arg0;
        if (out) {
            if (!user_ptr_writable(out, sizeof(*out))) {
                return linux_errno(LINUX_EFAULT);
            }
            *out = sec;
        }
        return sec;
    }
    case 202: { /* futex */
        uint32_t* uaddr = (uint32_t*)(uintptr_t)arg0;
        uint32_t op = (uint32_t)arg1 & LINUX_FUTEX_CMD_MASK;
        uint32_t val = (uint32_t)arg2;
        const linux_timespec_t* timeout = (const linux_timespec_t*)(uintptr_t)arg3;
        uint64_t ticks = 0u;

        if (!uaddr || !user_ptr_readable(uaddr, sizeof(*uaddr))) {
            return linux_errno(LINUX_EFAULT);
        }

        if (op == LINUX_FUTEX_WAIT || op == LINUX_FUTEX_WAIT_BITSET) {
            if (*uaddr != val) {
                return linux_errno(LINUX_EAGAIN);
            }
            if (timeout) {
                if (!user_ptr_readable(timeout, sizeof(*timeout)) || linux_timespec_to_ticks(timeout, &ticks) != 0) {
                    return linux_errno(LINUX_EINVAL);
                }
                if (ticks > 0u) {
                    scheduler_sleep_current(ticks);
                }
                return linux_errno(LINUX_ETIMEDOUT);
            }
            scheduler_sleep_current(1u);
            return 0;
        }

        if (op == LINUX_FUTEX_WAKE || op == LINUX_FUTEX_WAKE_BITSET) {
            return (int64_t)val;
        }

        return linux_errno(LINUX_EINVAL);
    }
    case 217: { /* getdents64 */
        int32_t fd = (int32_t)arg0;
        uint8_t* out = (uint8_t*)(uintptr_t)arg1;
        uint32_t out_size = (uint32_t)arg2;
        linux_dir_cache_t* dirh = linux_dir_cache_lookup((uint32_t)scheduler_current_pid(), fd);
        myaos_dirent_t entries[LINUX_DIR_LIST_MAX];
        size_t count = 0u;
        uint32_t cursor;
        uint32_t written = 0u;

        if (!dirh) {
            return linux_errno(LINUX_EBADF);
        }
        if (!out || (out_size != 0u && !user_ptr_writable(out, out_size))) {
            return linux_errno(LINUX_EFAULT);
        }
        if (out_size < 24u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (vfs_list("/", dirh->abs_path, entries, LINUX_DIR_LIST_MAX, &count) != 0) {
            return linux_errno(LINUX_ENOENT);
        }

        cursor = dirh->cursor;
        for (;;) {
            const char* name = NULL;
            uint8_t d_type = LINUX_DT_UNKNOWN;
            uint64_t ino = 0u;
            size_t name_len;
            uint64_t rec_len;
            linux_dirent64_t* rec;

            if (cursor == 0u) {
                name = ".";
                d_type = LINUX_DT_DIR;
                ino = 1u;
            } else if (cursor == 1u) {
                name = "..";
                d_type = LINUX_DT_DIR;
                ino = 2u;
            } else {
                uint32_t idx = cursor - 2u;
                if (idx >= (uint32_t)count) {
                    break;
                }
                name = entries[idx].name;
                d_type = linux_d_type_from_mya(entries[idx].type);
                ino = (uint64_t)idx + 3u;
            }

            name_len = linux_cstr_len(name);
            rec_len = linux_align_up((uint64_t)offsetof(linux_dirent64_t, d_name) + (uint64_t)name_len + 1u, 8u);
            if (rec_len > (uint64_t)out_size - (uint64_t)written) {
                if (written == 0u) {
                    return linux_errno(LINUX_EINVAL);
                }
                break;
            }

            rec = (linux_dirent64_t*)(void*)(out + written);
            rec->d_ino = ino;
            rec->d_off = (int64_t)(cursor + 1u);
            rec->d_reclen = (uint16_t)rec_len;
            rec->d_type = d_type;
            if (name_len > 0u) {
                mem_copy(rec->d_name, name, name_len);
            }
            rec->d_name[name_len] = '\0';
            {
                uint64_t used = (uint64_t)offsetof(linux_dirent64_t, d_name) + (uint64_t)name_len + 1u;
                if (rec_len > used) {
                    mem_zero((uint8_t*)rec + used, (size_t)(rec_len - used));
                }
            }

            written += (uint32_t)rec_len;
            cursor++;
        }

        dirh->cursor = cursor;
        return (int64_t)written;
    }
    case 218: /* set_tid_address */
        (void)arg0;
        return scheduler_current_pid();
    case 228: /* clock_gettime */
        return linux_clock_gettime_write(arg1);
    case 229: { /* clock_getres */
        linux_timespec_t* out = (linux_timespec_t*)(uintptr_t)arg1;
        uint32_t hz = linux_timer_hz();

        if (!out || !user_ptr_writable(out, sizeof(*out))) {
            return linux_errno(LINUX_EFAULT);
        }
        out->tv_sec = 0;
        out->tv_nsec = (int64_t)(1000000000ull / (uint64_t)hz);
        if (out->tv_nsec <= 0) {
            out->tv_nsec = 1;
        }
        return 0;
    }
    case 230: { /* clock_nanosleep */
        const linux_timespec_t* req = (const linux_timespec_t*)(uintptr_t)arg2;
        linux_timespec_t* rem = (linux_timespec_t*)(uintptr_t)arg3;
        uint64_t ticks = 0u;
        (void)arg0;
        (void)arg1;

        if (!req || !user_ptr_readable(req, sizeof(*req)) || linux_timespec_to_ticks(req, &ticks) != 0) {
            return linux_errno(LINUX_EINVAL);
        }
        if (rem && user_ptr_writable(rem, sizeof(*rem))) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        scheduler_sleep_current(ticks);
        return 0;
    }
    case 262: { /* newfstatat */
        int32_t dirfd = (int32_t)arg0;
        const char* path = (const char*)(uintptr_t)arg1;
        linux_stat_t* out = (linux_stat_t*)(uintptr_t)arg2;
        uint32_t flags = (uint32_t)arg3;
        char abs_path[MYAOS_PATH_MAX];

        if ((flags & ~(LINUX_AT_EMPTY_PATH | LINUX_AT_SYMLINK_NOFOLLOW)) != 0u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (!user_cstr_valid(path, MYAOS_PATH_MAX) || !out || !user_ptr_writable(out, sizeof(*out))) {
            return linux_errno(LINUX_EFAULT);
        }
        if (path[0] == '\0') {
            if ((flags & LINUX_AT_EMPTY_PATH) == 0u) {
                return linux_errno(LINUX_ENOENT);
            }
            if (dirfd == LINUX_AT_FDCWD) {
                return linux_errno(LINUX_ENOENT);
            }
            return linux_fstat_fd(dirfd, out);
        }
        if (path[0] != '/' && dirfd != LINUX_AT_FDCWD && !linux_fd_is_dir(dirfd)) {
            return linux_errno(LINUX_EBADF);
        }
        if (linux_resolve_at_path(dirfd, path, abs_path, sizeof(abs_path)) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        return linux_stat_abs_path(abs_path, out);
    }
    case 267: { /* readlinkat */
        int32_t dirfd = (int32_t)arg0;
        const char* path = (const char*)(uintptr_t)arg1;
        char* out = (char*)(uintptr_t)arg2;
        uint64_t out_size = arg3;
        const char* target = NULL;
        size_t len;

        char abs_path[MYAOS_PATH_MAX];
        const char* query_path = path;

        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (!out || out_size == 0u || !user_ptr_writable(out, out_size)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (dirfd != LINUX_AT_FDCWD || path[0] != '/') {
            if (path[0] != '/' && dirfd != LINUX_AT_FDCWD && !linux_fd_is_dir(dirfd)) {
                return linux_errno(LINUX_EBADF);
            }
            if (linux_resolve_at_path(dirfd, path, abs_path, sizeof(abs_path)) != 0) {
                return linux_errno(LINUX_ENOENT);
            }
            query_path = abs_path;
        }
        if (linux_cstr_len(query_path) == 14u &&
            query_path[0] == '/' && query_path[1] == 'p' && query_path[2] == 'r' && query_path[3] == 'o' &&
            query_path[4] == 'c' && query_path[5] == '/' && query_path[6] == 's' && query_path[7] == 'e' &&
            query_path[8] == 'l' && query_path[9] == 'f' && query_path[10] == '/' && query_path[11] == 'e' &&
            query_path[12] == 'x' && query_path[13] == 'e') {
            target = "/proc/self/exe";
        } else {
            return linux_errno(LINUX_EINVAL);
        }
        len = linux_cstr_len(target);
        if ((uint64_t)len > out_size) {
            len = (size_t)out_size;
        }
        if (len > 0u) {
            mem_copy(out, target, len);
        }
        return (int64_t)len;
    }
    case 269: { /* faccessat */
        int32_t dirfd = (int32_t)arg0;
        const char* path = (const char*)(uintptr_t)arg1;
        uint32_t mode = (uint32_t)arg2;
        uint32_t flags = (uint32_t)arg3;
        int32_t fd = -1;
        char abs_path[MYAOS_PATH_MAX];

        if ((flags & ~(0x200u | LINUX_AT_SYMLINK_NOFOLLOW)) != 0u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (!user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (path[0] != '/' && dirfd != LINUX_AT_FDCWD && !linux_fd_is_dir(dirfd)) {
            return linux_errno(LINUX_EBADF);
        }
        if (linux_resolve_at_path(dirfd, path, abs_path, sizeof(abs_path)) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        if (!linux_path_exists("/", abs_path)) {
            return linux_errno(LINUX_ENOENT);
        }
        if ((mode & 1u) != 0u && vfs_can_exec("/", abs_path) != 0) {
            return linux_errno(LINUX_EACCES);
        }
        if ((mode & 2u) != 0u) {
            if (vfs_is_dir("/", abs_path) > 0) {
                return linux_errno(LINUX_EACCES);
            }
            if (scheduler_posix_open(abs_path, MYAOS_POSIX_O_WRONLY, &fd) != 0 || fd <= 0) {
                return linux_errno(LINUX_EACCES);
            }
            (void)scheduler_posix_close(fd);
        } else if ((mode & 4u) != 0u) {
            if (vfs_is_dir("/", abs_path) <= 0) {
                if (scheduler_posix_open(abs_path, MYAOS_POSIX_O_RDONLY, &fd) != 0 || fd <= 0) {
                    return linux_errno(LINUX_EACCES);
                }
                (void)scheduler_posix_close(fd);
            }
        }
        return 0;
    }
    case 270: { /* pselect6 */
        int32_t nfds = (int32_t)arg0;
        uint8_t* readfds = (uint8_t*)(uintptr_t)arg1;
        uint8_t* writefds = (uint8_t*)(uintptr_t)arg2;
        uint8_t* exceptfds = (uint8_t*)(uintptr_t)arg3;
        const linux_timespec_t* timeout = (const linux_timespec_t*)(uintptr_t)arg4;
        uint64_t timeout_ticks = 0u;
        uint8_t wait_forever = 1u;

        (void)arg5;
        if (timeout) {
            if (!user_ptr_readable(timeout, sizeof(*timeout)) || linux_timespec_to_ticks(timeout, &timeout_ticks) != 0) {
                return linux_errno(LINUX_EINVAL);
            }
            wait_forever = 0u;
        }
        return linux_select_common(nfds, readfds, writefds, exceptfds, wait_forever, timeout_ticks);
    }
    case 271: { /* ppoll */
        linux_pollfd_t* fds = (linux_pollfd_t*)(uintptr_t)arg0;
        uint32_t nfds = (uint32_t)arg1;
        const linux_timespec_t* timeout = (const linux_timespec_t*)(uintptr_t)arg2;
        uint64_t ticks = 0u;
        int64_t timeout_ms = -1;
        (void)arg3;
        (void)arg4;

        if (timeout) {
            if (!user_ptr_readable(timeout, sizeof(*timeout)) || linux_timespec_to_ticks(timeout, &ticks) != 0) {
                return linux_errno(LINUX_EINVAL);
            }
            timeout_ms = (int64_t)((ticks * 1000ull) / (uint64_t)linux_timer_hz());
            if (timeout_ms < 0) {
                timeout_ms = 0;
            }
        }

        return syscall_dispatch_linux(
            7u,
            (uint64_t)(uintptr_t)fds,
            (uint64_t)nfds,
            (uint64_t)timeout_ms,
            0u,
            0u,
            0u,
            0u
        );
    }
    case 292: { /* dup3 */
        int32_t oldfd = (int32_t)arg0;
        int32_t newfd = (int32_t)arg1;
        uint32_t flags = (uint32_t)arg2;
        uint8_t old_is_pipe = 0u;
        int32_t old_mya;
        int32_t new_mya;

        if (oldfd == newfd) {
            return linux_errno(LINUX_EINVAL);
        }
        if ((flags & ~LINUX_O_CLOEXEC) != 0u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (oldfd < 0 || newfd < 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (oldfd < 3 || newfd < 3 || linux_fd_is_dir(oldfd) || linux_fd_is_dir(newfd)) {
            return linux_errno(LINUX_EBADF);
        }

        old_mya = linux_fd_to_mya_pipe(oldfd);
        if (old_mya > 0) {
            old_is_pipe = 1u;
        } else {
            old_mya = linux_fd_to_mya_file(oldfd);
        }
        if (old_mya <= 0) {
            return linux_errno(LINUX_EBADF);
        }

        if (old_is_pipe) {
            if (newfd < LINUX_FD_PIPE_BASE || newfd >= LINUX_FD_DIR_BASE) {
                return linux_errno(LINUX_EBADF);
            }
            new_mya = linux_fd_to_mya_pipe(newfd);
        } else {
            if (newfd >= LINUX_FD_PIPE_BASE) {
                return linux_errno(LINUX_EBADF);
            }
            new_mya = linux_fd_to_mya_file(newfd);
        }
        if (new_mya <= 0) {
            return linux_errno(LINUX_EBADF);
        }
        if (scheduler_posix_dup2(old_mya, new_mya) != 0) {
            return linux_errno(LINUX_EBADF);
        }
        return newfd;
    }
    case 293: { /* pipe2 */
        int32_t* fds = (int32_t*)(uintptr_t)arg0;
        uint32_t flags = (uint32_t)arg1;
        int32_t read_fd = -1;
        int32_t write_fd = -1;

        if ((flags & ~(LINUX_O_NONBLOCK | LINUX_O_CLOEXEC)) != 0u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (!fds || !user_ptr_writable(fds, sizeof(int32_t) * 2u)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (scheduler_pipe_create(&read_fd, &write_fd) != 0 || read_fd <= 0 || write_fd <= 0) {
            return linux_errno(LINUX_ENOMEM);
        }
        fds[0] = linux_fd_from_mya_pipe(read_fd);
        fds[1] = linux_fd_from_mya_pipe(write_fd);
        return 0;
    }
    case 302: { /* prlimit64 */
        int32_t pid = (int32_t)arg0;
        uint32_t resource = (uint32_t)arg1;
        const linux_rlimit_t* new_limit = (const linux_rlimit_t*)(uintptr_t)arg2;
        linux_rlimit_t* old_limit = (linux_rlimit_t*)(uintptr_t)arg3;

        if (pid != 0 && pid != scheduler_current_pid()) {
            return linux_errno(LINUX_ESRCH);
        }
        if (new_limit && !user_ptr_readable(new_limit, sizeof(*new_limit))) {
            return linux_errno(LINUX_EFAULT);
        }
        if (old_limit && !user_ptr_writable(old_limit, sizeof(*old_limit))) {
            return linux_errno(LINUX_EFAULT);
        }
        if (old_limit) {
            linux_rlimit_default(resource, old_limit);
        }
        return 0;
    }
    case 322: { /* execveat */
        int32_t dirfd = (int32_t)arg0;
        const char* path = (const char*)(uintptr_t)arg1;
        const char* const* argv = (const char* const*)(uintptr_t)arg2;
        uint32_t flags = (uint32_t)arg4;
        char exec_path[MYAOS_PATH_MAX];
        int argc = 0;

        if ((flags & ~LINUX_AT_EMPTY_PATH) != 0u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (!path || !user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (linux_scan_argv_user(argv, &argc) != 0) {
            return linux_errno(LINUX_EFAULT);
        }
        if (path[0] == '\0') {
            if ((flags & LINUX_AT_EMPTY_PATH) == 0u) {
                return linux_errno(LINUX_ENOENT);
            }
            return linux_errno(LINUX_EINVAL);
        }
        if (path[0] != '/' && dirfd != LINUX_AT_FDCWD && !linux_fd_is_dir(dirfd)) {
            return linux_errno(LINUX_EBADF);
        }
        if (linux_resolve_at_path(dirfd, path, exec_path, sizeof(exec_path)) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        if (scheduler_exec_current(exec_path, argc, argv) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        return 0;
    }
    case 318: { /* getrandom */
        uint8_t* out = (uint8_t*)(uintptr_t)arg0;
        uint32_t len = (uint32_t)arg1;
        uint64_t seed = timer_ticks() ^ ((uint64_t)(uintptr_t)out << 1);
        (void)arg2;

        if (len != 0u && !user_ptr_writable(out, len)) {
            return linux_errno(LINUX_EFAULT);
        }
        for (uint32_t i = 0u; i < len; i++) {
            seed = seed * 6364136223846793005ull + 1ull;
            out[i] = (uint8_t)(seed >> 32);
        }
        return (int64_t)len;
    }
    case 332: { /* statx */
        int32_t dirfd = (int32_t)arg0;
        const char* path = (const char*)(uintptr_t)arg1;
        uint32_t flags = (uint32_t)arg2;
        uint32_t mask = (uint32_t)arg3;
        linux_statx_t* out = (linux_statx_t*)(uintptr_t)arg4;
        char abs_path[MYAOS_PATH_MAX];

        if ((flags & ~(LINUX_AT_EMPTY_PATH | LINUX_AT_SYMLINK_NOFOLLOW)) != 0u) {
            return linux_errno(LINUX_EINVAL);
        }
        if (!out || !user_ptr_writable(out, sizeof(*out))) {
            return linux_errno(LINUX_EFAULT);
        }
        if (!path || !user_cstr_valid(path, MYAOS_PATH_MAX)) {
            return linux_errno(LINUX_EFAULT);
        }
        if (path[0] == '\0') {
            if ((flags & LINUX_AT_EMPTY_PATH) == 0u) {
                return linux_errno(LINUX_ENOENT);
            }
            if (dirfd == LINUX_AT_FDCWD) {
                return linux_errno(LINUX_ENOENT);
            }
            return linux_statx_fd(dirfd, out, mask);
        }
        if (path[0] != '/' && dirfd != LINUX_AT_FDCWD && !linux_fd_is_dir(dirfd)) {
            return linux_errno(LINUX_EBADF);
        }
        if (linux_resolve_at_path(dirfd, path, abs_path, sizeof(abs_path)) != 0) {
            return linux_errno(LINUX_ENOENT);
        }
        return linux_statx_abs_path(abs_path, out, mask);
    }
    case 334: /* rseq */
        return 0;
    default:
        return linux_errno(LINUX_ENOSYS);
    }
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
        const uint8_t* text = (const uint8_t*)(uintptr_t)arg0;
        uint32_t text_size = (uint32_t)arg1;
        return syscall_console_write_bytes(text, text_size);
    }
    case MYAOS_SYS_CONSOLE_CLEAR:
        console_reset();
        return 0;
    case MYAOS_SYS_CONSOLE_READCHAR:
        return (uint8_t)keyboard_read_char();
    case MYAOS_SYS_INPUT_KEY_STATE:
        if (arg0 >= MYAOS_INPUT_KEYBOARD_KEYS) {
            return -1;
        }
        return keyboard_key_state((uint16_t)arg0);
    case MYAOS_SYS_INPUT_KEYBOARD_STATE:
        if (arg0 == 0u || arg1 < MYAOS_INPUT_KEYBOARD_KEYS ||
            !user_ptr_writable((void*)(uintptr_t)arg0, MYAOS_INPUT_KEYBOARD_KEYS)) {
            return -1;
        }
        return keyboard_keyboard_state((uint8_t*)(uintptr_t)arg0, (uint32_t)arg1);
    case MYAOS_SYS_INPUT_KEY_EVENT_READ:
        if (arg0 == 0u ||
            !user_ptr_writable((void*)(uintptr_t)arg0, sizeof(myaos_input_key_event_t))) {
            return -1;
        }
        return keyboard_key_event_read((myaos_input_key_event_t*)(uintptr_t)arg0);
    case MYAOS_SYS_EVENT_POLL: {
        uint32_t* out_bits = (uint32_t*)(uintptr_t)arg2;
        uint32_t bits = 0u;
        uint32_t notify_bits = 0u;

        if (!out_bits || !user_ptr_writable(out_bits, sizeof(*out_bits))) {
            return -1;
        }
        if (scheduler_notify_poll((uint32_t)arg0, (uint8_t)arg1, &notify_bits) == 0) {
            bits |= notify_bits;
        }
        if (((uint32_t)arg0 == 0u || (((uint32_t)arg0 & MYAOS_EVENT_INPUT_KEYBOARD) != 0u)) &&
            keyboard_event_pending() != 0u) {
            bits |= MYAOS_EVENT_INPUT_KEYBOARD;
        }
        *out_bits = bits;
        return bits ? 0 : -1;
    }
    case MYAOS_SYS_INPUT_WAIT: {
        uint32_t timeout_ticks = (uint32_t)arg0;
        uint32_t* out_bits = (uint32_t*)(uintptr_t)arg1;

        if (!out_bits || !user_ptr_writable(out_bits, sizeof(*out_bits))) {
            return -1;
        }

        *out_bits = 0u;
        if (keyboard_event_pending() != 0u) {
            *out_bits = MYAOS_EVENT_INPUT_KEYBOARD;
            return 0;
        }
        if (timeout_ticks == 0u) {
            return -1;
        }
        return scheduler_notify_wait(MYAOS_EVENT_INPUT_KEYBOARD, timeout_ticks, 1u, out_bits);
    }
    case MYAOS_SYS_TIME_TICKS:
        return (int64_t)timer_ticks();
    case MYAOS_SYS_TIME_FREQ:
        return (int64_t)timer_hz();
    case MYAOS_SYS_GFX_MODE_SET:
        if (!g_boot || arg0 > MYAOS_GFX_MODE_GRAPHICS) {
            return -1;
        }
        console_set_mode((uint32_t)arg0);
        return 0;
    case MYAOS_SYS_GFX_MODE_GET:
        if (!g_boot || arg0 == 0u || !user_ptr_writable((void*)(uintptr_t)arg0, sizeof(uint32_t))) {
            return -1;
        }
        *((uint32_t*)(uintptr_t)arg0) = console_get_mode();
        return 0;
    case MYAOS_SYS_GFX_INFO_GET: {
        myaos_gfx_info_t* out = (myaos_gfx_info_t*)(uintptr_t)arg0;
        if (!g_boot || !out || !user_ptr_writable(out, sizeof(*out))) {
            return -1;
        }
        out->width = g_boot->fb.width;
        out->height = g_boot->fb.height;
        out->stride = g_boot->fb.pixels_per_scanline;
        out->format = g_boot->fb.format;
        out->mode = console_get_mode();
        out->reserved0 = 0u;
        out->reserved1 = 0u;
        out->reserved2 = 0u;
        return 0;
    }
    case MYAOS_SYS_GFX_DRAW:
        if (arg0 == 0u || !user_ptr_readable((const void*)(uintptr_t)arg0, sizeof(myaos_gfx_object_t))) {
            return -1;
        }
        return syscall_gfx_draw_one((const myaos_gfx_object_t*)(uintptr_t)arg0);
    case MYAOS_SYS_GFX_DRAW_BATCH: {
        const myaos_gfx_object_t* objs = (const myaos_gfx_object_t*)(uintptr_t)arg0;
        uint32_t count = (uint32_t)arg1;
        if (!g_boot) {
            return -1;
        }
        if (count == 0u) {
            return 0;
        }
        if (objs == NULL || count > SYSCALL_GFX_BATCH_MAX ||
            !user_ptr_readable(objs, (uint64_t)count * sizeof(*objs))) {
            return -1;
        }
        for (uint32_t i = 0u; i < count; i++) {
            if (syscall_gfx_draw_one(&objs[i]) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case MYAOS_SYS_GFX_BLIT:
        if (!g_boot || arg0 == 0u ||
            !user_ptr_readable((const void*)(uintptr_t)arg0, sizeof(myaos_gfx_blit_t))) {
            return -1;
        }
        return syscall_gfx_blit((const myaos_gfx_blit_t*)(uintptr_t)arg0);
    case MYAOS_SYS_GFX_PRESENT:
        if (!g_boot) {
            return -1;
        }
        return graphics_present(g_boot);
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
    case MYAOS_SYS_FS_CHMOD:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        return vfs_chmod(scheduler_current_cwd(), (const char*)(uintptr_t)arg0, (uint16_t)arg1);
    case MYAOS_SYS_FS_CHOWN:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        return vfs_chown(scheduler_current_cwd(), (const char*)(uintptr_t)arg0, (uint32_t)arg1);
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
    case MYAOS_SYS_SOCK_CONNECT4:
        return net_socket_connect4(
            scheduler_current_pid(),
            (int32_t)arg0,
            (uint32_t)arg1,
            (uint16_t)arg2
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
    case MYAOS_SYS_NET_PING4:
        if (arg4 != 0u && !user_ptr_writable((void*)(uintptr_t)arg4, sizeof(uint32_t))) {
            return -1;
        }
        return net_ping4(
            (uint32_t)arg0,
            (uint16_t)arg1,
            (uint16_t)arg2,
            (uint32_t)arg3,
            (uint32_t*)(uintptr_t)arg4
        );
    case MYAOS_SYS_POSIX_OPEN:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX) ||
            !user_ptr_writable((void*)(uintptr_t)arg2, sizeof(int32_t))) {
            return -1;
        }
        return scheduler_posix_open(
            (const char*)(uintptr_t)arg0,
            (uint32_t)arg1,
            (int32_t*)(uintptr_t)arg2
        );
    case MYAOS_SYS_POSIX_CLOSE:
        return scheduler_posix_close((int32_t)arg0);
    case MYAOS_SYS_POSIX_READ:
        if (arg3 == 0u || (arg2 != 0u && !user_ptr_writable((void*)(uintptr_t)arg1, (uint32_t)arg2)) ||
            !user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_posix_read(
            (int32_t)arg0,
            (void*)(uintptr_t)arg1,
            (uint32_t)arg2,
            (uint32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_POSIX_WRITE:
        if (arg3 == 0u || (arg2 != 0u && !user_ptr_readable((const void*)(uintptr_t)arg1, (uint32_t)arg2)) ||
            !user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_posix_write(
            (int32_t)arg0,
            (const void*)(uintptr_t)arg1,
            (uint32_t)arg2,
            (uint32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_POSIX_LSEEK:
        if (arg3 != 0u && !user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint64_t))) {
            return -1;
        }
        return scheduler_posix_lseek(
            (int32_t)arg0,
            (int64_t)arg1,
            (uint32_t)arg2,
            (uint64_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_POSIX_FSTAT:
        if (!user_ptr_writable((void*)(uintptr_t)arg1, sizeof(myaos_posix_stat_t))) {
            return -1;
        }
        return scheduler_posix_fstat((int32_t)arg0, (myaos_posix_stat_t*)(uintptr_t)arg1);
    case MYAOS_SYS_POSIX_DUP2:
        return scheduler_posix_dup2((int32_t)arg0, (int32_t)arg1);
    case MYAOS_SYS_POSIX_POLL:
        if (arg1 > 256u || arg3 == 0u || (arg1 != 0u &&
            !user_ptr_writable((void*)(uintptr_t)arg0, (uint64_t)arg1 * sizeof(myaos_posix_pollfd_t))) ||
            !user_ptr_writable((void*)(uintptr_t)arg3, sizeof(uint32_t))) {
            return -1;
        }
        return scheduler_posix_poll(
            (myaos_posix_pollfd_t*)(uintptr_t)arg0,
            (uint32_t)arg1,
            (uint32_t)arg2,
            (uint32_t*)(uintptr_t)arg3
        );
    case MYAOS_SYS_SEC_WHOAMI:
        return (int64_t)scheduler_current_uid();
    case MYAOS_SYS_SEC_LOGIN:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX)) {
            return -1;
        }
        return scheduler_login_current((const char*)(uintptr_t)arg0);
    case MYAOS_SYS_FB_PREF_SET:
        if (!g_boot) {
            return -1;
        }
        return power_set_gop_mode_pref(g_boot, (uint8_t)(arg0 ? 1u : 0u), (uint32_t)arg1);
    case MYAOS_SYS_FB_PREF_GET: {
        uint8_t has_mode = 0u;
        uint32_t mode = 0u;
        if (!g_boot || !user_ptr_writable((void*)(uintptr_t)arg0, sizeof(uint32_t)) ||
            !user_ptr_writable((void*)(uintptr_t)arg1, sizeof(uint32_t))) {
            return -1;
        }
        if (power_get_gop_mode_pref(g_boot, &has_mode, &mode) != 0) {
            return -1;
        }
        *((uint32_t*)(uintptr_t)arg0) = has_mode ? 1u : 0u;
        *((uint32_t*)(uintptr_t)arg1) = mode;
        return 0;
    }
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
        return module_load((const char*)(uintptr_t)arg0);
    case MYAOS_SYS_MOD_LOAD_FILE:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_PATH_MAX)) {
            return -1;
        }
        return module_load_file((const char*)(uintptr_t)arg0);
    case MYAOS_SYS_MOD_UNLOAD:
        if (!user_cstr_valid((const char*)(uintptr_t)arg0, MYAOS_NAME_MAX)) {
            return -1;
        }
        return module_unload((const char*)(uintptr_t)arg0);
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
