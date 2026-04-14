#include "../lib/myaos.h"
#include <stdint.h>

#define EDIT_FILE_MAX (128u * 1024u)
#define EDIT_LINE_MAX 256u

static uint8_t g_file[EDIT_FILE_MAX];
static uint8_t g_syntax_mode = 0u; /* 0=off, 1=c */

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

static int str_contains(const char* text, const char* needle) {
    uint32_t tlen = 0u;
    uint32_t nlen = 0u;

    if (!needle || !needle[0]) {
        return 1;
    }
    if (!text) {
        return 0;
    }
    while (text[tlen]) {
        tlen++;
    }
    while (needle[nlen]) {
        nlen++;
    }
    if (nlen > tlen) {
        return 0;
    }
    for (uint32_t i = 0; i + nlen <= tlen; i++) {
        uint32_t j = 0u;
        while (j < nlen && text[i + j] == needle[j]) {
            j++;
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

static int append_text(uint8_t* out, uint32_t out_size, uint32_t* io_pos, const char* text) {
    uint32_t pos;

    if (!out || !io_pos || !text || out_size == 0u) {
        return -1;
    }

    pos = *io_pos;
    for (uint32_t i = 0; text[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = (uint8_t)text[i];
    }

    out[pos] = '\0';
    *io_pos = pos;
    return 0;
}

static void show_help(void) {
    mya_putln("edit controls:");
    mya_putln("  type text and press Enter to append line");
    mya_putln("  .w      save and exit");
    mya_putln("  .q      quit without save");
    mya_putln("  .clear  clear current buffer");
    mya_putln("  .find X search text in buffer");
    mya_putln("  .print  show buffer (with syntax markers)");
    mya_putln("  .syntax off|c  toggle basic syntax highlighting");
}

static int is_word_char(char c) {
    return ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_') ? 1 : 0;
}

static int is_c_keyword(const char* tok, uint32_t len) {
    static const char* kw[] = {
        "if", "else", "for", "while", "return", "switch", "case", "break",
        "continue", "struct", "typedef", "enum", "static", "const", "void",
        "int", "char", "long", "short", "unsigned", "signed", "uint32_t", "uint64_t"
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(kw) / sizeof(kw[0])); i++) {
        uint32_t j = 0u;
        while (kw[i][j]) {
            j++;
        }
        if (j != len) {
            continue;
        }
        if (str_starts_with(tok, kw[i])) {
            return 1;
        }
    }
    return 0;
}

static void print_line_highlighted(const char* line, uint32_t len) {
    uint32_t i = 0u;

    if (g_syntax_mode != 1u) {
        for (i = 0u; i < len; i++) {
            char ch[2] = { line[i], '\0' };
            mya_puts(ch);
        }
        mya_puts("\n");
        return;
    }

    while (i < len) {
        if (is_word_char(line[i])) {
            uint32_t start = i;
            uint32_t tok_len;
            while (i < len && is_word_char(line[i])) {
                i++;
            }
            tok_len = i - start;
            if (is_c_keyword(line + start, tok_len)) {
                mya_puts("[");
            }
            for (uint32_t j = 0u; j < tok_len; j++) {
                char ch[2] = { line[start + j], '\0' };
                mya_puts(ch);
            }
            if (is_c_keyword(line + start, tok_len)) {
                mya_puts("]");
            }
            continue;
        }
        {
            char ch[2] = { line[i], '\0' };
            mya_puts(ch);
        }
        i++;
    }
    mya_puts("\n");
}

static void print_buffer_with_lines(const uint8_t* buf, uint32_t size) {
    uint32_t line_no = 1u;
    uint32_t line_start = 0u;

    if (size == 0u) {
        mya_putln("(empty)");
        return;
    }

    for (uint32_t i = 0u; i <= size; i++) {
        if (i == size || buf[i] == '\n') {
            char num[16];
            uint32_t line_len = (i >= line_start) ? (i - line_start) : 0u;
            mya_u32_to_dec(line_no, num, sizeof(num));
            mya_puts(num);
            mya_puts(": ");
            print_line_highlighted((const char*)(buf + line_start), line_len);
            line_no++;
            line_start = i + 1u;
        }
    }
}

static void search_buffer(const uint8_t* buf, uint32_t size, const char* needle) {
    uint32_t line_no = 1u;
    uint32_t line_start = 0u;
    uint32_t found = 0u;

    if (!needle || !needle[0]) {
        mya_putln("edit: empty search pattern");
        return;
    }

    for (uint32_t i = 0u; i <= size; i++) {
        if (i == size || buf[i] == '\n') {
            uint32_t line_len = (i >= line_start) ? (i - line_start) : 0u;
            char line[EDIT_LINE_MAX];
            uint32_t copy_len = line_len;
            if (copy_len >= sizeof(line)) {
                copy_len = sizeof(line) - 1u;
            }
            for (uint32_t j = 0u; j < copy_len; j++) {
                line[j] = (char)buf[line_start + j];
            }
            line[copy_len] = '\0';

            if (str_contains(line, needle)) {
                char num[16];
                mya_u32_to_dec(line_no, num, sizeof(num));
                mya_puts(num);
                mya_puts(": ");
                mya_putln(line);
                found++;
            }

            line_no++;
            line_start = i + 1u;
        }
    }

    if (found == 0u) {
        mya_putln("edit: no matches");
    }
}

int program_main(int argc, char** argv) {
    uint32_t size = 0;
    char line[EDIT_LINE_MAX];

    if (argc < 2) {
        mya_putln("usage: edit <file>");
        return 1;
    }

    if (mya_fs_read(argv[1], g_file, sizeof(g_file) - 1u, &size) != 0) {
        size = 0u;
        g_file[0] = '\0';
        mya_putln("edit: file does not exist yet, new file buffer");
    } else {
        g_file[size] = '\0';
        mya_puts("edit: opened ");
        mya_puts(argv[1]);
        mya_puts(" bytes=");
        mya_put_u32(size);
        mya_puts("\n");
        if (size > 0u) {
            mya_putln("----- current content -----");
            mya_puts((const char*)g_file);
            if (size > 0u && g_file[size - 1u] != '\n') {
                mya_puts("\n");
            }
            mya_putln("----- end current content -----");
        }
    }

    show_help();
    for (;;) {
        int n;

        mya_puts("edit> ");
        n = mya_console_readline(line, sizeof(line), 1u);
        if (n < 0) {
            continue;
        }

        if (str_eq(line, ".q")) {
            mya_putln("edit: quit without save");
            return 0;
        }
        if (str_eq(line, ".w")) {
            if (mya_fs_write(argv[1], g_file, size) != 0) {
                mya_puts("edit: failed to save ");
                mya_putln(argv[1]);
                return 1;
            }
            mya_puts("edit: saved ");
            mya_putln(argv[1]);
            return 0;
        }
        if (str_eq(line, ".clear")) {
            size = 0u;
            g_file[0] = '\0';
            mya_putln("edit: buffer cleared");
            continue;
        }
        if (str_eq(line, ".print")) {
            print_buffer_with_lines(g_file, size);
            continue;
        }
        if (str_starts_with(line, ".find ")) {
            search_buffer(g_file, size, line + 6);
            continue;
        }
        if (str_eq(line, ".syntax off")) {
            g_syntax_mode = 0u;
            mya_putln("edit: syntax off");
            continue;
        }
        if (str_eq(line, ".syntax c")) {
            g_syntax_mode = 1u;
            mya_putln("edit: syntax c");
            continue;
        }

        if (append_text(g_file, sizeof(g_file), &size, line) != 0 ||
            append_text(g_file, sizeof(g_file), &size, "\n") != 0) {
            mya_putln("edit: buffer full, use .w or .q");
        }
    }
}
