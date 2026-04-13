#ifndef MYAOS_SYSCALL_ABI_H
#define MYAOS_SYSCALL_ABI_H

#include <stdint.h>

#define MYAOS_ABI_VERSION_MAJOR 1u
#define MYAOS_ABI_VERSION_MINOR 15u
#define MYAOS_ABI_VERSION_PATCH 0u
#define MYAOS_ABI_VERSION_ENCODE(major, minor, patch) (((major) * 10000u) + ((minor) * 100u) + (patch))
#define MYAOS_ABI_VERSION \
    MYAOS_ABI_VERSION_ENCODE(MYAOS_ABI_VERSION_MAJOR, MYAOS_ABI_VERSION_MINOR, MYAOS_ABI_VERSION_PATCH)

#define MYAOS_PATH_MAX 128
#define MYAOS_NAME_MAX 32
#define MYAOS_DESC_MAX 64
#define MYAOS_PROC_NAME_MAX 24
#define MYAOS_DEVICE_NAME_MAX 24
#define MYAOS_DEVICE_DRIVER_MAX 24
#define MYAOS_IPC_MSG_MAX 128
#define MYAOS_INVALID_DISK_ID 0xFFFFFFFFu

typedef enum {
    MYAOS_NODE_NONE = 0,
    MYAOS_NODE_FILE = 1,
    MYAOS_NODE_DIR = 2,
    MYAOS_NODE_MOUNT = 3,
} myaos_node_type_t;

typedef enum {
    MYAOS_PROC_NONE = 0,
    MYAOS_PROC_RUNNING = 1,
    MYAOS_PROC_READY = 2,
    MYAOS_PROC_BLOCKED = 3,
    MYAOS_PROC_SLEEPING = 4,
    MYAOS_PROC_ZOMBIE = 5,
} myaos_proc_state_t;

typedef enum {
    MYAOS_PROC_PRIO_LOW = 1,
    MYAOS_PROC_PRIO_NORMAL = 2,
    MYAOS_PROC_PRIO_HIGH = 3,
} myaos_proc_priority_t;

typedef enum {
    MYAOS_DEV_NONE = 0,
    MYAOS_DEV_BLOCK = 1,
    MYAOS_DEV_INPUT = 2,
    MYAOS_DEV_TIMER = 3,
    MYAOS_DEV_POWER = 4,
    MYAOS_DEV_CONSOLE = 5,
    MYAOS_DEV_NETWORK = 6,
} myaos_device_class_t;

typedef struct {
    char name[MYAOS_NAME_MAX];
    uint8_t type;
    uint32_t size;
} myaos_dirent_t;

typedef struct {
    char path[MYAOS_PATH_MAX];
    char fs_name[MYAOS_NAME_MAX];
    char source[MYAOS_NAME_MAX];
    uint32_t disk_id;
    uint8_t read_only;
} myaos_mount_info_t;

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    char name[MYAOS_PROC_NAME_MAX];
    uint8_t state;
    uint8_t background;
    uint8_t priority;
    uint8_t reserved0;
    int32_t exit_code;
    uint32_t cpu_limit_ticks;
    uint32_t reserved1;
    uint64_t cpu_ticks_used;
    uint64_t wake_tick;
    uint64_t run_count;
    uint64_t last_run_tick;
    uint64_t vm_limit_bytes;
    uint64_t vm_used_bytes;
} myaos_proc_info_t;

typedef struct {
    uint32_t id;
    uint32_t class_id;
    char name[MYAOS_DEVICE_NAME_MAX];
    char driver[MYAOS_DEVICE_DRIVER_MAX];
} myaos_device_info_t;

typedef struct {
    uint32_t id;
    uint32_t block_size;
    uint64_t block_count;
    uint64_t size_bytes;
    uint8_t read_only;
    uint8_t is_boot;
    char name[MYAOS_DEVICE_NAME_MAX];
    char fs_name[MYAOS_NAME_MAX];
} myaos_disk_info_t;

