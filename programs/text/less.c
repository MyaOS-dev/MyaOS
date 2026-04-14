#include "libc.h"
#include <stdint.h>

#define LESS_FILE_MAX (256u * 1024u)
#define LESS_MAX_LINES 32768u
#define LESS_PAGE_LINES 22u
#define LESS_SEARCH_MAX 96u

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

static char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int line_contains_ci(const uint8_t* text, uint32_t len, const char* needle) {
    uint32_t needle_len = 0u;

    if (!needle || !needle[0]) {
        return 1;
    }
    while (needle[needle_len]) {
        needle_len++;
    }
    if (needle_len > len) {
        return 0;
    }

    for (uint32_t i = 0; i + needle_len <= len; i++) {
        uint32_t j = 0u;
        while (j < needle_len &&
               to_lower_char((char)text[i + j]) == to_lower_char(needle[j])) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static uint32_t find_next_match(
    const uint8_t* buf,
    uint32_t size,
    const uint32_t* line_offsets,
    uint32_t line_count,
    uint32_t start_line,
    const char* pattern
) {
    if (!pattern || !pattern[0] || !buf || !line_offsets || line_count == 0u) {
        return line_count;
    }

    for (uint32_t i = start_line; i < line_count; i++) {
        uint32_t line_start = line_offsets[i];
        uint32_t line_end = line_end_offset(buf, size, line_start);
        if (line_end >= line_start && line_contains_ci(buf + line_start, line_end - line_start, pattern)) {
            return i;
        }
    }
    return line_count;
}

static uint32_t find_prev_match(
    const uint8_t* buf,
    uint32_t size,
    const uint32_t* line_offsets,
    uint32_t line_count,
    uint32_t start_line,
    const char* pattern
) {
    if (!pattern || !pattern[0] || !buf || !line_offsets || line_count == 0u) {
        return line_count;
    }

    if (start_line >= line_count) {
        start_line = line_count - 1u;
    }

    for (uint32_t i = start_line + 1u; i > 0u; i--) {
        uint32_t idx = i - 1u;
        uint32_t line_start = line_offsets[idx];
        uint32_t line_end = line_end_offset(buf, size, line_start);
        if (line_end >= line_start && line_contains_ci(buf + line_start, line_end - line_start, pattern)) {
            return idx;
        }
    }
    return line_count;
}

static void print_status(
    const char* path,
    uint32_t top_line,
    uint32_t line_count,
    uint8_t file_maybe_truncated,
    uint8_t line_index_truncated,
    const char* search_pattern,
    uint8_t search_not_found
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
    if (search_pattern && search_pattern[0]) {
        mya_puts(" [/");
        mya_puts(search_pattern);
        if (search_not_found) {
            mya_puts(":not-found");
        }
        mya_puts("]");
    }
    mya_puts("  keys: j/k/space/b/g/G//n/N/h/q --\n");
}

static void render_page(
    const char* path,
    const uint8_t* buf,
    uint32_t size,
    const uint32_t* line_offsets,
    uint32_t line_count,
    uint32_t top_line,
    uint8_t file_maybe_truncated,
    uint8_t line_index_truncated,
    const char* search_pattern,
    uint8_t search_not_found
) {
    mya_console_clear();

    if (line_count == 0u) {
        mya_putln("(empty file)");
        print_status(path, 0u, line_count, file_maybe_truncated, line_index_truncated, search_pattern, search_not_found);
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

    print_status(path, top_line, line_count, file_maybe_truncated, line_index_truncated, search_pattern, search_not_found);
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

static int next_key(const char** key_script) {
    if (key_script && *key_script) {
        const char* script = *key_script;
        if (script[0] != '\0') {
            int ch = (unsigned char)script[0];
            *key_script = script + 1;
            return ch;
        }
    }
    return wait_key();
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
    mya_putln("  /text      : search forward");
    mya_putln("  n          : next search result");
    mya_putln("  N          : previous search result");
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
    uint8_t search_not_found = 0u;
    less_index_result_t index_result;
    const char* key_script = NULL;
    char path_label[64];
    char search_pattern[LESS_SEARCH_MAX];

    path_label[0] = '\0';
    search_pattern[0] = '\0';

    for (int i = 1; i < argc; i++) {
        if (mya_starts_with(argv[i], "--keys=")) {
            key_script = argv[i] + 7;
        } else if (path_label[0] == '\0') {
            uint32_t p = 0u;
            while (argv[i][p] && p + 1u < sizeof(path_label)) {
                path_label[p] = argv[i][p];
                p++;
            }
            path_label[p] = '\0';
        } else {
            mya_putln("less: unknown option");
            mya_putln("usage: less <file> [--keys=<key_script>]");
            return 1;
        }
    }

    if (path_label[0] == '\0') {
        int32_t stdin_read = 0;
        uint32_t total = 0u;
        for (;;) {
            uint32_t got = 0u;
            stdin_read = mya_posix_read(0, g_file_buf + total, LESS_FILE_MAX - total, &got);
            if (stdin_read != 0 || got == 0u) {
                break;
            }
            total += got;
            if (total >= LESS_FILE_MAX) {
                file_maybe_truncated = 1u;
                break;
            }
        }
        if (total == 0u) {
            mya_putln("usage: less <file> [--keys=<key_script>]");
            return 1;
        }
        size = total;
        path_label[0] = '(';
        path_label[1] = 's';
        path_label[2] = 't';
        path_label[3] = 'd';
        path_label[4] = 'i';
        path_label[5] = 'n';
        path_label[6] = ')';
        path_label[7] = '\0';
    } else {
        if (mya_fs_read(path_label, g_file_buf, LESS_FILE_MAX, &size) != 0) {
            mya_putln("less: read failed");
            return 1;
        }
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
            path_label,
            g_file_buf,
            size,
            g_line_offsets,
            line_count,
            top_line,
            file_maybe_truncated,
            line_index_truncated,
            search_pattern,
            search_not_found
        );

        if (key_script && key_script[0] == '\0') {
            return 0;
        }
        ch = next_key(&key_script);
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
        } else if (ch == '/') {
            char pattern[LESS_SEARCH_MAX];
            int n;

            mya_puts("/");
            n = mya_console_readline(pattern, sizeof(pattern), 1u);
            if (n >= 0 && pattern[0]) {
                uint32_t hit = find_next_match(
                    g_file_buf,
                    size,
                    g_line_offsets,
                    line_count,
                    (top_line < line_count) ? (top_line + 1u) : 0u,
                    pattern
                );
                uint32_t wrap_hit = line_count;
                for (uint32_t i = 0; pattern[i] && i + 1u < sizeof(search_pattern); i++) {
                    search_pattern[i] = pattern[i];
                    search_pattern[i + 1u] = '\0';
                }
                if (search_pattern[0] != '\0' && hit == line_count) {
                    wrap_hit = find_next_match(g_file_buf, size, g_line_offsets, line_count, 0u, search_pattern);
                }
                if (hit != line_count) {
                    top_line = hit;
                    search_not_found = 0u;
                } else if (wrap_hit != line_count) {
                    top_line = wrap_hit;
                    search_not_found = 0u;
                } else {
                    search_not_found = 1u;
                }
            }
        } else if ((ch == 'n' || ch == 'N') && search_pattern[0]) {
            uint32_t hit = line_count;

            if (ch == 'n') {
                uint32_t start = (top_line + 1u < line_count) ? (top_line + 1u) : 0u;
                hit = find_next_match(g_file_buf, size, g_line_offsets, line_count, start, search_pattern);
                if (hit == line_count && start != 0u) {
                    hit = find_next_match(g_file_buf, size, g_line_offsets, line_count, 0u, search_pattern);
                }
            } else {
                uint32_t start = (top_line > 0u) ? (top_line - 1u) : (line_count > 0u ? line_count - 1u : 0u);
                hit = find_prev_match(g_file_buf, size, g_line_offsets, line_count, start, search_pattern);
                if (hit == line_count && line_count > 0u) {
                    hit = find_prev_match(g_file_buf, size, g_line_offsets, line_count, line_count - 1u, search_pattern);
                }
            }

            if (hit != line_count) {
                top_line = hit;
                search_not_found = 0u;
            } else {
                search_not_found = 1u;
            }
        }
    }
}
