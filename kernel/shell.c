#include "shell.h"
#include "blockio.h"
#include "fat32.h"
#include "graphics.h"
#include "keyboard.h"
#include "power.h"
#include "ramfs.h"
#include <stddef.h>
#include <stdint.h>

#define SHELL_INPUT_MAX 256
#define SHELL_CMD_MAX 32
#define SHELL_TOKEN_MAX 64
#define SHELL_FATCAT_MAX 1024
#define FAT_PATH_DEPTH_MAX 16

typedef struct {
    boot_info_t* boot;
    uint32_t fg;
    uint32_t bg;
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_col;
    uint32_t cursor_row;
} shell_state_t;

static shell_state_t g_shell;
static fat32_fs_t g_fat32;
static ramfs_t g_ramfs;
static uint8_t g_fat_demo_mode;
static uint8_t g_fat_ready;
static uint8_t g_fat_dirty;
static uint32_t g_fat_path_clusters[FAT_PATH_DEPTH_MAX];
static char g_fat_path_names[FAT_PATH_DEPTH_MAX][FAT32_NAME_MAX];
static uint32_t g_fat_path_depth;

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

static void cmd_help(shell_state_t* shell) {
    console_write(shell, "help      show commands\n");
    console_write(shell, "clear     clear screen\n");
    console_write(shell, "echo      print text\n");
    console_write(shell, "about     show system info\n");
    console_write(shell, "halt      stop cpu\n");
    console_write(shell, "reboot    restart machine\n");
    console_write(shell, "shutdown  power off machine\n");
    console_write(shell, "fatinfo   show FAT32 info\n");
    console_write(shell, "fatpwd    show FAT32 path\n");
    console_write(shell, "fatls     list current FAT32 dir\n");
    console_write(shell, "fatcd     change FAT32 dir\n");
    console_write(shell, "fatcat    read FAT32 file\n");
    console_write(shell, "fatmkdir  create FAT32 dir\n");
    console_write(shell, "fattouch  create FAT32 file\n");
    console_write(shell, "fatwrite  write FAT32 file\n");
    console_write(shell, "fatflush  write FAT32 blocks back\n");
    console_write(shell, "ramls     list RAMFS files\n");
    console_write(shell, "ramwrite  write RAMFS file\n");
    console_write(shell, "ramcat    read RAMFS file\n");
    console_write(shell, "ramrm     remove RAMFS file\n");
    console_write(shell, "ramclear  clear RAMFS\n");
}

static void cmd_fatinfo(shell_state_t* shell) {
    if (!g_fat32.mounted) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    console_write(shell, "fat32 mounted\n");
    console_write(shell, "bytes_per_sector: ");
    console_write_u32(shell, g_fat32.bytes_per_sector);
    console_put_char(shell, '\n');
    console_write(shell, "sectors_per_cluster: ");
    console_write_u32(shell, g_fat32.sectors_per_cluster);
    console_put_char(shell, '\n');
    console_write(shell, "reserved_sectors: ");
    console_write_u32(shell, g_fat32.reserved_sectors);
    console_put_char(shell, '\n');
    console_write(shell, "fat_count: ");
    console_write_u32(shell, g_fat32.fat_count);
    console_put_char(shell, '\n');
    console_write(shell, "sectors_per_fat: ");
    console_write_u32(shell, g_fat32.sectors_per_fat);
    console_put_char(shell, '\n');
    console_write(shell, "total_sectors: ");
    console_write_u32(shell, g_fat32.total_sectors);
    console_put_char(shell, '\n');
    console_write(shell, g_fat_demo_mode ? "source: demo image\n" : "source: boot disk\n");
}

static void cmd_fatpwd(shell_state_t* shell) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    console_put_char(shell, '/');
    for (uint32_t i = 1; i <= g_fat_path_depth; i++) {
        console_write(shell, g_fat_path_names[i]);
        if (i != g_fat_path_depth) {
            console_put_char(shell, '/');
        }
    }
    console_put_char(shell, '\n');
}

