#include "shell.h"
#include "blockio.h"
#include "fat32.h"
#include "graphics.h"
#include "heap.h"
#include "keyboard.h"
#include "paging.h"
#include "pmm.h"
#include "power.h"
#include "ramfs.h"
#include "scheduler.h"
#include "syscall.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

#define SHELL_INPUT_MAX 256
#define SHELL_CMD_MAX 32
#define SHELL_TOKEN_MAX 64
#define SHELL_FATCAT_MAX 1024
#define SHELL_CMD_FILE_MAX 256
#define FAT_PATH_DEPTH_MAX 16
#define SHELL_CMD_BIN_VERSION 2u
#define SHELL_CMD_DESC_MAX 48u

typedef struct {
    boot_info_t* boot;
    uint32_t fg;
    uint32_t bg;
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_col;
    uint32_t cursor_row;
} shell_state_t;

typedef struct {
    const char* name;
    uint32_t api_addr;
    uint32_t op_code;
    const char* desc;
} shell_command_manifest_t;

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint16_t version;
    uint16_t flags;
    uint32_t api_addr;
    uint32_t op_code;
    char short_desc[SHELL_CMD_DESC_MAX];
} shell_command_binary_t;

static const shell_command_manifest_t g_command_manifest[] = {
    { "clear", SHELL_API_CONSOLE, SHELL_OP_CONSOLE_CLEAR, "clear screen" },
    { "about", SHELL_API_SYSTEM, SHELL_OP_SYSTEM_ABOUT, "show system info" },
    { "echo", SHELL_API_SYSTEM, SHELL_OP_SYSTEM_ECHO, "print text" },
    { "halt", SHELL_API_POWER, SHELL_OP_POWER_HALT, "stop CPU" },
    { "reboot", SHELL_API_POWER, SHELL_OP_POWER_REBOOT, "restart machine" },
    { "shutdown", SHELL_API_POWER, SHELL_OP_POWER_SHUTDOWN, "power off machine" },
    { "fatinfo", SHELL_API_FAT, SHELL_OP_FAT_INFO, "show FAT32 info" },
    { "fatpwd", SHELL_API_FAT, SHELL_OP_FAT_PWD, "show FAT32 path" },
    { "fatls", SHELL_API_FAT, SHELL_OP_FAT_LS, "list FAT32 directory" },
    { "fatcd", SHELL_API_FAT, SHELL_OP_FAT_CD, "change FAT32 directory" },
    { "fatcat", SHELL_API_FAT, SHELL_OP_FAT_CAT, "read FAT32 file" },
    { "fatmkdir", SHELL_API_FAT, SHELL_OP_FAT_MKDIR, "create FAT32 directory" },
    { "fattouch", SHELL_API_FAT, SHELL_OP_FAT_TOUCH, "create FAT32 file" },
    { "fatwrite", SHELL_API_FAT, SHELL_OP_FAT_WRITE, "write FAT32 file" },
    { "fatflush", SHELL_API_FAT, SHELL_OP_FAT_FLUSH, "flush FAT32 writeback" },
    { "ramls", SHELL_API_RAM, SHELL_OP_RAM_LS, "list RAMFS files" },
    { "ramwrite", SHELL_API_RAM, SHELL_OP_RAM_WRITE, "write RAMFS file" },
    { "ramcat", SHELL_API_RAM, SHELL_OP_RAM_CAT, "read RAMFS file" },
    { "ramrm", SHELL_API_RAM, SHELL_OP_RAM_RM, "remove RAMFS file" },
    { "ramclear", SHELL_API_RAM, SHELL_OP_RAM_CLEAR, "clear RAMFS" },
    { "meminfo", SHELL_API_STATS, SHELL_OP_STATS_MEMINFO, "show memory subsystem info" },
    { "sched", SHELL_API_STATS, SHELL_OP_STATS_SCHEDINFO, "show timer and scheduler stats" },
};

static shell_state_t g_shell;
static fat32_fs_t g_fat32;
static ramfs_t g_ramfs;
static uint8_t g_fat_demo_mode;
static uint8_t g_fat_ready;
static uint8_t g_fat_dirty;
static uint32_t g_fat_path_clusters[FAT_PATH_DEPTH_MAX];
static char g_fat_path_names[FAT_PATH_DEPTH_MAX][FAT32_NAME_MAX];
static uint32_t g_fat_path_depth;
static uint8_t g_cmd_dir_ready;
static uint32_t g_cmd_dir_cluster;
static char g_input_buffer[SHELL_INPUT_MAX];
static size_t g_input_len;
static uint8_t g_prompt_shown;

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void str_copy(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0) {
        return;
    }

    size_t i = 0;
    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