typedef struct {
    uint64_t timer_ticks;
    uint64_t pmm_managed_pages;
    uint64_t pmm_free_pages;
    uint64_t pmm_allocated_pages;
    uint64_t heap_page_count;
    uint64_t heap_alloc_count;
    uint64_t heap_bytes_used;
    uint64_t heap_bytes_capacity;
    uint64_t paging_mapped_bytes;
    uint64_t paging_table_pages;
    uint64_t paging_cr3;
    uint64_t swap_total_bytes;
    uint64_t swap_used_bytes;
    uint32_t swap_slots;
    uint32_t swap_used_slots;
} myaos_meminfo_t;

typedef struct {
    uint32_t timer_hz;
    uint64_t timer_ticks;
    uint32_t process_count;
    uint32_t current_pid;
} myaos_sched_info_t;

typedef struct {
    uint32_t flags;
    uint8_t stdout_append;
    uint8_t priority;
    uint16_t reserved0;
    uint32_t cpu_limit_ticks;
    char stdout_path[MYAOS_PATH_MAX];
} myaos_spawn_opts_t;

typedef struct {
    uint32_t cpu_limit_ticks;
    uint32_t reserved0;
    uint64_t vm_limit_bytes;
} myaos_proc_limits_t;

enum {
    MYAOS_SPAWN_BACKGROUND = 0x1u,
    MYAOS_SPAWN_STDOUT_REDIRECT = 0x2u,
    MYAOS_SPAWN_LINUX = 0x4u,
};

enum {
    MYAOS_MEM_MAP_WRITABLE = 0x1u,
};

enum {
    MYAOS_SOCK_PROTO_TCP = 6u,
    MYAOS_SOCK_PROTO_UDP = 17u,
};

typedef struct {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t dropped_packets;
    uint32_t socket_count;
    uint32_t bound_ports;
} myaos_netinfo_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint32_t slot_count;
    uint32_t used_slots;
    uint64_t swap_out_ops;
    uint64_t swap_in_ops;
} myaos_swap_info_t;

typedef struct {
    char name[MYAOS_NAME_MAX];
    char description[MYAOS_DESC_MAX];
    uint8_t loaded;
    uint8_t optional;
    uint16_t reserved0;
} myaos_module_info_t;

typedef struct {
    uint64_t size;
    uint32_t mode;
    uint32_t uid;
} myaos_posix_stat_t;

typedef struct {
    int32_t fd;
    int16_t events;
    int16_t revents;
} myaos_posix_pollfd_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t mode;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
} myaos_gfx_info_t;

typedef struct {
    uint32_t type;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    uint32_t color;
    uint32_t color2;
    uint64_t text_ptr;
    uint32_t text_len;
    uint32_t flags;
} myaos_gfx_object_t;

typedef struct {
    int32_t dst_x;
    int32_t dst_y;
    uint32_t width;
    uint32_t height;
    uint32_t src_stride;
    uint32_t src_format;
    uint64_t src_ptr;
    uint32_t flags;
} myaos_gfx_blit_t;

typedef struct {
    uint16_t keycode;
    uint8_t action;
    uint8_t modifiers;
    uint32_t ascii;
    uint64_t tick;
} myaos_input_key_event_t;

enum {
    MYAOS_POSIX_O_ACCMODE = 0x0003u,
    MYAOS_POSIX_O_RDONLY = 0x0000u,
    MYAOS_POSIX_O_WRONLY = 0x0001u,
    MYAOS_POSIX_O_RDWR = 0x0002u,
    MYAOS_POSIX_O_CREAT = 0x0040u,
    MYAOS_POSIX_O_TRUNC = 0x0200u,
    MYAOS_POSIX_O_APPEND = 0x0400u,
};

enum {
    MYAOS_POSIX_SEEK_SET = 0,
    MYAOS_POSIX_SEEK_CUR = 1,
    MYAOS_POSIX_SEEK_END = 2,
};

enum {
    MYAOS_POSIX_POLLIN = 0x0001,
    MYAOS_POSIX_POLLOUT = 0x0004,
    MYAOS_POSIX_POLLERR = 0x0008,
};

enum {
    MYAOS_GFX_MODE_TEXT = 0u,
    MYAOS_GFX_MODE_GRAPHICS = 1u,
};