static void cmd_fatls(shell_state_t* shell) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    fat32_dirent_t entries[32];
    size_t count = 0;
    int rc = fat32_list_dir(&g_fat32, fat_current_cluster(), entries, 32, &count);

    if (rc == -1) {
        console_write(shell, "fat32 read error\n");
        return;
    }
    if (count == 0) {
        console_write(shell, "fat32 dir is empty\n");
        return;
    }

    for (size_t i = 0; i < count; i++) {
        console_write(shell, entries[i].name);
        if (entries[i].is_dir) {
            console_write(shell, " dir\n");
        } else {
            console_write(shell, " ");
            console_write_u32(shell, entries[i].size);
            console_write(shell, " bytes\n");
        }
    }

    if (rc == -2) {
        console_write(shell, "output truncated\n");
    }
}

static void cmd_fatcd(shell_state_t* shell, const char* args) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    char target[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args, target, sizeof(target), &rest)) {
        console_write(shell, "usage: fatcd DIR\n");
        return;
    }
    (void)rest;

    if (str_eq(target, "/")) {
        fat_path_reset();
        return;
    }

    if (str_eq(target, "..")) {
        if (g_fat_path_depth > 0) {
            g_fat_path_depth--;
        }
        return;
    }

    fat32_dirent_t entry;
    int rc = fat32_lookup(&g_fat32, fat_current_cluster(), target, &entry);
    if (rc != 0) {
        console_write(shell, "fat32 dir not found\n");
        return;
    }
    if (!entry.is_dir) {
        console_write(shell, "fat32 target is not dir\n");
        return;
    }
    if (g_fat_path_depth + 1 >= FAT_PATH_DEPTH_MAX) {
        console_write(shell, "fat32 path depth limit\n");
        return;
    }

    g_fat_path_depth++;
    g_fat_path_clusters[g_fat_path_depth] = entry.first_cluster;
    str_copy(g_fat_path_names[g_fat_path_depth], entry.name, FAT32_NAME_MAX);
}

static void cmd_fatcat(shell_state_t* shell, const char* args) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args, name, sizeof(name), &rest)) {
        console_write(shell, "usage: fatcat FILE\n");
        return;
    }
    (void)rest;

    uint8_t data[SHELL_FATCAT_MAX + 1];
    uint32_t read_size = 0;
    int rc = fat32_read_file(&g_fat32, fat_current_cluster(), name, data, SHELL_FATCAT_MAX, &read_size);

    if (rc == -2) {
        console_write(shell, "fat32 file not found\n");
        return;
    }
    if (rc == -3) {
        console_write(shell, "fat32 target is directory\n");
        return;
    }
    if (rc == -4) {
        console_write(shell, "fat32 file too large for buffer\n");
        return;
    }
    if (rc != 0) {
        console_write(shell, "fat32 read failed\n");
        return;
    }
    if (read_size == 0) {
        console_write(shell, "(empty)\n");
        return;
    }

    data[read_size] = 0;
    console_write_binary_as_text(shell, data, read_size);
    if (data[read_size - 1] != '\n') {
        console_put_char(shell, '\n');
    }
}

static void cmd_fatmkdir(shell_state_t* shell, const char* args) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args, name, sizeof(name), &rest)) {
        console_write(shell, "usage: fatmkdir DIR\n");
        return;
    }
    (void)rest;

    int rc = fat32_mkdir(&g_fat32, fat_current_cluster(), name);
    if (rc == -2) {
        console_write(shell, "fat32 entry exists\n");
        return;
    }
    if (rc != 0) {
        console_write(shell, "fat32 mkdir failed\n");
        return;
    }

    console_write(shell, "fat32 mkdir ok\n");
    g_fat_dirty = 1;
}

