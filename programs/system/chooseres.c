#include "../lib/myaos.h"
#include <stdint.h>

#define CHOOSERES_MAX_MODES 64u
#define CHOOSERES_TEXT_MAX 4096u
#define CHOOSERES_CFG_MAX 128u
#define CHOOSERES_VISIBLE_ROWS 12u

#define KEY_UP ((char)0x11)
#define KEY_DOWN ((char)0x12)
#define KEY_LEFT ((char)0x13)
#define KEY_RIGHT ((char)0x14)

#define PREF_PATH_PRIMARY "/boot/myares.cfg"
#define PREF_PATH_FALLBACK "/myares.cfg"
#define PREF_PATH_LEGACY_PRIMARY "/boot/myaos.resolution.cfg"
#define PREF_PATH_LEGACY_FALLBACK "/myaos.resolution.cfg"

typedef struct {
    uint32_t mode;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint8_t current;
} mode_entry_t;

static uint8_t g_text[CHOOSERES_TEXT_MAX];

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0u;

    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int str_starts_with(const char* text, const char* prefix) {
    uint32_t i = 0u;

    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static const char* str_find(const char* text, const char* needle) {
    uint32_t nlen = 0u;

    if (!text || !needle || !needle[0]) {
        return NULL;
    }
    while (needle[nlen]) {
        nlen++;
    }
    for (uint32_t i = 0u; text[i]; i++) {
        uint32_t j = 0u;
        while (j < nlen && text[i + j] == needle[j]) {
            j++;
        }
        if (j == nlen) {
            return text + i;
        }
    }
    return NULL;
}

static void str_copy(char* dst, uint32_t dst_cap, const char* src) {
    uint32_t i = 0u;

    if (!dst || dst_cap == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1u < dst_cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int append_text(char* out, uint32_t out_cap, uint32_t* pos, const char* text) {
    uint32_t i = 0u;

    if (!out || !pos || !text || *pos >= out_cap) {
        return -1;
    }
    while (text[i]) {
        if (*pos + 1u >= out_cap) {
            return -1;
        }
        out[*pos] = text[i];
        (*pos)++;
        i++;
    }
    out[*pos] = '\0';
    return 0;
}

static int parse_u32(const char* text, uint32_t* out) {
    uint64_t value = 0u;

    if (!text || !text[0] || !out || mya_strto_u64(text, &value) != 0 || value > 0xFFFFFFFFull) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static char* trim_ascii(char* text) {
    uint32_t start = 0u;
    uint32_t end = 0u;

    if (!text) {
        return NULL;
    }
    while (text[end]) {
        end++;
    }
    while (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n') {
        start++;
    }
    while (end > start &&
           (text[end - 1u] == ' ' || text[end - 1u] == '\t' || text[end - 1u] == '\r' || text[end - 1u] == '\n')) {
        end--;
    }
    text[end] = '\0';
    return text + start;
}

static int read_text_file(const char* path, uint8_t* out_buf, uint32_t out_cap, uint32_t* out_size) {
    uint32_t size = 0u;

    if (!path || !out_buf || out_cap == 0u) {
        return -1;
    }
    if (mya_fs_read(path, out_buf, out_cap - 1u, &size) != 0) {
        return -1;
    }
    out_buf[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return 0;
}

static int parse_mode_line(const char* line, mode_entry_t* out) {
    const char* p;
    const char* sx;
    const char* sf;
    char tmp[32];
    uint32_t n = 0u;

    if (!line || !out || !str_starts_with(line, "mode=")) {
        return -1;
    }

    p = line + 5u;
    while (p[n] >= '0' && p[n] <= '9') {
        n++;
    }
    if (n == 0u || n >= sizeof(tmp)) {
        return -1;
    }
    for (uint32_t i = 0u; i < n; i++) {
        tmp[i] = p[i];
    }
    tmp[n] = '\0';
    if (parse_u32(tmp, &out->mode) != 0) {
        return -1;
    }
    p += n;

    while (*p == ' ') {
        p++;
    }
    out->current = 0u;
    if (str_starts_with(p, "current")) {
        out->current = 1u;
        p += 7u;
        while (*p == ' ') {
            p++;
        }
    }

    n = 0u;
    while (p[n] >= '0' && p[n] <= '9') {
        n++;
    }
    if (n == 0u || n >= sizeof(tmp) || p[n] != 'x') {
        return -1;
    }
    for (uint32_t i = 0u; i < n; i++) {
        tmp[i] = p[i];
    }
    tmp[n] = '\0';
    if (parse_u32(tmp, &out->width) != 0) {
        return -1;
    }
    p += n + 1u;

    n = 0u;
    while (p[n] >= '0' && p[n] <= '9') {
        n++;
    }
    if (n == 0u || n >= sizeof(tmp)) {
        return -1;
    }
    for (uint32_t i = 0u; i < n; i++) {
        tmp[i] = p[i];
    }
    tmp[n] = '\0';
    if (parse_u32(tmp, &out->height) != 0) {
        return -1;
    }

    out->stride = 0u;
    out->format = 0u;

    sx = str_find(line, "stride=");
    if (sx) {
        if (parse_u32(sx + 7u, &out->stride) != 0) {
            out->stride = 0u;
        }
    }
    sf = str_find(line, "fmt=");
    if (sf) {
        if (parse_u32(sf + 4u, &out->format) != 0) {
            out->format = 0u;
        }
    }
    return 0;
}

static int read_fb_modes(mode_entry_t* out, uint32_t out_cap, uint32_t* out_count, uint32_t* out_current_idx) {
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t current_idx = 0u;
    uint8_t has_current = 0u;
    uint32_t current_mode = 0u;
    uint8_t has_current_mode = 0u;
    uint32_t i = 0u;

    if (!out || out_cap == 0u || !out_count || !out_current_idx) {
        return -1;
    }
    if (read_text_file("/sys/fb_modes", g_text, sizeof(g_text), &size) != 0) {
        return -1;
    }

    while (i < size) {
        uint32_t line_start = i;
        uint32_t line_end = i;

        while (line_end < size && g_text[line_end] != '\n' && g_text[line_end] != '\r') {
            line_end++;
        }
        if (line_end < size) {
            g_text[line_end] = '\0';
        }
        i = line_end + 1u;

        if (g_text[line_start] == '\0' || str_eq((const char*)g_text + line_start, "none")) {
            continue;
        }
        if (count >= out_cap) {
            break;
        }
        if (parse_mode_line((const char*)g_text + line_start, &out[count]) == 0) {
            if (out[count].current) {
                has_current = 1u;
                current_idx = count;
            }
            count++;
        }
    }

    if (read_text_file("/sys/fb_mode", g_text, sizeof(g_text), &size) == 0) {
        if (parse_u32(trim_ascii((char*)g_text), &current_mode) == 0) {
            has_current_mode = 1u;
        }
    }

    if (!has_current && has_current_mode) {
        for (uint32_t idx = 0u; idx < count; idx++) {
            if (out[idx].mode == current_mode) {
                current_idx = idx;
                has_current = 1u;
                break;
            }
        }
    }
    if (!has_current) {
        current_idx = 0u;
    }

    *out_count = count;
    *out_current_idx = current_idx;
    return 0;
}

static int parse_saved_pref(uint8_t* out_has_mode, uint32_t* out_mode) {
    const char* pref_paths[] = {
        PREF_PATH_PRIMARY,
        PREF_PATH_FALLBACK,
        PREF_PATH_LEGACY_PRIMARY,
        PREF_PATH_LEGACY_FALLBACK,
    };
    uint32_t size = 0u;
    uint32_t has_mode32 = 0u;
    uint32_t mode32 = 0u;

    if (!out_has_mode || !out_mode) {
        return -1;
    }
    *out_has_mode = 0u;
    *out_mode = 0u;

    if (mya_fb_pref_get(&has_mode32, &mode32) == 0) {
        *out_has_mode = has_mode32 ? 1u : 0u;
        *out_mode = mode32;
        return 0;
    }

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(pref_paths) / sizeof(pref_paths[0])); i++) {
        uint32_t p = 0u;

        if (read_text_file(pref_paths[i], g_text, sizeof(g_text), &size) != 0) {
            continue;
        }

        while (p < size) {
            uint32_t line_start = p;
            uint32_t line_end = p;
            char* value;

            while (line_end < size && g_text[line_end] != '\n' && g_text[line_end] != '\r') {
                line_end++;
            }
            if (line_end < size) {
                g_text[line_end] = '\0';
            }
            p = line_end + 1u;

            value = trim_ascii((char*)g_text + line_start);
            if (!value || !value[0] || value[0] == '#') {
                continue;
            }
            if (!str_starts_with(value, "mode=")) {
                continue;
            }
            value = trim_ascii(value + 5u);
            if (str_eq(value, "auto")) {
                *out_has_mode = 0u;
                *out_mode = 0u;
                return 0;
            }
            if (parse_u32(value, out_mode) == 0) {
                *out_has_mode = 1u;
                return 0;
            }
            return -1;
        }
    }
    return 0;
}

static int find_mode_index(const mode_entry_t* modes, uint32_t count, uint32_t mode) {
    if (!modes) {
        return -1;
    }
    for (uint32_t i = 0u; i < count; i++) {
        if (modes[i].mode == mode) {
            return (int)i;
        }
    }
    return -1;
}

static void print_file_fallback_warning(const char* path) {
    mya_putln("chooseres: warning: UEFI preference write failed");
    mya_puts("chooseres: fallback file used: ");
    mya_putln(path ? path : "<unknown>");
    mya_putln("chooseres: warning: file fallback may not persist after reboot");
}

static int save_pref_file(
    uint8_t has_mode,
    uint32_t mode,
    char* out_used_path,
    uint32_t out_used_path_cap,
    uint8_t* out_used_file_fallback
) {
    const char* pref_paths[] = {
        PREF_PATH_PRIMARY,
        PREF_PATH_FALLBACK,
        PREF_PATH_LEGACY_PRIMARY,
        PREF_PATH_LEGACY_FALLBACK,
    };
    char content[CHOOSERES_CFG_MAX];
    uint32_t pos = 0u;

    if (out_used_file_fallback) {
        *out_used_file_fallback = 0u;
    }

    content[0] = '\0';
    if (append_text(content, sizeof(content), &pos, "# MYAOS_RESOLUTION1\nmode=") != 0) {
        return -1;
    }
    if (has_mode) {
        char mode_text[16];
        mya_u32_to_dec(mode, mode_text, sizeof(mode_text));
        if (append_text(content, sizeof(content), &pos, mode_text) != 0 ||
            append_text(content, sizeof(content), &pos, "\n") != 0) {
            return -1;
        }
    } else {
        if (append_text(content, sizeof(content), &pos, "auto\n") != 0) {
            return -1;
        }
    }

    if (mya_fb_pref_set(has_mode ? 1u : 0u, mode) == 0) {
        uint32_t has_mode32 = 0u;
        uint32_t mode32 = 0u;

        if (mya_fb_pref_get(&has_mode32, &mode32) == 0 &&
            has_mode32 == (has_mode ? 1u : 0u) &&
            (has_mode == 0u || mode32 == mode)) {
            if (out_used_path && out_used_path_cap > 0u) {
                str_copy(out_used_path, out_used_path_cap, "uefi:MyaOSGopMode");
            }
            return 0;
        }
    }

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(pref_paths) / sizeof(pref_paths[0])); i++) {
        int rc = mya_fs_write(pref_paths[i], content, pos);
        if (rc != 0) {
            (void)mya_fs_touch(pref_paths[i]);
            rc = mya_fs_write(pref_paths[i], content, pos);
        }
        if (rc == 0) {
            if (out_used_path && out_used_path_cap > 0u) {
                str_copy(out_used_path, out_used_path_cap, pref_paths[i]);
            }
            if (out_used_file_fallback) {
                *out_used_file_fallback = 1u;
            }
            return 1;
        }
    }
    return -1;
}

static int wait_key(void) {
    for (;;) {
        int ch = mya_console_readchar();
        if (ch != 0) {
            return ch;
        }
        mya_proc_yield();
    }
}

static void print_box_line(const char* text, uint8_t selected) {
    uint32_t len = 0u;
    const uint32_t width = 72u;

    if (!text) {
        text = "";
    }
    while (text[len]) {
        len++;
    }

    if (selected) {
        mya_puts("\x1b[30;47m");
    } else {
        mya_puts("\x1b[37;44m");
    }
    mya_puts("| ");
    for (uint32_t i = 0u; i < len && i < width - 2u; i++) {
        char ch[2] = { text[i], '\0' };
        mya_puts(ch);
    }
    if (len < width - 2u) {
        for (uint32_t i = 0u; i < (width - 2u) - len; i++) {
            mya_puts(" ");
        }
    }
    mya_puts(" |\n");
}

static void render_menu(
    const mode_entry_t* modes,
    uint32_t count,
    uint32_t selected,
    uint32_t top,
    uint8_t saved_has_mode,
    uint32_t saved_mode
) {
    char row[128];

    mya_console_clear();
    mya_puts("\x1b[37;44m");
    mya_putln("+--------------------------------------------------------------------------+");
    print_box_line("                             GNU GRUB Style                                ", 0u);
    print_box_line("                         MyaOS Resolution Chooser                          ", 0u);
    mya_putln("+--------------------------------------------------------------------------+");

    if (count == 0u) {
        print_box_line("No GOP modes available in /sys/fb_modes", 0u);
    } else {
        for (uint32_t i = 0u; i < CHOOSERES_VISIBLE_ROWS; i++) {
            uint32_t idx = top + i;
            if (idx >= count) {
                print_box_line("", 0u);
                continue;
            }
            uint32_t pos = 0u;
            char nbuf[16];

            row[0] = '\0';
            if (append_text(row, sizeof(row), &pos, (idx == selected) ? "> " : "  ") != 0) {
                row[0] = '\0';
            }
            mya_u32_to_dec(modes[idx].mode, nbuf, sizeof(nbuf));
            (void)append_text(row, sizeof(row), &pos, "mode=");
            (void)append_text(row, sizeof(row), &pos, nbuf);
            (void)append_text(row, sizeof(row), &pos, "  ");
            mya_u32_to_dec(modes[idx].width, nbuf, sizeof(nbuf));
            (void)append_text(row, sizeof(row), &pos, nbuf);
            (void)append_text(row, sizeof(row), &pos, "x");
            mya_u32_to_dec(modes[idx].height, nbuf, sizeof(nbuf));
            (void)append_text(row, sizeof(row), &pos, nbuf);
            (void)append_text(row, sizeof(row), &pos, "  stride=");
            mya_u32_to_dec(modes[idx].stride, nbuf, sizeof(nbuf));
            (void)append_text(row, sizeof(row), &pos, nbuf);
            if (modes[idx].current) {
                (void)append_text(row, sizeof(row), &pos, "  [current]");
            }
            if (saved_has_mode && modes[idx].mode == saved_mode) {
                (void)append_text(row, sizeof(row), &pos, "  [saved]");
            }
            print_box_line(row, idx == selected);
        }
    }

    mya_putln("+--------------------------------------------------------------------------+");
    print_box_line("Keys: Up/Down or j/k, Enter=save mode, a=save auto, q/Esc=quit", 0u);
    print_box_line("Changes apply on next boot", 0u);
    mya_putln("+--------------------------------------------------------------------------+");
    mya_puts("\x1b[0m");
}

static void print_plain_modes(const mode_entry_t* modes, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        mya_puts("mode=");
        mya_put_u32(modes[i].mode);
        mya_puts(" ");
        mya_put_u32(modes[i].width);
        mya_puts("x");
        mya_put_u32(modes[i].height);
        mya_puts(" stride=");
        mya_put_u32(modes[i].stride);
        mya_puts(" fmt=");
        mya_put_u32(modes[i].format);
        if (modes[i].current) {
            mya_puts(" current");
        }
        mya_puts("\n");
    }
}