static void fill_command_binary(
    shell_command_binary_t* out,
    uint32_t api_addr,
    uint32_t op_code,
    const char* desc
) {
    uint8_t* bytes = (uint8_t*)(void*)out;
    for (size_t i = 0; i < sizeof(*out); i++) {
        bytes[i] = 0;
    }

    out->magic[0] = 'C';
    out->magic[1] = 'M';
    out->magic[2] = 'D';
    out->magic[3] = '2';
    out->version = SHELL_CMD_BIN_VERSION;
    out->flags = 0;
    out->api_addr = api_addr;
    out->op_code = op_code;
    str_copy(out->short_desc, desc ? desc : "", sizeof(out->short_desc));
}

static int command_binary_is_valid(const shell_command_binary_t* bin) {
    if (!bin) {
        return 0;
    }

    if (bin->magic[0] != 'C' || bin->magic[1] != 'M' || bin->magic[2] != 'D' || bin->magic[3] != '2') {
        return 0;
    }
    if (bin->version != SHELL_CMD_BIN_VERSION) {
        return 0;
    }
    if (bin->api_addr == 0) {
        return 0;
    }
    if (bin->op_code == 0) {
        return 0;
    }
    return 1;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void trim_in_place(char* text) {
    size_t begin = 0;
    while (text[begin] && is_space(text[begin])) {
        begin++;
    }

    size_t end = str_len(text);
    while (end > begin && is_space(text[end - 1])) {
        end--;
    }

    size_t out = 0;
    for (size_t i = begin; i < end; i++) {
        text[out++] = text[i];
    }
    text[out] = '\0';
}

static void to_lower_in_place(char* text) {
    for (size_t i = 0; text[i]; i++) {
        if (text[i] >= 'A' && text[i] <= 'Z') {
            text[i] = (char)(text[i] - 'A' + 'a');
        }
    }
}

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static const char* skip_spaces(const char* text) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

static void parse_command(const char* line, char* cmd, size_t cmd_size, const char** args_out) {
    const char* p = skip_spaces(line);
    size_t i = 0;

    while (p[i] && !is_space(p[i])) {
        if (i + 1 < cmd_size) {
            cmd[i] = p[i];
        }
        i++;
    }

    if (cmd_size > 0) {
        size_t end = (i < cmd_size - 1) ? i : (cmd_size - 1);
        cmd[end] = '\0';
    }

    p += i;
    *args_out = skip_spaces(p);
}

static int parse_first_token(const char* text, char* token, size_t token_size, const char** rest_out) {
    const char* p = skip_spaces(text);
    if (*p == '\0') {
        if (token_size > 0) {
            token[0] = '\0';
        }
        *rest_out = p;
        return 0;
    }

    size_t i = 0;
    while (p[i] && !is_space(p[i])) {
        if (i + 1 < token_size) {
            token[i] = p[i];
        }
        i++;
    }

    if (token_size > 0) {
        size_t end = (i < token_size - 1) ? i : (token_size - 1);
        token[end] = '\0';
    }

    *rest_out = skip_spaces(p + i);
    return 1;
}

static void u32_to_dec(uint32_t value, char* out, size_t out_size) {
    if (out_size == 0) {
        return;
    }

    char rev[16];
    size_t n = 0;
    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    size_t pos = 0;
    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static void u64_to_dec(uint64_t value, char* out, size_t out_size) {
    if (out_size == 0) {
        return;
    }

    char rev[32];
    size_t n = 0;
    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    size_t pos = 0;
    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static void console_scroll(shell_state_t* shell) {
    framebuffer_t* fb = &shell->boot->fb;
    volatile uint32_t* pixels = (volatile uint32_t*)(uintptr_t)fb->base;

    if (fb->height <= FONT_HEIGHT) {
        clear_screen(shell->boot, shell->bg);
        shell->cursor_col = 0;
        shell->cursor_row = 0;
        return;
    }

    uint32_t stride = fb->pixels_per_scanline;

    for (uint32_t y = FONT_HEIGHT; y < fb->height; y++) {
        uint32_t dst_off = (y - FONT_HEIGHT) * stride;
        uint32_t src_off = y * stride;

        for (uint32_t x = 0; x < stride; x++) {
            pixels[dst_off + x] = pixels[src_off + x];
        }
    }

    for (uint32_t y = fb->height - FONT_HEIGHT; y < fb->height; y++) {
        uint32_t row_off = y * stride;
        for (uint32_t x = 0; x < stride; x++) {
            pixels[row_off + x] = shell->bg;
        }
    }

    if (shell->cursor_row > 0) {
        shell->cursor_row--;
    }
}

static void console_newline(shell_state_t* shell) {
    shell->cursor_col = 0;
    shell->cursor_row++;

    if (shell->cursor_row >= shell->rows) {
        console_scroll(shell);
        shell->cursor_row = shell->rows - 1;
    }
}

static void console_put_char(shell_state_t* shell, char c) {
    if (c == '\n') {
        console_newline(shell);
        return;
    }

    if (c == '\b') {
        if (shell->cursor_col > 0) {
            shell->cursor_col--;
        } else if (shell->cursor_row > 0) {
            shell->cursor_row--;
            shell->cursor_col = shell->cols - 1;
        } else {
            return;
        }

        draw_char(
            shell->boot,
            shell->cursor_col * FONT_WIDTH,
            shell->cursor_row * FONT_HEIGHT,
            ' ',
            shell->fg,
            shell->bg
        );
        return;
    }

    if (c < 32 || c > 126) {
        return;
    }

    draw_char(
        shell->boot,
        shell->cursor_col * FONT_WIDTH,
        shell->cursor_row * FONT_HEIGHT,
        c,
        shell->fg,
        shell->bg
    );

    shell->cursor_col++;
    if (shell->cursor_col >= shell->cols) {
        console_newline(shell);
    }
}

static void console_write(shell_state_t* shell, const char* text) {
    for (size_t i = 0; text[i]; i++) {
        console_put_char(shell, text[i]);
    }
}

static void console_write_u32(shell_state_t* shell, uint32_t value) {
    char buf[16];
    u32_to_dec(value, buf, sizeof(buf));
    console_write(shell, buf);
}

static void console_write_u64(shell_state_t* shell, uint64_t value) {
    char buf[32];
    u64_to_dec(value, buf, sizeof(buf));
    console_write(shell, buf);
}

static void console_write_binary_as_text(shell_state_t* shell, const uint8_t* data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c <= 126)) {
            console_put_char(shell, c);
        } else {
            console_put_char(shell, '.');
        }
    }
}

