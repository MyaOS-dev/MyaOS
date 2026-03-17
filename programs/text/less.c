#include "libc.h"
#include <stdint.h>

#define LESS_FILE_MAX (256u * 1024u)
#define LESS_MAX_LINES 32768u
#define LESS_PAGE_LINES 22u

typedef struct {
    uint32_t line_count;
    uint8_t line_index_truncated;
} less_index_result_t;

static uint8_t g_file_buf[LESS_FILE_MAX];
static uint32_t g_line_offsets[LESS_MAX_LINES];

static void console_write_len(const uint8_t* text, uint32_t size) {
    if (!text || size == 0u) {
        return;
    }
    (void)mya_syscall(MYAOS_SYS_CONSOLE_WRITE, (uint64_t)(uintptr_t)text, size, 0, 0, 0);
}

static uint32_t line_end_offset(const uint8_t* buf, uint32_t size, uint32_t start) {
    uint32_t end = start;
    while (end < size && buf[end] != '\n' && buf[end] != '\r') {
        end++;
    }
    return end;
}

static less_index_result_t build_line_index(const uint8_t* buf, uint32_t size, uint32_t* line_offsets, uint32_t max_lines) {
    less_index_result_t result;
    uint32_t i = 0;

    result.line_count = 0u;
    result.line_index_truncated = 0u;

    if (!buf || !line_offsets || max_lines == 0u || size == 0u) {
        return result;
    }

    line_offsets[result.line_count++] = 0u;

    while (i < size) {
        if (buf[i] == '\r' || buf[i] == '\n') {
            uint32_t next = i + 1u;
            if (buf[i] == '\r' && next < size && buf[next] == '\n') {
                next++;
            }

            if (next < size) {
                if (result.line_count < max_lines) {
                    line_offsets[result.line_count++] = next;
                } else {
                    result.line_index_truncated = 1u;
                }
            }
            i = next;
            continue;
        }
        i++;
    }

    return result;
}

static uint32_t clamp_top_line(uint32_t top_line, uint32_t line_count) {
    uint32_t max_top = 0u;

    if (line_count > LESS_PAGE_LINES) {
        max_top = line_count - LESS_PAGE_LINES;
    }
    if (top_line > max_top) {
        top_line = max_top;
    }
    return top_line;
}

static void print_status(
    const char* path,
    uint32_t top_line,
    uint32_t line_count,
    uint8_t file_maybe_truncated,
    uint8_t line_index_truncated
) {
    mya_puts("-- less ");
    mya_puts(path);
    mya_puts(" ");
    if (line_count == 0u) {
        mya_puts("(empty)");
    } else {
        mya_puts("line ");
        mya_put_u32(top_line + 1u);
        mya_puts("/");
        mya_put_u32(line_count);
    }
    if (file_maybe_truncated) {
        mya_puts(" [file-maybe-truncated]");
    }
    if (line_index_truncated) {
        mya_puts(" [line-index-truncated]");
    }
    mya_puts("  keys: j/k/space/b/g/G/q --\n");
}

static void render_page(
    const char* path,
    const uint8_t* buf,
    uint32_t size,
    const uint32_t* line_offsets,
    uint32_t line_count,
    uint32_t top_line,
    uint8_t file_maybe_truncated,
    uint8_t line_index_truncated
) {
    mya_console_clear();

    if (line_count == 0u) {
        mya_putln("(empty file)");
        print_status(path, 0u, line_count, file_maybe_truncated, line_index_truncated);
        return;
    }

    for (uint32_t row = 0; row < LESS_PAGE_LINES; row++) {
        uint32_t line_index = top_line + row;
        uint32_t line_start;
        uint32_t line_end;

        if (line_index >= line_count) {
            break;
        }
        line_start = line_offsets[line_index];
        if (line_start >= size) {
            break;
        }
        line_end = line_end_offset(buf, size, line_start);
        if (line_end > line_start) {
            console_write_len(buf + line_start, line_end - line_start);
        }
        mya_puts("\n");
    }

    print_status(path, top_line, line_count, file_maybe_truncated, line_index_truncated);
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

static void show_help(void) {
    mya_console_clear();
    mya_putln("less - pager");
    mya_putln("controls:");
    mya_putln("  j or Enter : one line down");
    mya_putln("  k          : one line up");
    mya_putln("  space      : one page down");
    mya_putln("  b          : one page up");
    mya_putln("  g          : go to start");
    mya_putln("  G          : go to end");
    mya_putln("  q          : quit");
    mya_putln("");
    mya_putln("press any key to continue...");
    (void)wait_key();
}

int program_main(int argc, char** argv) {
    uint32_t size = 0u;
    uint32_t top_line = 0u;
    uint32_t line_count = 0u;
    uint8_t file_maybe_truncated = 0u;
    uint8_t line_index_truncated = 0u;
    less_index_result_t index_result;

    if (argc < 2) {
        mya_putln("usage: less <file>");
        return 1;
    }

    if (mya_fs_read(argv[1], g_file_buf, LESS_FILE_MAX, &size) != 0) {
        mya_putln("less: read failed");
        return 1;
    }

    if (size == LESS_FILE_MAX) {
        file_maybe_truncated = 1u;
    }

    index_result = build_line_index(g_file_buf, size, g_line_offsets, LESS_MAX_LINES);
    line_count = index_result.line_count;
    line_index_truncated = index_result.line_index_truncated;

    for (;;) {
        int ch;
        uint32_t max_top = 0u;

        top_line = clamp_top_line(top_line, line_count);
        render_page(
            argv[1],
            g_file_buf,
            size,
            g_line_offsets,
            line_count,
            top_line,
            file_maybe_truncated,
            line_index_truncated
        );

        ch = wait_key();
        if (ch == 'q' || ch == 'Q') {
            return 0;
        }
        if (ch == 0x03) {
            return 130;
        }
        if (line_count > LESS_PAGE_LINES) {
            max_top = line_count - LESS_PAGE_LINES;
        }

        if (ch == ' ' || ch == 'f' || ch == 'F') {
            top_line += LESS_PAGE_LINES;
        } else if (ch == 'b' || ch == 'B') {
            if (top_line > LESS_PAGE_LINES) {
                top_line -= LESS_PAGE_LINES;
            } else {
                top_line = 0u;
            }
        } else if (ch == 'j' || ch == 'J' || ch == '\n' || ch == '\r') {
            if (top_line < max_top) {
                top_line++;
            }
        } else if (ch == 'k' || ch == 'K') {
            if (top_line > 0u) {
                top_line--;
            }
        } else if (ch == 'g') {
            top_line = 0u;
        } else if (ch == 'G') {
            top_line = max_top;
        } else if (ch == 'h' || ch == 'H' || ch == '?') {
            show_help();
        }
    }
}
