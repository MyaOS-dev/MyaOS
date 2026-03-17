#ifndef MYAOS_SYSCALL_ABI_H
#define MYAOS_SYSCALL_ABI_H

#include <stdint.h>

#define MYAOS_ABI_VERSION_MAJOR 1u
#define MYAOS_ABI_VERSION_MINOR 8u
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
};

#endif