static void cmd_fattouch(shell_state_t* shell, const char* args) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args, name, sizeof(name), &rest)) {
        console_write(shell, "usage: fattouch FILE\n");
        return;
    }
    (void)rest;

    int rc = fat32_create_file(&g_fat32, fat_current_cluster(), name);
    if (rc == -2) {
        console_write(shell, "fat32 entry exists\n");
        return;
    }
    if (rc != 0) {
        console_write(shell, "fat32 create failed\n");
        return;
    }

    console_write(shell, "fat32 file created\n");
    g_fat_dirty = 1;
}

static void cmd_fatwrite(shell_state_t* shell, const char* args) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }

    char name[SHELL_TOKEN_MAX];
    const char* text = NULL;
    if (!parse_first_token(args, name, sizeof(name), &text)) {
        console_write(shell, "usage: fatwrite FILE TEXT\n");
        return;
    }

    int rc = fat32_write_file(
        &g_fat32,
        fat_current_cluster(),
        name,
        (const uint8_t*)text,
        (uint32_t)str_len(text)
    );
    if (rc == -3) {
        console_write(shell, "fat32 target is directory\n");
        return;
    }
    if (rc != 0) {
        console_write(shell, "fat32 write failed\n");
        return;
    }

    console_write(shell, "fat32 write ok\n");
    g_fat_dirty = 1;
}

static void cmd_fatflush(shell_state_t* shell) {
    if (!g_fat_ready) {
        console_write(shell, "fat32 not mounted\n");
        return;
    }
    if (g_fat_demo_mode) {
        console_write(shell, "fat32 demo image cannot writeback\n");
        return;
    }
    if (!g_fat_dirty) {
        console_write(shell, "fat32 clean\n");
        return;
    }

    int rc = blockio_writeback(shell->boot);
    if (rc != 0) {
        console_write(shell, "fat32 writeback failed\n");
        return;
    }

    g_fat_dirty = 0;
    console_write(shell, "fat32 writeback ok\n");
}

static void cmd_ramls(shell_state_t* shell) {
    uint32_t count = ramfs_count(&g_ramfs);
    if (count == 0) {
        console_write(shell, "ramfs is empty\n");
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const ramfs_file_t* file = ramfs_file_at(&g_ramfs, i);
        if (!file) {
            continue;
        }

        console_write(shell, file->name);
        console_write(shell, " ");
        console_write_u32(shell, file->size);
        console_write(shell, " bytes\n");
    }
}

static void cmd_ramwrite(shell_state_t* shell, const char* args) {
    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args, name, sizeof(name), &rest)) {
        console_write(shell, "usage: ramwrite NAME TEXT\n");
        return;
    }

    int rc = ramfs_write(&g_ramfs, name, rest);
    if (rc == -1) {
        console_write(shell, "ramfs invalid name\n");
        return;
    }
    if (rc == -2) {
        console_write(shell, "ramfs text too long\n");
        return;
    }
    if (rc == -3) {
        console_write(shell, "ramfs is full\n");
        return;
    }

    console_write(shell, "ramfs write ok\n");
}

static void cmd_ramcat(shell_state_t* shell, const char* args) {
    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args, name, sizeof(name), &rest)) {
        console_write(shell, "usage: ramcat NAME\n");
        return;
    }
    (void)rest;

    const char* data = NULL;
    uint32_t size = 0;
    if (ramfs_read(&g_ramfs, name, &data, &size) != 0) {
        console_write(shell, "ramfs file not found\n");
        return;
    }
    if (size == 0) {
        console_write(shell, "(empty)\n");
        return;
    }

    console_write_binary_as_text(shell, (const uint8_t*)data, size);
    if (data[size - 1] != '\n') {
        console_put_char(shell, '\n');
    }
}

static void cmd_ramrm(shell_state_t* shell, const char* args) {
    char name[SHELL_TOKEN_MAX];
    const char* rest = NULL;
    if (!parse_first_token(args, name, sizeof(name), &rest)) {
        console_write(shell, "usage: ramrm NAME\n");
        return;
    }
    (void)rest;

    if (ramfs_remove(&g_ramfs, name) != 0) {
        console_write(shell, "ramfs file not found\n");
        return;
    }
    console_write(shell, "ramfs file removed\n");
}