enum {
    MYAOS_GFX_OBJ_CLEAR = 1u,
    MYAOS_GFX_OBJ_PIXEL = 2u,
    MYAOS_GFX_OBJ_LINE = 3u,
    MYAOS_GFX_OBJ_RECT = 4u,
    MYAOS_GFX_OBJ_FRAME = 5u,
    MYAOS_GFX_OBJ_TEXT = 6u,
};

enum {
    MYAOS_GFX_TEXT_TRANSPARENT_BG = 0x0001u,
};

enum {
    MYAOS_GFX_SRC_BGR24 = 1u,
    MYAOS_GFX_SRC_BGRA32 = 2u,
    MYAOS_GFX_SRC_RGB24 = 3u,
    MYAOS_GFX_SRC_RGBA32 = 4u,
};

enum {
    MYAOS_GFX_BLIT_FLIP_Y = 0x0001u,
};

enum {
    MYAOS_INPUT_KEYBOARD_KEYS = 256u,
};

enum {
    MYAOS_INPUT_KEY_EVENT_PRESS = 1u,
    MYAOS_INPUT_KEY_EVENT_RELEASE = 2u,
    MYAOS_INPUT_KEY_EVENT_REPEAT = 3u,
};

enum {
    MYAOS_INPUT_MOD_SHIFT = 0x0001u,
    MYAOS_INPUT_MOD_CTRL = 0x0002u,
    MYAOS_INPUT_MOD_ALT = 0x0004u,
    MYAOS_INPUT_MOD_GUI = 0x0008u,
};

enum {
    MYAOS_EVENT_INPUT_KEYBOARD = 0x00010000u,
};

