#include "vfs_sysview.h"
#include "console.h"
#include "device.h"
#include "heap.h"
#include "init.h"
#include "log.h"
#include "module.h"
#include "net.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "swap.h"
#include "timer.h"
#include <stddef.h>

#define SYSVIEW_MAX_PROCS 64u

static const char* g_config_files[] = {
    "timer_hz",
    "timer_ticks",
    "proc_count",
    "current_pid",
    "pmm_managed_pages",
    "pmm_free_pages",
    "pmm_allocated_pages",
    "heap_page_count",
    "heap_alloc_count",
    "heap_bytes_used",
    "heap_bytes_capacity",
    "paging_mapped_bytes",
    "paging_table_pages",
    "paging_cr3",
    "swap_total_bytes",
    "swap_used_bytes",
    "swap_slots",
    "swap_used_slots",
    "fb_width",
    "fb_height",
    "fb_stride",
    "fb_format",
    "fb_size",
    "abi_version",
    "net_tx_packets",
    "net_rx_packets",
    "net_dropped_packets",
    "net_socket_count",
    "modules_total",
    "modules_loaded",
    "init_steps_total",
    "init_steps_ok",
    "init_last_step",
    "init_failed_step",
    "init_failed_rc",
    "log",
};

#define SYSVIEW_CONFIG_FILE_COUNT ((uint32_t)(sizeof(g_config_files) / sizeof(g_config_files[0])))

