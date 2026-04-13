#include "../lib/myaos.h"
#include <stdint.h>

typedef struct {
    uint8_t use_default;
    uint8_t bright;
    uint8_t index;
} color_spec_t;

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int str_eq_ci(const char* a, const char* b) {
    uint32_t i = 0u;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (to_lower_ascii(a[i]) != to_lower_ascii(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int str_starts_with_ci(const char* text, const char* prefix) {
    uint32_t i = 0u;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (!text[i] || to_lower_ascii(text[i]) != to_lower_ascii(prefix[i])) {
            return 0;
        }
        i++;
    }
    return 1;
}

static uint8_t str_len(const char* s) {
    uint8_t n = 0u;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int parse_base_color(const char* name, uint8_t* out_index) {
    if (!name || !out_index) {
        return -1;
    }

    if (str_eq_ci(name, "black")) {
        *out_index = 0u;
        return 0;
    }
    if (str_eq_ci(name, "red")) {
        *out_index = 1u;
        return 0;
    }
    if (str_eq_ci(name, "green")) {
        *out_index = 2u;
        return 0;
    }
    if (str_eq_ci(name, "yellow")) {
        *out_index = 3u;
        return 0;
    }
    if (str_eq_ci(name, "blue")) {
        *out_index = 4u;
        return 0;
    }
    if (str_eq_ci(name, "magenta")) {
        *out_index = 5u;
        return 0;
    }
    if (str_eq_ci(name, "cyan")) {
        *out_index = 6u;
        return 0;
    }
    if (str_eq_ci(name, "white")) {
        *out_index = 7u;
        return 0;
    }
    return -1;
}

static int parse_color_spec(const char* text, color_spec_t* out_spec) {
    const char* base = text;
    uint8_t prefix_len = 0u;
    uint8_t idx = 0u;

    if (!text || !out_spec) {
        return -1;
    }

    out_spec->use_default = 0u;
    out_spec->bright = 0u;
    out_spec->index = 7u;

    if (str_eq_ci(text, "default")) {
        out_spec->use_default = 1u;
        return 0;
    }
    if (str_starts_with_ci(text, "bright-")) {
        out_spec->bright = 1u;
        prefix_len = (uint8_t)str_len("bright-");
    } else if (str_starts_with_ci(text, "light-")) {
        out_spec->bright = 1u;
        prefix_len = (uint8_t)str_len("light-");
    }

    base += prefix_len;
    if (parse_base_color(base, &idx) != 0) {
        return -1;
    }
    out_spec->index = idx;
    return 0;
}

static uint32_t fg_code(const color_spec_t* spec) {
    if (spec->use_default) {
        return 39u;
    }
    return spec->bright ? (uint32_t)(90u + spec->index) : (uint32_t)(30u + spec->index);
}

static uint32_t bg_code(const color_spec_t* spec) {
    if (spec->use_default) {
        return 49u;
    }
    return spec->bright ? (uint32_t)(100u + spec->index) : (uint32_t)(40u + spec->index);
}

static void emit_sgr1(uint32_t code0) {
    mya_puts("\x1b[");
    mya_put_u32(code0);
    mya_puts("m");
}

static void emit_sgr2(uint32_t code0, uint32_t code1) {
    mya_puts("\x1b[");
    mya_put_u32(code0);
    mya_puts(";");
    mya_put_u32(code1);
    mya_puts("m");
}

static void print_usage(void) {
    mya_putln("usage:");
    mya_putln("  color --list");
    mya_putln("  color reset");
    mya_putln("  color <fg>");
    mya_putln("  color <fg> <bg>");
    mya_putln("examples:");
    mya_putln("  color red");
    mya_putln("  color bright-white blue");
    mya_putln("  color default default");
}

static void print_palette(void) {
    mya_putln("colors:");
    mya_putln("  black red green yellow blue magenta cyan white");
    mya_putln("prefixes:");
    mya_putln("  bright-<color>, light-<color>");
    mya_putln("special:");
    mya_putln("  default, reset");
}

int program_main(int argc, char** argv) {
    color_spec_t fg;
    color_spec_t bg;

    if (argc < 2) {
        print_usage();
        return 0;
    }

    if (str_eq_ci(argv[1], "--help") || str_eq_ci(argv[1], "-h")) {
        print_usage();
        return 0;
    }
    if (str_eq_ci(argv[1], "--list")) {
        print_palette();
        return 0;
    }
    if (str_eq_ci(argv[1], "reset")) {
        emit_sgr1(0u);
        return 0;
    }
    if (argc > 3) {
        mya_putln("color: too many arguments");
        print_usage();
        return 1;
    }

    if (parse_color_spec(argv[1], &fg) != 0) {
        mya_puts("color: unknown fg color: ");
        mya_putln(argv[1]);
        print_palette();
        return 1;
    }

    if (argc == 2) {
        emit_sgr1(fg_code(&fg));
        return 0;
    }

    if (parse_color_spec(argv[2], &bg) != 0) {
        mya_puts("color: unknown bg color: ");
        mya_putln(argv[2]);
        print_palette();
        return 1;
    }

    emit_sgr2(fg_code(&fg), bg_code(&bg));
    return 0;
}