static void console_reset(shell_state_t* shell) {
    clear_screen(shell->boot, shell->bg);
    shell->cursor_col = 0;
    shell->cursor_row = 0;
}

static uint32_t fat_current_cluster(void) {
    if (!g_fat_ready) {
        return 0;
    }
    return g_fat_path_clusters[g_fat_path_depth];
}

static void fat_path_reset(void) {
    g_fat_path_depth = 0;
    g_fat_path_clusters[0] = fat32_root_cluster(&g_fat32);
    g_fat_path_names[0][0] = '\0';
    g_fat_ready = 1;
}

static int build_cmd_filename(const char* cmd, char* out, size_t out_size) {
    if (!cmd || !out || out_size < 5) {
        return -1;
    }

    size_t n = str_len(cmd);
    if (n == 0 || n > 8 || n + 5 > out_size) {
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        out[i] = to_upper_ascii(cmd[i]);
    }
    out[n++] = '.';
    out[n++] = 'C';
    out[n++] = 'M';
    out[n++] = 'D';
    out[n] = '\0';
    return 0;
}

static int ensure_command_directory(void) {
    if (!g_fat_ready) {
        return -1;
    }

    uint32_t root = fat32_root_cluster(&g_fat32);
    fat32_dirent_t dir;
    int rc = fat32_lookup(&g_fat32, root, "CMD", &dir);
    if (rc != 0) {
        rc = fat32_mkdir(&g_fat32, root, "CMD");
        if (rc != 0 && rc != -2) {
            return -1;
        }
        g_fat_dirty = 1;
        rc = fat32_lookup(&g_fat32, root, "CMD", &dir);
    }
    if (rc != 0 || !dir.is_dir) {
        return -1;
    }

    g_cmd_dir_cluster = dir.first_cluster;
    g_cmd_dir_ready = 1;
    return 0;
}