enum {
    MYAOS_SYS_CONSOLE_WRITE = 1,
    MYAOS_SYS_CONSOLE_CLEAR = 2,
    MYAOS_SYS_CONSOLE_READCHAR = 3,
    MYAOS_SYS_FS_LIST = 10,
    MYAOS_SYS_FS_READ = 11,
    MYAOS_SYS_FS_WRITE = 12,
    MYAOS_SYS_FS_MKDIR = 13,
    MYAOS_SYS_FS_TOUCH = 14,
    MYAOS_SYS_FS_CHDIR = 15,
    MYAOS_SYS_FS_GETCWD = 16,
    MYAOS_SYS_FS_MOUNTS = 17,
    MYAOS_SYS_FS_MOUNT = 18,
    MYAOS_SYS_FS_REMOVE = 19,
    MYAOS_SYS_PROC_SPAWN = 30,
    MYAOS_SYS_PROC_EXEC = 31,
    MYAOS_SYS_PROC_WAIT = 32,
    MYAOS_SYS_PROC_EXIT = 33,
    MYAOS_SYS_PROC_YIELD = 34,
    MYAOS_SYS_PROC_SLEEP = 35,
    MYAOS_SYS_PROC_GETPID = 36,
    MYAOS_SYS_PROC_INFO = 37,
    MYAOS_SYS_PROC_SCHED = 38,
    MYAOS_SYS_PROC_NOTIFY = 39,
    MYAOS_SYS_PROC_NOTIFY_POLL = 40,
    MYAOS_SYS_PROC_MSG_SEND = 41,
    MYAOS_SYS_PROC_MSG_RECV = 42,
    MYAOS_SYS_PROC_SPAWN_EX = 43,
    MYAOS_SYS_PROC_WAIT_POLL = 44,
    MYAOS_SYS_PROC_KILL = 45,
    MYAOS_SYS_MEM_MAP = 46,
    MYAOS_SYS_MEM_UNMAP = 47,
    MYAOS_SYS_PIPE_CREATE = 48,
    MYAOS_SYS_PIPE_CLOSE = 49,
    MYAOS_SYS_DEV_LIST = 50,
    MYAOS_SYS_DEV_DISKS = 51,
    MYAOS_SYS_PIPE_READ = 52,
    MYAOS_SYS_PIPE_WRITE = 53,
    MYAOS_SYS_THREAD_CREATE = 54,
    MYAOS_SYS_SHM_CREATE = 56,
    MYAOS_SYS_SHM_OPEN = 57,
    MYAOS_SYS_SEC_WHOAMI = 58,
    MYAOS_SYS_SEC_LOGIN = 59,
    MYAOS_SYS_SYS_MEMINFO = 60,
    MYAOS_SYS_SYS_HALT = 61,
    MYAOS_SYS_SYS_REBOOT = 62,
    MYAOS_SYS_SYS_SHUTDOWN = 63,
    MYAOS_SYS_SHM_READ = 64,
    MYAOS_SYS_SHM_WRITE = 65,
    MYAOS_SYS_SHM_CLOSE = 66,
    MYAOS_SYS_SOCK_OPEN = 67,
    MYAOS_SYS_SOCK_CLOSE = 68,
    MYAOS_SYS_SOCK_SEND = 69,
    MYAOS_SYS_SOCK_RECV = 70,
    MYAOS_SYS_NET_INFO = 71,
    MYAOS_SYS_MEM_SWAP_OUT = 72,
    MYAOS_SYS_MEM_SWAP_IN = 73,
    MYAOS_SYS_MEM_SWAP_INFO = 74,
    MYAOS_SYS_DEV_HOTPLUG_RAMDISK = 75,
    MYAOS_SYS_MOD_LIST = 76,
    MYAOS_SYS_MOD_LOAD = 77,
    MYAOS_SYS_MOD_UNLOAD = 78,
    MYAOS_SYS_THREAD_JOIN = 79,
    MYAOS_SYS_THREAD_JOIN_POLL = 80,
    MYAOS_SYS_PROC_SET_LIMITS = 81,
    MYAOS_SYS_PROC_GET_LIMITS = 82,
    MYAOS_SYS_PROC_NOTIFY_WAIT = 83,
    MYAOS_SYS_SOCK_OPEN_EX = 84,
    MYAOS_SYS_SOCK_BIND = 85,
    MYAOS_SYS_SOCK_LISTEN = 86,
    MYAOS_SYS_SOCK_ACCEPT = 87,
    MYAOS_SYS_SOCK_CONNECT = 88,
    MYAOS_SYS_SOCK_SEND_TO = 89,
    MYAOS_SYS_SOCK_RECV_FROM = 90,
    MYAOS_SYS_NET_SEND_UDP4 = 91,
    MYAOS_SYS_POSIX_OPEN = 92,
    MYAOS_SYS_POSIX_CLOSE = 93,
    MYAOS_SYS_POSIX_READ = 94,
    MYAOS_SYS_POSIX_WRITE = 95,
    MYAOS_SYS_POSIX_LSEEK = 96,
    MYAOS_SYS_POSIX_FSTAT = 97,
    MYAOS_SYS_POSIX_DUP2 = 98,
    MYAOS_SYS_POSIX_POLL = 99,
    MYAOS_SYS_FS_CHMOD = 100,
    MYAOS_SYS_FS_CHOWN = 101,
    MYAOS_SYS_SOCK_CONNECT4 = 102,
    MYAOS_SYS_NET_PING4 = 103,
    MYAOS_SYS_FB_PREF_SET = 104,
    MYAOS_SYS_FB_PREF_GET = 105,
    MYAOS_SYS_GFX_MODE_SET = 106,
    MYAOS_SYS_GFX_MODE_GET = 107,
    MYAOS_SYS_GFX_INFO_GET = 108,
    MYAOS_SYS_GFX_DRAW = 109,
    MYAOS_SYS_GFX_DRAW_BATCH = 110,
    MYAOS_SYS_GFX_BLIT = 111,
    MYAOS_SYS_INPUT_KEY_STATE = 112,
    MYAOS_SYS_INPUT_KEYBOARD_STATE = 113,
    MYAOS_SYS_INPUT_KEY_EVENT_READ = 114,
    MYAOS_SYS_GFX_PRESENT = 115,
    MYAOS_SYS_TIME_TICKS = 116,
    MYAOS_SYS_TIME_FREQ = 117,
    MYAOS_SYS_INPUT_WAIT = 118,
    MYAOS_SYS_EVENT_POLL = 119,
    MYAOS_SYS_MOD_LOAD_FILE = 120,
};

#endif