typedef struct {
    char* buf;
    size_t max;
    size_t len;
} text_builder_t;

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
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int starts_with(const char* text, const char* prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void u32_to_dec(uint32_t value, char* out, size_t out_size) {
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

static void u64_to_dec(uint64_t value, char* out, size_t out_size) {
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

static int parse_u32(const char* text, uint32_t* out) {
    uint32_t value = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }

    for (size_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
    }

    *out = value;
    return 0;
}

static void tb_init(text_builder_t* tb, char* buf, size_t max) {
    tb->buf = buf;
    tb->max = max;
    tb->len = 0;
    if (tb->max > 0) {
        tb->buf[0] = '\0';
    }
}

static void tb_append(text_builder_t* tb, const char* text) {
    size_t n = str_len(text);

    if (!tb || !tb->buf || tb->max == 0) {
        return;
    }
    if (tb->len + n + 1 > tb->max) {
        n = (tb->len + 1 < tb->max) ? (tb->max - tb->len - 1) : 0;
    }

    for (size_t i = 0; i < n; i++) {
        tb->buf[tb->len++] = text[i];
    }
    tb->buf[tb->len] = '\0';
}

static void tb_append_kv_u32(text_builder_t* tb, const char* key, uint32_t value) {
    char num[16];
    u32_to_dec(value, num, sizeof(num));
    tb_append(tb, key);
    tb_append(tb, "=");
    tb_append(tb, num);
    tb_append(tb, "\n");
}

static void tb_append_kv_u64(text_builder_t* tb, const char* key, uint64_t value) {
    char num[32];
    u64_to_dec(value, num, sizeof(num));
    tb_append(tb, key);
    tb_append(tb, "=");
    tb_append(tb, num);
    tb_append(tb, "\n");
}

static void tb_append_kv_text(text_builder_t* tb, const char* key, const char* value) {
    tb_append(tb, key);
    tb_append(tb, "=");
    tb_append(tb, value ? value : "");
    tb_append(tb, "\n");
}

static int emit_text(uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size, const char* text) {
    size_t len = str_len(text);

    if (!out_buf || !out_size) {
        return -1;
    }
    if ((uint64_t)len > out_buf_size) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        out_buf[i] = (uint8_t)text[i];
    }
    *out_size = (uint32_t)len;
    return 0;
}

static int emit_u64(uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size, uint64_t value) {
    char n[32];

    u64_to_dec(value, n, sizeof(n));
    return emit_text(out_buf, out_buf_size, out_size, n);
}

static int sysview_list(void* ctx, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count) {
    vfs_sysview_t* view = (vfs_sysview_t*)ctx;
    size_t count = 0;

    if (!view || !path || !out || !out_count) {
        return -1;
    }
    if (!str_eq(path, "/")) {
        return -1;
    }

    if (view->kind == VFS_SYSVIEW_KIND_DEVICES) {
        uint32_t dev_count = device_count();
        if (count < max_entries) {
            str_copy(out[count].name, "count", sizeof(out[count].name));
            out[count].type = MYAOS_NODE_FILE;
            out[count].size = dev_count;
            count++;
        }
        for (uint32_t i = 0; i < dev_count && count < max_entries; i++) {
            char num[16];
            char name[MYAOS_NAME_MAX];
            u32_to_dec(i, num, sizeof(num));
            str_copy(name, "dev", sizeof(name));
            {
                size_t base = str_len(name);
                for (size_t j = 0; num[j] && base + j + 1 < sizeof(name); j++) {
                    name[base + j] = num[j];
                    name[base + j + 1] = '\0';
                }
            }
            str_copy(out[count].name, name, sizeof(out[count].name));
            out[count].type = MYAOS_NODE_FILE;
            out[count].size = 0;
            count++;
        }
    } else if (view->kind == VFS_SYSVIEW_KIND_PROCESSES) {
        myaos_proc_info_t procs[SYSVIEW_MAX_PROCS];
        uint32_t proc_count = 0;

        (void)scheduler_list_processes(procs, SYSVIEW_MAX_PROCS, &proc_count);
        if (count < max_entries) {
            str_copy(out[count].name, "count", sizeof(out[count].name));
            out[count].type = MYAOS_NODE_FILE;
            out[count].size = proc_count;
            count++;
        }
        if (count < max_entries) {
            str_copy(out[count].name, "self", sizeof(out[count].name));
            out[count].type = MYAOS_NODE_FILE;
            out[count].size = 0;
            count++;
        }

        for (uint32_t i = 0; i < proc_count && count < max_entries; i++) {
            char num[16];
            char name[MYAOS_NAME_MAX];
            u32_to_dec(procs[i].pid, num, sizeof(num));
            str_copy(name, "pid", sizeof(name));
            {
                size_t base = str_len(name);
                for (size_t j = 0; num[j] && base + j + 1 < sizeof(name); j++) {
                    name[base + j] = num[j];
                    name[base + j + 1] = '\0';
                }
            }
            str_copy(out[count].name, name, sizeof(out[count].name));
            out[count].type = MYAOS_NODE_FILE;
            out[count].size = 0;
            count++;
        }
    } else if (view->kind == VFS_SYSVIEW_KIND_CONFIG) {
        if (count < max_entries) {
            str_copy(out[count].name, "count", sizeof(out[count].name));
            out[count].type = MYAOS_NODE_FILE;
            out[count].size = SYSVIEW_CONFIG_FILE_COUNT;
            count++;
        }

        for (uint32_t i = 0; i < SYSVIEW_CONFIG_FILE_COUNT && count < max_entries; i++) {
            str_copy(out[count].name, g_config_files[i], sizeof(out[count].name));
            out[count].type = MYAOS_NODE_FILE;
            out[count].size = 0;
            count++;
        }
    } else {
        return -1;
    }

    *out_count = count;
    return 0;
}

static int read_device_file(const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    char text[256];
    text_builder_t tb;

    if (str_eq(path, "/count")) {
        char n[16];
        u32_to_dec(device_count(), n, sizeof(n));
        return emit_text(out_buf, out_buf_size, out_size, n);
    }

    if (starts_with(path, "/dev")) {
        uint32_t id = 0;
        const device_t* dev;

        if (parse_u32(path + 4, &id) != 0) {
            return -1;
        }
        dev = device_get(id);
        if (!dev) {
            return -1;
        }

        tb_init(&tb, text, sizeof(text));
        tb_append_kv_u32(&tb, "id", dev->id);
        tb_append_kv_u32(&tb, "class", dev->class_id);
        tb_append_kv_text(&tb, "name", dev->name);
        tb_append_kv_text(&tb, "driver", dev->driver);
        return emit_text(out_buf, out_buf_size, out_size, text);
    }

    return -1;
}

static int read_proc_file(const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    myaos_proc_info_t procs[SYSVIEW_MAX_PROCS];
    uint32_t proc_count = 0;
    char text[320];
    text_builder_t tb;

    if (scheduler_list_processes(procs, SYSVIEW_MAX_PROCS, &proc_count) != 0) {
        return -1;
    }

    if (str_eq(path, "/count")) {
        char n[16];
        u32_to_dec(proc_count, n, sizeof(n));
        return emit_text(out_buf, out_buf_size, out_size, n);
    }

    if (str_eq(path, "/self")) {
        int32_t self_pid = scheduler_current_pid();
        for (uint32_t i = 0; i < proc_count; i++) {
            if ((int32_t)procs[i].pid == self_pid) {
                tb_init(&tb, text, sizeof(text));
                tb_append_kv_u32(&tb, "pid", procs[i].pid);
                tb_append_kv_u32(&tb, "ppid", procs[i].ppid);
                tb_append_kv_text(&tb, "name", procs[i].name);
                tb_append_kv_u32(&tb, "state", procs[i].state);
                tb_append_kv_u32(&tb, "bg", procs[i].background);
                tb_append_kv_u64(&tb, "run_count", procs[i].run_count);
                return emit_text(out_buf, out_buf_size, out_size, text);
            }
        }
        return -1;
    }

    if (starts_with(path, "/pid")) {
        uint32_t pid = 0;

        if (parse_u32(path + 4, &pid) != 0) {
            return -1;
        }

        for (uint32_t i = 0; i < proc_count; i++) {
            if (procs[i].pid != pid) {
                continue;
            }

            tb_init(&tb, text, sizeof(text));
            tb_append_kv_u32(&tb, "pid", procs[i].pid);
            tb_append_kv_u32(&tb, "ppid", procs[i].ppid);
            tb_append_kv_text(&tb, "name", procs[i].name);
            tb_append_kv_u32(&tb, "state", procs[i].state);
            tb_append_kv_u32(&tb, "bg", procs[i].background);
            tb_append_kv_u32(&tb, "exit", (uint32_t)procs[i].exit_code);
            tb_append_kv_u64(&tb, "wake_tick", procs[i].wake_tick);
            tb_append_kv_u64(&tb, "run_count", procs[i].run_count);
            tb_append_kv_u64(&tb, "last_run_tick", procs[i].last_run_tick);
            return emit_text(out_buf, out_buf_size, out_size, text);
        }
    }

    return -1;
}

static int read_config_file(const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    pmm_stats_t pmm;
    heap_stats_t heap;
    paging_stats_t paging;
    myaos_swap_info_t swap;
    myaos_netinfo_t net;
    boot_info_t* boot = console_boot_info();

    if (str_eq(path, "/count")) {
        return emit_u64(out_buf, out_buf_size, out_size, SYSVIEW_CONFIG_FILE_COUNT);
    }

    pmm_get_stats(&pmm);
    heap_get_stats(&heap);
    paging_get_stats(&paging);
    swap_get_info(&swap);
    (void)net_info(&net);

    if (str_eq(path, "/timer_hz")) {
        return emit_u64(out_buf, out_buf_size, out_size, timer_hz());
    }
    if (str_eq(path, "/timer_ticks")) {
        return emit_u64(out_buf, out_buf_size, out_size, timer_ticks());
    }
    if (str_eq(path, "/proc_count")) {
        return emit_u64(out_buf, out_buf_size, out_size, scheduler_process_count());
    }
    if (str_eq(path, "/current_pid")) {
        return emit_u64(out_buf, out_buf_size, out_size, (uint32_t)scheduler_current_pid());
    }
    if (str_eq(path, "/pmm_managed_pages")) {
        return emit_u64(out_buf, out_buf_size, out_size, pmm.managed_pages);
    }
    if (str_eq(path, "/pmm_free_pages")) {
        return emit_u64(out_buf, out_buf_size, out_size, pmm.free_pages);
    }
    if (str_eq(path, "/pmm_allocated_pages")) {
        return emit_u64(out_buf, out_buf_size, out_size, pmm.allocated_pages);
    }
    if (str_eq(path, "/heap_page_count")) {
        return emit_u64(out_buf, out_buf_size, out_size, heap.page_count);
    }
    if (str_eq(path, "/heap_alloc_count")) {
        return emit_u64(out_buf, out_buf_size, out_size, heap.alloc_count);
    }
    if (str_eq(path, "/heap_bytes_used")) {
        return emit_u64(out_buf, out_buf_size, out_size, heap.bytes_used);
    }
    if (str_eq(path, "/heap_bytes_capacity")) {
        return emit_u64(out_buf, out_buf_size, out_size, heap.bytes_capacity);
    }
    if (str_eq(path, "/paging_mapped_bytes")) {
        return emit_u64(out_buf, out_buf_size, out_size, paging.mapped_bytes);
    }
    if (str_eq(path, "/paging_table_pages")) {
        return emit_u64(out_buf, out_buf_size, out_size, paging.table_pages);
    }
    if (str_eq(path, "/paging_cr3")) {
        return emit_u64(out_buf, out_buf_size, out_size, paging.cr3);
    }
    if (str_eq(path, "/swap_total_bytes")) {
        return emit_u64(out_buf, out_buf_size, out_size, swap.total_bytes);
    }
    if (str_eq(path, "/swap_used_bytes")) {
        return emit_u64(out_buf, out_buf_size, out_size, swap.used_bytes);
    }
    if (str_eq(path, "/swap_slots")) {
        return emit_u64(out_buf, out_buf_size, out_size, swap.slot_count);
    }
    if (str_eq(path, "/swap_used_slots")) {
        return emit_u64(out_buf, out_buf_size, out_size, swap.used_slots);
    }
    if (str_eq(path, "/fb_width")) {
        return emit_u64(out_buf, out_buf_size, out_size, boot ? boot->fb.width : 0u);
    }
    if (str_eq(path, "/fb_height")) {
        return emit_u64(out_buf, out_buf_size, out_size, boot ? boot->fb.height : 0u);
    }
    if (str_eq(path, "/fb_stride")) {
        return emit_u64(out_buf, out_buf_size, out_size, boot ? boot->fb.pixels_per_scanline : 0u);
    }
    if (str_eq(path, "/fb_format")) {
        return emit_u64(out_buf, out_buf_size, out_size, boot ? boot->fb.format : 0u);
    }
    if (str_eq(path, "/fb_size")) {
        return emit_u64(out_buf, out_buf_size, out_size, boot ? boot->fb.size : 0u);
    }
    if (str_eq(path, "/abi_version")) {
        return emit_u64(out_buf, out_buf_size, out_size, MYAOS_ABI_VERSION);
    }
    if (str_eq(path, "/net_tx_packets")) {
        return emit_u64(out_buf, out_buf_size, out_size, net.tx_packets);
    }
    if (str_eq(path, "/net_rx_packets")) {
        return emit_u64(out_buf, out_buf_size, out_size, net.rx_packets);
    }
    if (str_eq(path, "/net_dropped_packets")) {
        return emit_u64(out_buf, out_buf_size, out_size, net.dropped_packets);
    }
    if (str_eq(path, "/net_socket_count")) {
        return emit_u64(out_buf, out_buf_size, out_size, net.socket_count);
    }
    if (str_eq(path, "/modules_total")) {
        return emit_u64(out_buf, out_buf_size, out_size, module_count());
    }
    if (str_eq(path, "/modules_loaded")) {
        return emit_u64(out_buf, out_buf_size, out_size, module_loaded_count());
    }
    if (str_eq(path, "/init_steps_total")) {
        return emit_u64(out_buf, out_buf_size, out_size, kernel_init_step_count());
    }
    if (str_eq(path, "/init_steps_ok")) {
        return emit_u64(out_buf, out_buf_size, out_size, kernel_init_ok_count());
    }
    if (str_eq(path, "/init_last_step")) {
        return emit_text(out_buf, out_buf_size, out_size, kernel_init_last_step());
    }
    if (str_eq(path, "/init_failed_step")) {
        return emit_text(out_buf, out_buf_size, out_size, kernel_init_failed_step());
    }
    if (str_eq(path, "/init_failed_rc")) {
        return emit_u64(out_buf, out_buf_size, out_size, (uint64_t)(int64_t)kernel_init_failed_rc());
    }
    if (str_eq(path, "/log")) {
        return klog_snapshot((char*)out_buf, out_buf_size, out_size);
    }

    return -1;
}

static int sysview_read_file(void* ctx, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    vfs_sysview_t* view = (vfs_sysview_t*)ctx;

    if (!view || !path) {
        return -1;
    }

    if (view->kind == VFS_SYSVIEW_KIND_DEVICES) {
        return read_device_file(path, out_buf, out_buf_size, out_size);
    }
    if (view->kind == VFS_SYSVIEW_KIND_PROCESSES) {
        return read_proc_file(path, out_buf, out_buf_size, out_size);
    }
    if (view->kind == VFS_SYSVIEW_KIND_CONFIG) {
        return read_config_file(path, out_buf, out_buf_size, out_size);
    }

    return -1;
}

static int sysview_is_dir(void* ctx, const char* path) {
    (void)ctx;
    return str_eq(path, "/") ? 1 : 0;
}

static int sysview_sync(void* ctx) {
    (void)ctx;
    return 0;
}

static const vfs_ops_t g_sysview_ops = {
    .list = sysview_list,
    .read_file = sysview_read_file,
    .write_file = NULL,
    .mkdir = NULL,
    .touch = NULL,
    .remove = NULL,
    .is_dir = sysview_is_dir,
    .sync = sysview_sync,
};

void vfs_sysview_init(vfs_sysview_t* view, uint8_t kind) {
    if (!view) {
        return;
    }
    view->kind = kind;
}

const vfs_ops_t* vfs_sysview_ops(void) {
    return &g_sysview_ops;
}