static void ensure_command_binary_files(void) {
    if (ensure_command_directory() != 0) {
        return;
    }

    for (size_t i = 0; i < sizeof(g_command_manifest) / sizeof(g_command_manifest[0]); i++) {
        char filename[FAT32_NAME_MAX];
        if (build_cmd_filename(g_command_manifest[i].name, filename, sizeof(filename)) != 0) {
            continue;
        }

        shell_command_binary_t payload;
        fill_command_binary(
            &payload,
            g_command_manifest[i].api_addr,
            g_command_manifest[i].op_code,
            g_command_manifest[i].desc
        );

        int should_write = 1;
        fat32_dirent_t entry;
        if (fat32_lookup(&g_fat32, g_cmd_dir_cluster, filename, &entry) == 0 && !entry.is_dir) {
            uint8_t raw[sizeof(shell_command_binary_t)];
            uint32_t read_size = 0;
            if (fat32_read_file(
                    &g_fat32,
                    g_cmd_dir_cluster,
                    filename,
                    raw,
                    (uint32_t)sizeof(raw),
                    &read_size
                ) == 0 && read_size == sizeof(raw)) {
                const shell_command_binary_t* old = (const shell_command_binary_t*)(const void*)raw;
                if (command_binary_is_valid(old)) {
                    should_write = 0;
                    const uint8_t* old_bytes = (const uint8_t*)(const void*)old;
                    const uint8_t* new_bytes = (const uint8_t*)(const void*)&payload;
                    for (size_t j = 0; j < sizeof(payload); j++) {
                        if (old_bytes[j] != new_bytes[j]) {
                            should_write = 1;
                            break;
                        }
                    }
                }
            }
        }

        if (!should_write) {
            continue;
        }

        if (fat32_write_file(
                &g_fat32,
                g_cmd_dir_cluster,
                filename,
                (const uint8_t*)&payload,
                (uint32_t)sizeof(payload)
            ) == 0) {
            g_fat_dirty = 1;
        }
    }
}

static int resolve_command_from_disk(const char* cmd, uint32_t* out_api_addr, uint32_t* out_op_code) {
    if (!g_cmd_dir_ready) {
        return -1;
    }

    char filename[FAT32_NAME_MAX];
    if (build_cmd_filename(cmd, filename, sizeof(filename)) != 0) {
        return -1;
    }

    uint8_t raw[sizeof(shell_command_binary_t)];
    uint32_t read_size = 0;
    int rc = fat32_read_file(
        &g_fat32,
        g_cmd_dir_cluster,
        filename,
        raw,
        (uint32_t)sizeof(raw),
        &read_size
    );
    if (rc != 0 || read_size != sizeof(raw)) {
        return -1;
    }

    const shell_command_binary_t* bin = (const shell_command_binary_t*)(const void*)raw;
    if (!command_binary_is_valid(bin) || out_api_addr == NULL || out_op_code == NULL) {
        return -1;
    }

    *out_api_addr = bin->api_addr;
    *out_op_code = bin->op_code;
    return 0;
}

static void cmd_help(shell_state_t* shell) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }
    if (ensure_command_directory() != 0) {
        console_write(shell, "cmd directory unavailable\n");
        return;
    }

    fat32_dirent_t entries[64];
    size_t count = 0;
    int rc = fat32_list_dir(&g_fat32, g_cmd_dir_cluster, entries, 64, &count);
    if (rc == -1) {
        console_write(shell, "cmd directory read failed\n");
        return;
    }

    console_write(shell, "help - commands from /CMD\n");
    if (count == 0) {
        console_write(shell, "(no command files)\n");
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (entries[i].is_dir) {
            continue;
        }

        char name[FAT32_NAME_MAX];
        str_copy(name, entries[i].name, sizeof(name));
        to_lower_in_place(name);
        size_t n = str_len(name);
        if (n > 4 && name[n - 4] == '.' && name[n - 3] == 'c' && name[n - 2] == 'm' && name[n - 1] == 'd') {
            name[n - 4] = '\0';
        }

        char desc[SHELL_CMD_DESC_MAX + 1];
        desc[0] = '\0';

        uint8_t raw[sizeof(shell_command_binary_t)];
        uint32_t read_size = 0;
        if (fat32_read_file(
                &g_fat32,
                g_cmd_dir_cluster,
                entries[i].name,
                raw,
                (uint32_t)sizeof(raw),
                &read_size
            ) == 0 && read_size == sizeof(raw)) {
            const shell_command_binary_t* bin = (const shell_command_binary_t*)(const void*)raw;
            if (command_binary_is_valid(bin)) {
                str_copy(desc, bin->short_desc, sizeof(desc));
            }
        }

        if (desc[0] == '\0') {
            str_copy(desc, "(invalid command file)", sizeof(desc));
        }

        console_write(shell, name);
        console_write(shell, " - ");
        console_write(shell, desc);
        console_put_char(shell, '\n');
    }

    if (rc == -2) {
        console_write(shell, "output truncated\n");
    }
}

