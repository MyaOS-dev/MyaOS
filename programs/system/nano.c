#include "../lib/myaos.h"
#include <stdint.h>

#define NANO_FILE_MAX (256u * 1024u)
#define NANO_STATUS_MAX 160u
#define NANO_LINEBUF_MAX 320u

#define NANO_FALLBACK_COLS 80u
#define NANO_FALLBACK_ROWS 25u

#define NANO_KEY_ESC 0x01u
#define NANO_KEY_BACKSPACE 0x0Eu
#define NANO_KEY_ENTER 0x1Cu
#define NANO_KEY_O 0x18u
#define NANO_KEY_X 0x2Du
#define NANO_KEY_UP 0xC8u
#define NANO_KEY_DOWN 0xD0u
#define NANO_KEY_LEFT 0xCBu
#define NANO_KEY_RIGHT 0xCDu
#define NANO_KEY_DELETE 0xD3u

static uint8_t g_text[NANO_FILE_MAX];
static uint32_t g_len = 0u;
static uint32_t g_cursor = 0u;
static uint32_t g_desired_col = 0u;
static uint32_t g_view_top_line = 0u;
static uint32_t g_view_left_col = 0u;
static uint8_t g_keep_desired_col = 0u;
static uint8_t g_modified = 0u;
static uint8_t g_quit_armed = 0u;
static char g_status[NANO_STATUS_MAX];
static uint8_t g_screen_size_known = 0u;
static uint32_t g_last_cols = 0u;
static uint32_t g_last_rows = 0u;

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static uint32_t max_u32(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
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

static int parse_u32_str(const char* text, uint32_t* out) {
    uint64_t value = 0u;
    if (!text || !text[0] || !out || mya_strto_u64(text, &value) != 0 || value > 0xFFFFFFFFull) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int read_sys_u32(const char* path, uint32_t* out) {
    uint8_t buf[32];
    uint32_t size = 0u;
    char text[32];

    if (!path || !out) {
        return -1;
    }
    if (mya_fs_read(path, buf, sizeof(buf) - 1u, &size) != 0 || size == 0u || size >= sizeof(text)) {
        return -1;
    }
    for (uint32_t i = 0u; i < size; i++) {
        text[i] = (char)buf[i];
    }
    text[size] = '\0';

    {
        uint32_t i = 0u;
        while (text[i]) {
            if (text[i] == '\n' || text[i] == '\r' || text[i] == ' ' || text[i] == '\t') {
                text[i] = '\0';
                break;
            }
            i++;
        }
    }
    return parse_u32_str(text, out);
}

static void mem_move_u8(uint8_t* dst, const uint8_t* src, uint32_t size) {
    if (!dst || !src || size == 0u || dst == src) {
        return;
    }
    if (dst < src) {
        for (uint32_t i = 0u; i < size; i++) {
            dst[i] = src[i];
        }
        return;
    }
    for (uint32_t i = size; i > 0u; i--) {
        dst[i - 1u] = src[i - 1u];
    }
}

static void put_char(char c) {
    char out[2];
    out[0] = c;
    out[1] = '\0';
    mya_puts(out);
}

static void put_spaces(uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        put_char(' ');
    }
}

static int append_text(char* out, uint32_t out_cap, uint32_t* io_pos, const char* text) {
    uint32_t pos;

    if (!out || out_cap == 0u || !io_pos || !text) {
        return -1;
    }
    pos = *io_pos;
    for (uint32_t i = 0u; text[i]; i++) {
        if (pos + 1u >= out_cap) {
            return -1;
        }
        out[pos++] = text[i];
    }
    out[pos] = '\0';
    *io_pos = pos;
    return 0;
}

static int append_u32(char* out, uint32_t out_cap, uint32_t* io_pos, uint32_t value) {
    char dec[16];
    mya_u32_to_dec(value, dec, sizeof(dec));
    return append_text(out, out_cap, io_pos, dec);
}

static void set_status(const char* text) {
    str_copy(g_status, sizeof(g_status), text ? text : "");
}

static uint32_t line_start_of(uint32_t index) {
    if (index > g_len) {
        index = g_len;
    }
    while (index > 0u && g_text[index - 1u] != '\n') {
        index--;
    }
    return index;
}

static uint32_t line_end_of(uint32_t start) {
    uint32_t i = start;
    if (i > g_len) {
        i = g_len;
    }
    while (i < g_len && g_text[i] != '\n') {
        i++;
    }
    return i;
}

static uint32_t total_lines(void) {
    uint32_t lines = 1u;
    for (uint32_t i = 0u; i < g_len; i++) {
        if (g_text[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

static uint32_t line_offset(uint32_t line_no) {
    uint32_t current = 0u;

    if (line_no == 0u) {
        return 0u;
    }
    for (uint32_t i = 0u; i < g_len; i++) {
        if (g_text[i] == '\n') {
            current++;
            if (current == line_no) {
                return i + 1u;
            }
        }
    }
    return g_len;
}

static void cursor_line_col(uint32_t* out_line, uint32_t* out_col) {
    uint32_t line = 0u;
    uint32_t col = 0u;

    if (g_cursor > g_len) {
        g_cursor = g_len;
    }
    for (uint32_t i = 0u; i < g_cursor; i++) {
        if (g_text[i] == '\n') {
            line++;
            col = 0u;
        } else {
            col++;
        }
    }
    if (out_line) {
        *out_line = line;
    }
    if (out_col) {
        *out_col = col;
    }
}

static void detect_console_size(uint32_t* out_cols, uint32_t* out_rows) {
    myaos_gfx_info_t info;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t cols = NANO_FALLBACK_COLS;
    uint32_t rows = NANO_FALLBACK_ROWS;

    if (out_cols) {
        *out_cols = NANO_FALLBACK_COLS;
    }
    if (out_rows) {
        *out_rows = NANO_FALLBACK_ROWS;
    }

    if (mya_gfx_info_get(&info) == 0) {
        width = info.width;
        height = info.height;
    }
    if (width == 0u || height == 0u) {
        (void)read_sys_u32("/sys/fb_width", &width);
        (void)read_sys_u32("/sys/fb_height", &height);
    }

    if (width >= 8u) {
        cols = max_u32(width / 8u, 1u);
    }
    if (height >= 16u) {
        rows = max_u32(height / 16u, 1u);
    }

    if (out_cols) {
        *out_cols = cols;
    }
    if (out_rows) {
        *out_rows = rows;
    }
}

static void ensure_view(uint32_t edit_cols, uint32_t edit_rows) {
    uint32_t line = 0u;
    uint32_t col = 0u;

    if (edit_cols == 0u) {
        edit_cols = 1u;
    }
    if (edit_rows == 0u) {
        edit_rows = 1u;
    }

    cursor_line_col(&line, &col);

    if (line < g_view_top_line) {
        g_view_top_line = line;
    } else if (line >= g_view_top_line + edit_rows) {
        g_view_top_line = line - edit_rows + 1u;
    }

    if (col < g_view_left_col) {
        g_view_left_col = col;
    } else if (col >= g_view_left_col + edit_cols) {
        g_view_left_col = col - edit_cols + 1u;
    }
}

static void render_bar(const char* color_seq, const char* text, uint32_t width, uint8_t add_newline) {
    uint32_t i = 0u;

    mya_puts(color_seq);
    while (text && text[i] && i < width) {
        put_char(text[i]);
        i++;
    }
    if (i < width) {
        put_spaces(width - i);
    }
    mya_puts("\x1b[0m");
    if (add_newline) {
        mya_puts("\n");
    }
}

static void render_text_row(uint32_t line_start, uint32_t line_end, uint32_t cursor_col, uint32_t cols) {
    uint32_t line_len = (line_end >= line_start) ? (line_end - line_start) : 0u;

    for (uint32_t col = 0u; col < cols; col++) {
        uint32_t logical_col = g_view_left_col + col;
        char out = ' ';

        if (logical_col < line_len) {
            uint8_t in = g_text[line_start + logical_col];
            if (in >= 32u && in <= 126u) {
                out = (char)in;
            } else if (in == '\t') {
                out = '>';
            } else {
                out = '?';
            }
        }

        if (logical_col == cursor_col) {
            mya_puts("\x1b[30;47m");
            put_char(out);
            mya_puts("\x1b[0m");
        } else {
            put_char(out);
        }
    }
}

static void render_editor(const char* path) {
    uint32_t cols = NANO_FALLBACK_COLS;
    uint32_t draw_cols;
    uint32_t rows = NANO_FALLBACK_ROWS;
    uint32_t bottom_rows;
    uint32_t edit_rows;
    uint32_t line = 0u;
    uint32_t col = 0u;
    uint32_t lines;
    uint32_t draw_line;
    uint32_t draw_start;
    char title[NANO_LINEBUF_MAX];
    char status[NANO_LINEBUF_MAX];
    char help[NANO_LINEBUF_MAX];
    uint32_t pos;

    detect_console_size(&cols, &rows);
    draw_cols = (cols > 1u) ? (cols - 1u) : 1u;
    if (rows >= 4u) {
        bottom_rows = 2u;
    } else if (rows == 3u) {
        bottom_rows = 1u;
    } else {
        bottom_rows = 0u;
    }
    if (rows > 1u + bottom_rows) {
        edit_rows = rows - 1u - bottom_rows;
    } else {
        edit_rows = 0u;
    }

    ensure_view(draw_cols, (edit_rows > 0u) ? edit_rows : 1u);
    cursor_line_col(&line, &col);
    lines = total_lines();

    if (!g_screen_size_known || cols != g_last_cols || rows != g_last_rows) {
        mya_puts("\x1b[2J");
        g_last_cols = cols;
        g_last_rows = rows;
        g_screen_size_known = 1u;
    }
    mya_puts("\x1b[H");

    pos = 0u;
    title[0] = '\0';
    (void)append_text(title, sizeof(title), &pos, " GNU nano  ");
    (void)append_text(title, sizeof(title), &pos, path);
    if (g_modified) {
        (void)append_text(title, sizeof(title), &pos, "  [modified]");
    }
    if (g_quit_armed) {
        (void)append_text(title, sizeof(title), &pos, "  [press ^X again]");
    }
    render_bar("\x1b[44;97m", title, draw_cols, (rows > 1u) ? 1u : 0u);

    draw_line = g_view_top_line;
    draw_start = line_offset(draw_line);
    for (uint32_t r = 0u; r < edit_rows; r++) {
        if (draw_line >= lines) {
            mya_puts("\x1b[90m~\x1b[0m");
            if (draw_cols > 1u) {
                put_spaces(draw_cols - 1u);
            }
            if (r + 1u < edit_rows || bottom_rows > 0u) {
                mya_puts("\n");
            }
            continue;
        }

        {
            uint32_t end = line_end_of(draw_start);
            uint32_t cursor_col_for_line = (draw_line == line) ? col : 0xFFFFFFFFu;
            render_text_row(draw_start, end, cursor_col_for_line, draw_cols);
            if (r + 1u < edit_rows || bottom_rows > 0u) {
                mya_puts("\n");
            }

            if (end < g_len && g_text[end] == '\n') {
                draw_start = end + 1u;
            } else {
                draw_start = end;
            }
            draw_line++;
        }
    }

    if (bottom_rows >= 1u) {
        pos = 0u;
        status[0] = '\0';
        (void)append_text(status, sizeof(status), &pos, " Ln ");
        (void)append_u32(status, sizeof(status), &pos, line + 1u);
        (void)append_text(status, sizeof(status), &pos, "/");
        (void)append_u32(status, sizeof(status), &pos, lines);
        (void)append_text(status, sizeof(status), &pos, "  Col ");
        (void)append_u32(status, sizeof(status), &pos, col + 1u);
        (void)append_text(status, sizeof(status), &pos, "  Pos ");
        (void)append_u32(status, sizeof(status), &pos, g_cursor);
        (void)append_text(status, sizeof(status), &pos, "/");
        (void)append_u32(status, sizeof(status), &pos, g_len);
        if (g_status[0]) {
            (void)append_text(status, sizeof(status), &pos, "  | ");
            (void)append_text(status, sizeof(status), &pos, g_status);
        }
        render_bar("\x1b[100;97m", status, draw_cols, (bottom_rows >= 2u) ? 1u : 0u);
    }

    if (bottom_rows >= 2u) {
        pos = 0u;
        help[0] = '\0';
        (void)append_text(help, sizeof(help), &pos, " ^O Write Out   ^X Exit   Arrows Move   Backspace/Delete Edit ");
        render_bar("\x1b[46;30m", help, draw_cols, 0u);
    }
}

static void clear_quit_prompt(void) {
    if (g_quit_armed) {
        g_quit_armed = 0u;
        if (g_status[0]) {
            set_status("");
        }
    }
}

static int insert_byte(uint8_t byte) {
    if (g_len >= NANO_FILE_MAX) {
        return -1;
    }
    if (g_cursor < g_len) {
        mem_move_u8(g_text + g_cursor + 1u, g_text + g_cursor, g_len - g_cursor);
    }
    g_text[g_cursor] = byte;
    g_cursor++;
    g_len++;
    g_modified = 1u;
    return 0;
}

static int delete_backward(void) {
    if (g_cursor == 0u) {
        return -1;
    }
    if (g_cursor < g_len) {
        mem_move_u8(g_text + g_cursor - 1u, g_text + g_cursor, g_len - g_cursor);
    }
    g_cursor--;
    g_len--;
    g_modified = 1u;
    return 0;
}

static int delete_forward(void) {
    if (g_cursor >= g_len) {
        return -1;
    }
    if (g_cursor + 1u < g_len) {
        mem_move_u8(g_text + g_cursor, g_text + g_cursor + 1u, g_len - g_cursor - 1u);
    }
    g_len--;
    g_modified = 1u;
    return 0;
}

static void move_left(void) {
    if (g_cursor > 0u) {
        g_cursor--;
    }
}

static void move_right(void) {
    if (g_cursor < g_len) {
        g_cursor++;
    }
}

static void move_up(void) {
    uint32_t cur_start = line_start_of(g_cursor);
    uint32_t cur_col = g_cursor - cur_start;
    uint32_t prev_end;
    uint32_t prev_start;
    uint32_t prev_len;

    if (cur_start == 0u) {
        return;
    }
    if (!g_keep_desired_col) {
        g_desired_col = cur_col;
    }

    prev_end = cur_start - 1u;
    prev_start = line_start_of(prev_end);
    prev_len = prev_end - prev_start;
    g_cursor = prev_start + min_u32(g_desired_col, prev_len);
    g_keep_desired_col = 1u;
}

static void move_down(void) {
    uint32_t cur_start = line_start_of(g_cursor);
    uint32_t cur_col = g_cursor - cur_start;
    uint32_t cur_end = line_end_of(cur_start);
    uint32_t next_start;
    uint32_t next_end;
    uint32_t next_len;

    if (cur_end >= g_len) {
        return;
    }
    if (!g_keep_desired_col) {
        g_desired_col = cur_col;
    }

    next_start = cur_end + 1u;
    next_end = line_end_of(next_start);
    next_len = next_end - next_start;
    g_cursor = next_start + min_u32(g_desired_col, next_len);
    g_keep_desired_col = 1u;
}

static int save_file(const char* path) {
    char status[NANO_STATUS_MAX];
    uint32_t pos = 0u;

    if (mya_fs_write(path, g_text, g_len) != 0) {
        set_status("Write failed");
        return -1;
    }

    g_modified = 0u;
    g_quit_armed = 0u;

    status[0] = '\0';
    (void)append_text(status, sizeof(status), &pos, "Wrote ");
    (void)append_u32(status, sizeof(status), &pos, g_len);
    (void)append_text(status, sizeof(status), &pos, " bytes");
    set_status(status);
    return 0;
}

static int handle_ctrl_shortcut(const myaos_input_key_event_t* ev, const char* path, int* running, int* exit_code) {
    if (!ev || !running || !exit_code) {
        return 0;
    }
    if (ev->action != MYAOS_INPUT_KEY_EVENT_PRESS) {
        return 0;
    }
    if ((ev->modifiers & MYAOS_INPUT_MOD_CTRL) == 0u) {
        return 0;
    }

    if (ev->keycode == NANO_KEY_O) {
        (void)save_file(path);
        return 1;
    }

    if (ev->keycode == NANO_KEY_X) {
        if (g_modified && !g_quit_armed) {
            g_quit_armed = 1u;
            set_status("Unsaved changes. Press Ctrl+X again to quit or Ctrl+O to save.");
            return 1;
        }
        *running = 0;
        *exit_code = 0;
        return 1;
    }

    return 0;
}

static int handle_key_event(const myaos_input_key_event_t* ev, const char* path, int* running, int* exit_code) {
    if (!ev || !path || !running || !exit_code) {
        return 0;
    }
    if (ev->action != MYAOS_INPUT_KEY_EVENT_PRESS && ev->action != MYAOS_INPUT_KEY_EVENT_REPEAT) {
        return 0;
    }

    if (handle_ctrl_shortcut(ev, path, running, exit_code) != 0) {
        return 1;
    }

    if (ev->keycode == NANO_KEY_ESC && ev->action == MYAOS_INPUT_KEY_EVENT_PRESS) {
        if (g_modified && !g_quit_armed) {
            g_quit_armed = 1u;
            set_status("Unsaved changes. Press Esc again to quit or Ctrl+O to save.");
            return 1;
        }
        *running = 0;
        *exit_code = 0;
        return 1;
    }

    clear_quit_prompt();

    if (ev->keycode == NANO_KEY_LEFT) {
        move_left();
        g_keep_desired_col = 0u;
        return 1;
    }
    if (ev->keycode == NANO_KEY_RIGHT) {
        move_right();
        g_keep_desired_col = 0u;
        return 1;
    }
    if (ev->keycode == NANO_KEY_UP) {
        move_up();
        return 1;
    }
    if (ev->keycode == NANO_KEY_DOWN) {
        move_down();
        return 1;
    }
    if (ev->keycode == NANO_KEY_BACKSPACE || ev->ascii == (uint32_t)'\b') {
        if (delete_backward() != 0) {
            set_status("Start of buffer");
        }
        g_keep_desired_col = 0u;
        return 1;
    }
    if (ev->keycode == NANO_KEY_DELETE) {
        if (delete_forward() != 0) {
            set_status("End of buffer");
        }
        g_keep_desired_col = 0u;
        return 1;
    }

    if ((ev->modifiers & (MYAOS_INPUT_MOD_CTRL | MYAOS_INPUT_MOD_ALT | MYAOS_INPUT_MOD_GUI)) != 0u) {
        return 0;
    }

    if (ev->ascii == (uint32_t)'\n' || ev->keycode == NANO_KEY_ENTER) {
        if (insert_byte((uint8_t)'\n') != 0) {
            set_status("Buffer full");
        }
        g_keep_desired_col = 0u;
        return 1;
    }
    if (ev->ascii == (uint32_t)'\t') {
        if (insert_byte((uint8_t)'\t') != 0) {
            set_status("Buffer full");
        }
        g_keep_desired_col = 0u;
        return 1;
    }
    if (ev->ascii >= 32u && ev->ascii <= 126u) {
        if (insert_byte((uint8_t)ev->ascii) != 0) {
            set_status("Buffer full");
        }
        g_keep_desired_col = 0u;
        return 1;
    }

    return 0;
}

int program_main(int argc, char** argv) {
    const char* path;
    uint32_t read_size = 0u;
    int running = 1;
    int exit_code = 0;

    if (argc < 2 || !argv || !argv[1] || !argv[1][0]) {
        mya_putln("usage: nano <file>");
        return 1;
    }

    path = argv[1];
    g_len = 0u;
    g_cursor = 0u;
    g_desired_col = 0u;
    g_view_top_line = 0u;
    g_view_left_col = 0u;
    g_keep_desired_col = 0u;
    g_modified = 0u;
    g_quit_armed = 0u;
    set_status("");

    if (mya_fs_read(path, g_text, sizeof(g_text), &read_size) != 0) {
        g_len = 0u;
        set_status("New file");
    } else {
        g_len = read_size;
        if (g_len == sizeof(g_text)) {
            set_status("Opened (possibly truncated to internal limit)");
        } else {
            set_status("Opened");
        }
    }

    render_editor(path);

    while (running) {
        uint32_t events = 0u;
        int wait_rc = mya_input_wait(100u, &events);

        if (wait_rc != 0 || (events & MYAOS_EVENT_INPUT_KEYBOARD) == 0u) {
            continue;
        }

        for (;;) {
            myaos_input_key_event_t ev;
            int ev_rc = mya_input_key_event_read(&ev);
            if (ev_rc <= 0) {
                break;
            }
            if (handle_key_event(&ev, path, &running, &exit_code) != 0) {
                render_editor(path);
            }
            if (!running) {
                break;
            }
        }
    }

    (void)mya_console_clear();
    return exit_code;
}
