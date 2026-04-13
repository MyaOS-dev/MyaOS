#include "boot.h"
#include "console.h"
#include "gdt.h"
#include "init.h"
#include "interrupts.h"
#include "log.h"
#include "module.h"
#include "shell.h"
#include "syscall.h"
#include "blockio.h"
#include "device.h"
#include "keyboard.h"
#include "panic.h"
#include "power.h"
#include "timer.h"
#include "fat32.h"
#include "fs_driver.h"
#include "vfs.h"
#include "vfs_fat32.h"
#include "vfs_myafs.h"
#include "vfs_ramfs.h"
#include "vfs_sysview.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "swap.h"
#include "net.h"
#include <stdint.h>

#define KERNEL_TIMER_HZ 250u
#define KERNEL_BOOT_MENU_TIMEOUT_TICKS (KERNEL_TIMER_HZ * 3u)
#define KERNEL_BOOT_MENU_FALLBACK_SPINS 2000000ull
#define KERNEL_BOOT_LOG_MAX (16u * 1024u)
#define KERNEL_BOOT_CURRENT_LOG "/var/log/boot.current.log"
#define KERNEL_BOOT_LAST_LOG "/var/log/boot.last.log"

extern char _kernel_start;
extern char _kernel_end;

static vfs_fat32_t g_boot_fs;
static vfs_ramfs_t g_ram_fs;
static vfs_ramfs_t g_home_fs;
static vfs_ramfs_t g_tmp_fs;
static vfs_ramfs_t g_run_fs;
static vfs_myafs_t g_mya_fs;
static vfs_sysview_t g_dvc_view;
static vfs_sysview_t g_prc_view;
static vfs_sysview_t g_cfg_view;