static void fat_flush_if_needed(shell_state_t* shell) {
    if (!g_fat_ready || g_fat_demo_mode || !g_fat_dirty) {
        return;
    }

    if (blockio_writeback(shell->boot) == 0) {
        g_fat_dirty = 0;
    }
}

#include "../programs/clear.c"
#include "../programs/about.c"
#include "../programs/echo.c"
#include "../programs/halt.c"
#include "../programs/reboot.c"
#include "../programs/shutdown.c"
#include "../programs/fatinfo.c"
#include "../programs/fatpwd.c"
#include "../programs/fatls.c"
#include "../programs/fatcd.c"
#include "../programs/fatcat.c"
#include "../programs/fatmkdir.c"
#include "../programs/fattouch.c"
#include "../programs/fatwrite.c"
#include "../programs/fatflush.c"
#include "../programs/ramls.c"
#include "../programs/ramwrite.c"
#include "../programs/ramcat.c"
#include "../programs/ramrm.c"
#include "../programs/ramclear.c"
#include "../programs/meminfo.c"
#include "../programs/sched.c"

int shell_syscall_run_api(uint32_t api_addr, uint32_t op_code, const char* args) {
    switch (api_addr) {
    case SHELL_API_CONSOLE:
        switch (op_code) {
        case SHELL_OP_CONSOLE_CLEAR:
            return program_clear_run(args);
        default:
            return -1;
        }
    case SHELL_API_SYSTEM:
        switch (op_code) {
        case SHELL_OP_SYSTEM_ABOUT:
            return program_about_run(args);
        case SHELL_OP_SYSTEM_ECHO:
            return program_echo_run(args);
        default:
            return -1;
        }
    case SHELL_API_POWER:
        switch (op_code) {
        case SHELL_OP_POWER_HALT:
            return program_halt_run(args);
        case SHELL_OP_POWER_REBOOT:
            return program_reboot_run(args);
        case SHELL_OP_POWER_SHUTDOWN:
            return program_shutdown_run(args);
        default:
            return -1;
        }
    case SHELL_API_FAT:
        switch (op_code) {
        case SHELL_OP_FAT_INFO:
            return program_fatinfo_run(args);
        case SHELL_OP_FAT_PWD:
            return program_fatpwd_run(args);
        case SHELL_OP_FAT_LS:
            return program_fatls_run(args);
        case SHELL_OP_FAT_CD:
            return program_fatcd_run(args);
        case SHELL_OP_FAT_CAT:
            return program_fatcat_run(args);
        case SHELL_OP_FAT_MKDIR:
            return program_fatmkdir_run(args);
        case SHELL_OP_FAT_TOUCH:
            return program_fattouch_run(args);
        case SHELL_OP_FAT_WRITE:
            return program_fatwrite_run(args);
        case SHELL_OP_FAT_FLUSH:
            return program_fatflush_run(args);
        default:
            return -1;
        }
    case SHELL_API_RAM:
        switch (op_code) {
        case SHELL_OP_RAM_LS:
            return program_ramls_run(args);
        case SHELL_OP_RAM_WRITE:
            return program_ramwrite_run(args);
        case SHELL_OP_RAM_CAT:
            return program_ramcat_run(args);
        case SHELL_OP_RAM_RM:
            return program_ramrm_run(args);
        case SHELL_OP_RAM_CLEAR:
            return program_ramclear_run(args);
        default:
            return -1;
        }
    case SHELL_API_STATS:
        switch (op_code) {
        case SHELL_OP_STATS_MEMINFO:
            return program_meminfo_run(args);
        case SHELL_OP_STATS_SCHEDINFO:
            return program_sched_run(args);
        default:
            return -1;
        }
    default:
        return -1;
    }
}