static void print_usage(void) {
    mya_putln("usage:");
    mya_putln("  chooseres");
    mya_putln("  chooseres --list");
    mya_putln("  chooseres --set <mode>");
    mya_putln("  chooseres --auto");
}

int program_main(int argc, char** argv) {
    mode_entry_t modes[CHOOSERES_MAX_MODES];
    uint32_t mode_count = 0u;
    uint32_t current_idx = 0u;
    uint32_t selected = 0u;
    uint32_t top = 0u;
    uint8_t saved_has_mode = 0u;
    uint32_t saved_mode = 0u;

    if (argc >= 2) {
        if (str_eq(argv[1], "--list")) {
            if (read_fb_modes(modes, CHOOSERES_MAX_MODES, &mode_count, &current_idx) != 0 || mode_count == 0u) {
                mya_putln("chooseres: failed to read /sys/fb_modes");
                return 1;
            }
            print_plain_modes(modes, mode_count);
            return 0;
        }
        if (str_eq(argv[1], "--auto")) {
            char path[MYAOS_PATH_MAX];
            uint8_t used_file_fallback = 0u;
            int save_rc = save_pref_file(0u, 0u, path, sizeof(path), &used_file_fallback);

            if (save_rc < 0) {
                mya_putln("chooseres: failed to save auto mode preference");
                return 1;
            }
            mya_puts("chooseres: saved auto mode to ");
            mya_putln(path);
            if (used_file_fallback) {
                print_file_fallback_warning(path);
            }
            mya_putln("chooseres: reboot to apply");
            return 0;
        }
        if (str_eq(argv[1], "--set")) {
            uint32_t mode = 0u;
            char path[MYAOS_PATH_MAX];
            uint8_t used_file_fallback = 0u;
            int save_rc;

            if (argc < 3 || parse_u32(argv[2], &mode) != 0) {
                mya_putln("chooseres: --set requires numeric mode");
                return 1;
            }
            if (read_fb_modes(modes, CHOOSERES_MAX_MODES, &mode_count, &current_idx) == 0 && mode_count > 0u) {
                int idx = find_mode_index(modes, mode_count, mode);
                if (idx < 0) {
                    mya_putln("chooseres: mode is not available");
                    return 1;
                }
            }
            save_rc = save_pref_file(1u, mode, path, sizeof(path), &used_file_fallback);
            if (save_rc < 0) {
                mya_putln("chooseres: failed to save mode preference");
                return 1;
            }
            mya_puts("chooseres: saved mode=");
            mya_put_u32(mode);
            mya_puts(" to ");
            mya_putln(path);
            if (used_file_fallback) {
                print_file_fallback_warning(path);
            }
            mya_putln("chooseres: reboot to apply");
            return 0;
        }
        print_usage();
        return 1;
    }

    if (read_fb_modes(modes, CHOOSERES_MAX_MODES, &mode_count, &current_idx) != 0 || mode_count == 0u) {
        mya_putln("chooseres: failed to read /sys/fb_modes");
        return 1;
    }
    (void)parse_saved_pref(&saved_has_mode, &saved_mode);

    selected = current_idx;
    if (saved_has_mode) {
        int idx = find_mode_index(modes, mode_count, saved_mode);
        if (idx >= 0) {
            selected = (uint32_t)idx;
        }
    }

    for (;;) {
        int ch;

        if (selected < top) {
            top = selected;
        } else if (selected >= top + CHOOSERES_VISIBLE_ROWS) {
            top = selected - CHOOSERES_VISIBLE_ROWS + 1u;
        }

        render_menu(modes, mode_count, selected, top, saved_has_mode, saved_mode);
        ch = wait_key();

        if (ch == KEY_UP || ch == 'k' || ch == 'K') {
            if (selected > 0u) {
                selected--;
            }
            continue;
        }
        if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
            if (selected + 1u < mode_count) {
                selected++;
            }
            continue;
        }
        if (ch == KEY_LEFT) {
            if (selected > 5u) {
                selected -= 5u;
            } else {
                selected = 0u;
            }
            continue;
        }
        if (ch == KEY_RIGHT) {
            selected += 5u;
            if (selected >= mode_count) {
                selected = mode_count - 1u;
            }
            continue;
        }
        if (ch == 'g') {
            selected = 0u;
            continue;
        }
        if (ch == 'G') {
            selected = mode_count - 1u;
            continue;
        }
        if (ch == 'a' || ch == 'A') {
            char path[MYAOS_PATH_MAX];
            uint8_t used_file_fallback = 0u;
            int save_rc = save_pref_file(0u, 0u, path, sizeof(path), &used_file_fallback);

            if (save_rc < 0) {
                mya_putln("chooseres: failed to save auto mode preference");
                return 1;
            }
            mya_console_clear();
            mya_putln("chooseres: auto mode saved");
            mya_puts("file: ");
            mya_putln(path);
            if (used_file_fallback) {
                print_file_fallback_warning(path);
            }
            mya_putln("reboot to apply");
            return 0;
        }
        if (ch == '\n' || ch == '\r' || ch == 's' || ch == 'S') {
            char path[MYAOS_PATH_MAX];
            uint8_t used_file_fallback = 0u;
            int save_rc = save_pref_file(1u, modes[selected].mode, path, sizeof(path), &used_file_fallback);

            if (save_rc < 0) {
                mya_putln("chooseres: failed to save mode preference");
                return 1;
            }
            mya_console_clear();
            mya_puts("chooseres: saved mode=");
            mya_put_u32(modes[selected].mode);
            mya_puts(" (");
            mya_put_u32(modes[selected].width);
            mya_puts("x");
            mya_put_u32(modes[selected].height);
            mya_putln(")");
            mya_puts("file: ");
            mya_putln(path);
            if (used_file_fallback) {
                print_file_fallback_warning(path);
            }
            mya_putln("reboot to apply");
            return 0;
        }
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            mya_console_clear();
            mya_putln("chooseres: cancelled");
            return 0;
        }
    }
}