static void fat_flush_if_needed(shell_state_t* shell) {
    if (!g_fat_ready || g_fat_demo_mode || !g_fat_dirty) {
        return;
    }

    if (blockio_writeback(shell->boot) == 0) {
        g_fat_dirty = 0;
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
    if (str_eq(cmd, "clear")) {
        console_reset(shell);
        return;
    }
    if (str_eq(cmd, "about")) {
        console_write(shell, "myaos kernel shell\n");
        console_write(shell, "features: fat32 rw ramfs power\n");
        return;
    }
    if (str_eq(cmd, "echo")) {
        console_write(shell, args);
        console_put_char(shell, '\n');
        return;
    }
    if (str_eq(cmd, "halt")) {
        fat_flush_if_needed(shell);
        console_write(shell, "cpu halted\n");
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    }
    if (str_eq(cmd, "reboot")) {
        fat_flush_if_needed(shell);
        console_write(shell, "rebooting\n");
        power_reboot(shell->boot);
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    }
    if (str_eq(cmd, "shutdown")) {
        fat_flush_if_needed(shell);
        console_write(shell, "powering off\n");
        power_shutdown(shell->boot);
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    }
    if (str_eq(cmd, "fatinfo")) {
        cmd_fatinfo(shell);
        return;
    }
    if (str_eq(cmd, "fatpwd")) {
        cmd_fatpwd(shell);
        return;
    }
    if (str_eq(cmd, "fatls")) {
        cmd_fatls(shell);
        return;
    }
    if (str_eq(cmd, "fatcd")) {
        cmd_fatcd(shell, args);
        return;
    }
    if (str_eq(cmd, "fatcat")) {
        cmd_fatcat(shell, args);
        return;
    }
    if (str_eq(cmd, "fatmkdir")) {
        cmd_fatmkdir(shell, args);
        return;
    }
    if (str_eq(cmd, "fattouch")) {
        cmd_fattouch(shell, args);
        return;
    }
    if (str_eq(cmd, "fatwrite")) {
        cmd_fatwrite(shell, args);
        return;
    }
    if (str_eq(cmd, "fatflush")) {
        cmd_fatflush(shell);
        return;
    }
    if (str_eq(cmd, "ramls")) {
        cmd_ramls(shell);
        return;
    }
    if (str_eq(cmd, "ramwrite")) {
        cmd_ramwrite(shell, args);
        return;
    }
    if (str_eq(cmd, "ramcat")) {
        cmd_ramcat(shell, args);
        return;
    }
    if (str_eq(cmd, "ramrm")) {
        cmd_ramrm(shell, args);
        return;
    }
    if (str_eq(cmd, "ramclear")) {
        ramfs_clear(&g_ramfs);
        console_write(shell, "ramfs cleared\n");
        return;
    }

    console_write(shell, "unknown command\n");
}

void shell_start(boot_info_t* boot) {
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

    for (;;) {
        char input[SHELL_INPUT_MAX];
        size_t input_len = 0;

        console_write(&g_shell, "myaos: ");

        for (;;) {
            char c = keyboard_read_char();
            if (c == 0) {
                continue;
            }
            if (c == '\n') {
                console_put_char(&g_shell, '\n');
                input[input_len] = '\0';
                shell_execute(&g_shell, input);
                break;
            }
            if (c == '\b') {
                if (input_len > 0) {
                    input_len--;
                    console_put_char(&g_shell, '\b');
                }
                continue;
            }
            if (c < 32 || c > 126) {
                continue;
            }
            if (input_len + 1 >= sizeof(input)) {
                continue;
            }

            input[input_len++] = c;
            console_put_char(&g_shell, c);
        }
    }
}