static void shell_execute(shell_state_t* shell, const char* input_raw) {
    char line[SHELL_INPUT_MAX];
    str_copy(line, input_raw, sizeof(line));
    trim_in_place(line);
    if (line[0] == '\0') {
        return;
    }

    char cmd[SHELL_CMD_MAX];
    const char* args = NULL;
    parse_command(line, cmd, sizeof(cmd), &args);
    to_lower_in_place(cmd);

    if (str_eq(cmd, "help")) {
        cmd_help(shell);
        return;
    }

    uint32_t api_addr = 0;
    uint32_t op_code = 0;
    if (resolve_command_from_disk(cmd, &api_addr, &op_code) != 0) {
        console_write(shell, "unknown command (no binary)\n");
        return;
    }

    if (sys_run_api(api_addr, op_code, args) != 0) {
        console_write(shell, "command execution failed\n");
    }
}

void shell_init(boot_info_t* boot) {
    g_shell.boot = boot;
    g_shell.fg = 0x00FFFFFFu;
    g_shell.bg = 0x00000000u;

    g_shell.cols = boot->fb.width / FONT_WIDTH;
    g_shell.rows = boot->fb.height / FONT_HEIGHT;
    if (g_shell.cols == 0) {
        g_shell.cols = 1;
    }
    if (g_shell.rows == 0) {
        g_shell.rows = 1;
    }

    ramfs_init(&g_ramfs);
    (void)ramfs_write(&g_ramfs, "welcome.txt", "this file lives in ramfs");

    int fat_mount_rc = -1;
    g_fat_demo_mode = 0;
    g_fat_ready = 0;
    g_cmd_dir_ready = 0;
    g_cmd_dir_cluster = 0;
    if (boot->boot_disk_base != 0 && boot->boot_disk_size >= 512) {
        fat_mount_rc = fat32_mount(
            &g_fat32,
            (uint8_t*)(uintptr_t)boot->boot_disk_base,
            boot->boot_disk_size
        );
    }
    if (fat_mount_rc != 0) {
        fat_mount_rc = fat32_mount_demo(&g_fat32);
        if (fat_mount_rc == 0) {
            g_fat_demo_mode = 1;
        }
    }
    if (fat_mount_rc == 0) {
        fat_path_reset();
    }
    g_fat_dirty = 0;
    if (fat_mount_rc == 0) {
        ensure_command_binary_files();
    }

    g_input_len = 0;
    g_input_buffer[0] = '\0';
    g_prompt_shown = 0;

    console_reset(&g_shell);
    console_write(&g_shell, "myaos shell\n");
    if (fat_mount_rc == 0 && g_fat_demo_mode == 0) {
        console_write(&g_shell, "fat32 boot disk mounted\n");
    } else if (fat_mount_rc == 0) {
        console_write(&g_shell, "fat32 demo mounted\n");
    } else {
        console_write(&g_shell, "fat32 mount failed\n");
    }
    console_write(&g_shell, "type help\n\n");
}

void shell_step(void) {
    if (!g_prompt_shown) {
        console_write(&g_shell, "myaos: ");
        g_prompt_shown = 1;
    }

    char c = keyboard_read_char();
    if (c == 0) {
        return;
    }
    if (c == '\n') {
        console_put_char(&g_shell, '\n');
        g_input_buffer[g_input_len] = '\0';
        shell_execute(&g_shell, g_input_buffer);
        g_input_len = 0;
        g_input_buffer[0] = '\0';
        g_prompt_shown = 0;
        return;
    }
    if (c == '\b') {
        if (g_input_len > 0) {
            g_input_len--;
            g_input_buffer[g_input_len] = '\0';
            console_put_char(&g_shell, '\b');
        }
        return;
    }
    if (c < 32 || c > 126) {
        return;
    }
    if (g_input_len + 1 >= sizeof(g_input_buffer)) {
        return;
    }

    g_input_buffer[g_input_len++] = c;
    g_input_buffer[g_input_len] = '\0';
    console_put_char(&g_shell, c);
}

void shell_start(boot_info_t* boot) {
    shell_init(boot);
    for (;;) {
        shell_step();
        __asm__ __volatile__("hlt");
    }
}