static inline void dbg_putc(char c) {
    __asm__ __volatile__("outb %0, $0xe9" : : "a"(c));
}

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int str_ends_with(const char* text, const char* suffix) {
    size_t text_len = str_len(text);
    size_t suffix_len = str_len(suffix);

    if (suffix_len > text_len) {
        return 0;
    }
    for (size_t i = 0; i < suffix_len; i++) {
        if (text[text_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

static int build_path(const char* base, const char* name, char* out, size_t out_size) {
    size_t pos = 0;

    if (!base || !name || !out || out_size == 0u) {
        return -1;
    }

    while (base[pos] && pos + 1u < out_size) {
        out[pos] = base[pos];
        pos++;
    }
    if (base[pos] != '\0') {
        return -1;
    }
    if (pos == 0u || out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = '/';
    }
    for (size_t i = 0; name[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = name[i];
    }
    out[pos] = '\0';
    return 0;
}

static void bootstrap_sync_command_manifests(void) {
    myaos_dirent_t entries[96];
    size_t count = 0;

    (void)vfs_mkdir("/", "/cmd");
    if (vfs_list("/", "/boot/cmd", entries, 96, &count) != 0) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        char src[MYAOS_PATH_MAX];
        char dst[MYAOS_PATH_MAX];
        uint8_t stack_buf[512];
        uint8_t* file_buf = stack_buf;
        uint32_t read_size = 0;
        uint32_t read_cap = (uint32_t)sizeof(stack_buf);
        int need_free = 0;

        if (entries[i].type != MYAOS_NODE_FILE || !str_ends_with(entries[i].name, ".cmd")) {
            continue;
        }
        if (entries[i].size > read_cap) {
            file_buf = (uint8_t*)kmalloc(entries[i].size);
            if (!file_buf) {
                continue;
            }
            read_cap = entries[i].size;
            need_free = 1;
        }
        if (build_path("/boot/cmd", entries[i].name, src, sizeof(src)) != 0 ||
            build_path("/cmd", entries[i].name, dst, sizeof(dst)) != 0) {
            if (need_free) {
                kfree(file_buf);
            }
            continue;
        }
        if (vfs_read_file("/", src, file_buf, read_cap, &read_size) != 0) {
            if (need_free) {
                kfree(file_buf);
            }
            continue;
        }
        (void)vfs_write_file("/", dst, file_buf, read_size);
        if (need_free) {
            kfree(file_buf);
        }
    }
}

static void kernel_mount_filesystems(boot_info_t* boot) {
    const blockio_disk_t* boot_disk = blockio_get_disk(0);
    const blockio_disk_t* root_disk = NULL;
    const char* root_source = "myafs";
    uint32_t root_disk_id = MYAOS_INVALID_DISK_ID;
    uint8_t root_read_only = 0u;
    uint32_t disk_count = blockio_disk_count();

    vfs_init();
    fs_driver_init();

    for (uint32_t i = 0; i < disk_count; i++) {
        const blockio_disk_t* disk = blockio_get_disk(i);
        if (disk && disk->is_boot && vfs_myafs_probe_disk(disk)) {
            root_disk = disk;
            break;
        }
    }
    if (!root_disk) {
        for (uint32_t i = 0; i < disk_count; i++) {
            const blockio_disk_t* disk = blockio_get_disk(i);
            if (disk && vfs_myafs_probe_disk(disk)) {
                root_disk = disk;
                break;
            }
        }
    }

    if (root_disk && vfs_myafs_mount_disk(&g_mya_fs, root_disk) == 0 &&
        vfs_mount("/", "myafs", root_disk->name, root_disk->disk_id, (uint8_t)root_disk->read_only, &g_mya_fs, vfs_myafs_ops()) == 0) {
        root_source = root_disk->name;
        root_disk_id = root_disk->disk_id;
        root_read_only = (uint8_t)root_disk->read_only;
    } else {
        vfs_myafs_init(&g_mya_fs, "system");
        (void)vfs_mount("/", "myafs", "myafs", MYAOS_INVALID_DISK_ID, 0, &g_mya_fs, vfs_myafs_ops());
        (void)vfs_write_file("/", "/readme.txt", (const uint8_t*)"welcome to myafs", 16u);
    }
    (void)vfs_mkdir("/", "/bin");
    (void)vfs_mkdir("/", "/cmd");
    (void)vfs_mount("/mya", "myafs", root_source, root_disk_id, root_read_only, &g_mya_fs, vfs_myafs_ops());

    if (boot_disk && vfs_fat32_mount_disk(&g_boot_fs, boot_disk) == 0) {
        (void)vfs_mount("/boot", "fat32", boot_disk->name, boot_disk->disk_id, (uint8_t)boot_disk->read_only, &g_boot_fs, vfs_fat32_ops());
    } else if (root_disk_id == MYAOS_INVALID_DISK_ID && vfs_fat32_mount_boot(&g_boot_fs, boot) == 0) {
        (void)vfs_mount(
            "/boot",
            "fat32",
            g_boot_fs.demo_mode ? "demo" : "disk0",
            g_boot_fs.demo_mode ? MYAOS_INVALID_DISK_ID : 0u,
            0,
            &g_boot_fs,
            vfs_fat32_ops()
        );
    }
    bootstrap_sync_command_manifests();

    vfs_ramfs_init(&g_ram_fs);
    (void)vfs_mount("/ram", "ramfs", "ramfs", MYAOS_INVALID_DISK_ID, 0, &g_ram_fs, vfs_ramfs_ops());
    (void)vfs_write_file("/", "/ram/welcome.txt", (const uint8_t*)"this file lives in ramfs", 24u);

    vfs_ramfs_init(&g_home_fs);
    (void)vfs_mount("/home", "ramfs", "homefs", MYAOS_INVALID_DISK_ID, 0, &g_home_fs, vfs_ramfs_ops());
    (void)vfs_write_file("/", "/home/readme.txt", (const uint8_t*)"home is isolated from system root", 33u);

    vfs_ramfs_init(&g_tmp_fs);
    (void)vfs_mount("/tmp", "ramfs", "tmpfs", MYAOS_INVALID_DISK_ID, 0, &g_tmp_fs, vfs_ramfs_ops());

    vfs_ramfs_init(&g_run_fs);
    (void)vfs_mount("/run", "ramfs", "runfs", MYAOS_INVALID_DISK_ID, 0, &g_run_fs, vfs_ramfs_ops());

    vfs_sysview_init(&g_dvc_view, VFS_SYSVIEW_KIND_DEVICES);
    (void)vfs_mount("/dvc", "sysview", "devices", MYAOS_INVALID_DISK_ID, 1, &g_dvc_view, vfs_sysview_ops());
    (void)vfs_mount("/dev", "sysview", "devices", MYAOS_INVALID_DISK_ID, 1, &g_dvc_view, vfs_sysview_ops());
    vfs_sysview_init(&g_prc_view, VFS_SYSVIEW_KIND_PROCESSES);
    (void)vfs_mount("/prc", "sysview", "processes", MYAOS_INVALID_DISK_ID, 1, &g_prc_view, vfs_sysview_ops());
    (void)vfs_mount("/proc", "sysview", "processes", MYAOS_INVALID_DISK_ID, 1, &g_prc_view, vfs_sysview_ops());
    vfs_sysview_init(&g_cfg_view, VFS_SYSVIEW_KIND_CONFIG);
    (void)vfs_mount("/cfg", "sysview", "config", MYAOS_INVALID_DISK_ID, 1, &g_cfg_view, vfs_sysview_ops());
    (void)vfs_mount("/sys", "sysview", "config", MYAOS_INVALID_DISK_ID, 1, &g_cfg_view, vfs_sysview_ops());
}

static uint8_t kernel_boot_select_recovery(void) {
    uint64_t start_tick = timer_ticks();
    uint64_t fallback_spins = 0ull;

    console_write("\nboot menu: [Enter] normal, [R] recovery (auto in 3s)\n");
    console_write("choice> ");
    for (;;) {
        char c = keyboard_read_char();
        uint64_t now = timer_ticks();

        if (c == 0) {
            if (now > start_tick) {
                if (now - start_tick >= KERNEL_BOOT_MENU_TIMEOUT_TICKS) {
                    break;
                }
            } else {
                fallback_spins++;
                if (fallback_spins >= KERNEL_BOOT_MENU_FALLBACK_SPINS) {
                    break;
                }
            }
        } else {
            if (c == 'r' || c == 'R') {
                console_write("recovery\n");
                return 1u;
            }
            if (c == '\n' || c == '\r' || c == ' ') {
                break;
            }
        }
    }

    console_write("normal\n");
    return 0u;
}

static void kernel_persist_boot_logs(void) {
    static uint8_t prev[KERNEL_BOOT_LOG_MAX];
    static char snap[KERNEL_BOOT_LOG_MAX];
    uint32_t prev_size = 0u;
    uint32_t snap_size = 0u;

    (void)vfs_mkdir("/", "/var");
    (void)vfs_mkdir("/", "/var/log");

    if (vfs_read_file("/", KERNEL_BOOT_CURRENT_LOG, prev, sizeof(prev), &prev_size) == 0) {
        (void)vfs_write_file("/", KERNEL_BOOT_LAST_LOG, prev, prev_size);
    }
    if (klog_snapshot(snap, sizeof(snap), &snap_size) == 0) {
        (void)vfs_write_file("/", KERNEL_BOOT_CURRENT_LOG, (const uint8_t*)snap, snap_size);
    }
}

static int init_step_pmm(boot_info_t* boot) {
    pmm_init(boot, (uint64_t)(uintptr_t)&_kernel_start, (uint64_t)(uintptr_t)&_kernel_end);
    return 0;
}

static int init_step_paging(boot_info_t* boot) {
    return paging_init(boot);
}

static int init_step_heap(boot_info_t* boot) {
    (void)boot;
    heap_init();
    return 0;
}

static int init_step_swap(boot_info_t* boot) {
    (void)boot;
    swap_init(32ull * 1024ull * 1024ull, 64u);
    return 0;
}

static int init_step_gdt(boot_info_t* boot) {
    (void)boot;
    gdt_init();
    return 0;
}

static int init_step_devices(boot_info_t* boot) {
    (void)boot;
    device_init();
    return 0;
}

static int init_step_console(boot_info_t* boot) {
    console_init(boot, 0x00FFFFFFu, 0x00000000u);
    return 0;
}

static int init_step_power(boot_info_t* boot) {
    power_init(boot);
    return 0;
}

static int init_step_keyboard(boot_info_t* boot) {
    (void)boot;
    keyboard_init();
    return 0;
}

static int init_step_blockio(boot_info_t* boot) {
    return blockio_init(boot);
}

static int init_step_network(boot_info_t* boot) {
    (void)boot;
    net_init();
    return 0;
}

static int init_step_modules(boot_info_t* boot) {
    (void)boot;
    module_init();
    module_register_defaults();
    return 0;
}

static int init_step_vfs(boot_info_t* boot) {
    kernel_mount_filesystems(boot);
    return 0;
}

static int init_step_shell(boot_info_t* boot) {
    (void)boot;
    shell_init();
    return 0;
}

static int init_step_syscall(boot_info_t* boot) {
    syscall_set_boot_info(boot);
    return 0;
}

static int init_step_scheduler(boot_info_t* boot) {
    (void)boot;
    scheduler_init(1u);
    return 0;
}

static int init_step_interrupts(boot_info_t* boot) {
    (void)boot;
    interrupts_init();
    return 0;
}

static int init_step_timer(boot_info_t* boot) {
    (void)boot;
    timer_init(KERNEL_TIMER_HZ);
    return 0;
}

static const kernel_init_step_t g_kernel_init_steps[] = {
    { "pmm", init_step_pmm },
    { "paging", init_step_paging },
    { "heap", init_step_heap },
    { "swap", init_step_swap },
    { "gdt", init_step_gdt },
    { "devices", init_step_devices },
    { "console", init_step_console },
    { "power", init_step_power },
    { "keyboard", init_step_keyboard },
    { "blockio", init_step_blockio },
    { "modules", init_step_modules },
    { "network", init_step_network },
    { "vfs", init_step_vfs },
    { "shell", init_step_shell },
    { "syscall", init_step_syscall },
    { "scheduler", init_step_scheduler },
    { "interrupts", init_step_interrupts },
    { "timer", init_step_timer },
};

#define KERNEL_INIT_STEP_COUNT ((uint32_t)(sizeof(g_kernel_init_steps) / sizeof(g_kernel_init_steps[0])))

void kernel_main(boot_info_t* boot) {
    int32_t shell_pid = -1;
    int32_t idle_pid = -1;
    int shell_rc;
    int idle_rc;
    uint8_t recovery_mode = 0u;

    dbg_putc('i');
    if (kernel_init_run(boot, g_kernel_init_steps, KERNEL_INIT_STEP_COUNT) != 0) {
        dbg_putc('X');
        panic_message(kernel_init_failed_step()[0] ? kernel_init_failed_step() : "kernel init failed");
    }
    dbg_putc('j');

    recovery_mode = kernel_boot_select_recovery();
    shell_set_recovery_mode(recovery_mode);
    kernel_persist_boot_logs();

    shell_rc = scheduler_spawn_kernel("shell", shell_main, 0, NULL, 0, &shell_pid);
    idle_rc = scheduler_spawn_kernel("idle", shell_idle_main, 0, NULL, 1, &idle_pid);
    if (shell_rc != 0) {
        dbg_putc('S');
        panic_message("failed to start shell");
    }
    if (idle_rc != 0) {
        dbg_putc('I');
        panic_message("failed to start idle task");
    }
    dbg_putc('k');

    scheduler_run();
}
