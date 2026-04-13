#include "../lib/myaos.h"
#include <stdint.h>

#define RESTORE_FILE_MAX (1024u * 1024u)

static uint8_t g_buf[RESTORE_FILE_MAX];

static size_t str_len(const char* s) {
    size_t n = 0u;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0u;
    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i + 1u < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

static char* next_line(char** cursor) {
    char* line;
    char* p;

    if (!cursor || !*cursor || !(*cursor)[0]) {
        return NULL;
    }
    line = *cursor;
    p = line;
    while (*p && *p != '\n' && *p != '\r') {
        p++;
    }
    while (*p == '\n' || *p == '\r') {
        *p = '\0';
        p++;
    }
    *cursor = p;
    return line;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (int)(c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (int)(c - 'A');
    }
    return -1;
}

static int decode_hex(const char* hex, uint8_t* out, uint32_t out_size, uint32_t* out_len) {
    uint32_t hex_len = (uint32_t)str_len(hex);
    uint32_t n = hex_len / 2u;

    if ((hex_len & 1u) != 0u || n > out_size) {
        return -1;
    }
    for (uint32_t i = 0u; i < n; i++) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)(((uint8_t)hi << 4u) | (uint8_t)lo);
    }
    if (out_len) {
        *out_len = n;
    }
    return 0;
}

static void ensure_parent_dirs(const char* file_path) {
    char temp[MYAOS_PATH_MAX];

    if (!file_path || file_path[0] != '/') {
        return;
    }

    str_copy(temp, file_path, sizeof(temp));
    for (uint32_t i = 1; temp[i]; i++) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            if (temp[1] != '\0') {
                (void)mya_fs_mkdir(temp);
            }
            temp[i] = '/';
        }
    }
}

static int join_prefix_path(const char* prefix, const char* abs_path, char* out, uint32_t out_size) {
    uint32_t pos = 0u;
    if (!prefix || !abs_path || !out || out_size == 0u || abs_path[0] != '/') {
        return -1;
    }

    if (prefix[0] == '\0' || (prefix[0] == '/' && prefix[1] == '\0')) {
        str_copy(out, abs_path, out_size);
        return 0;
    }

    for (uint32_t i = 0u; prefix[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = prefix[i];
    }
    if (out[pos - 1u] == '/') {
        pos--;
    }
    for (uint32_t i = 0u; abs_path[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = abs_path[i];
    }
    out[pos] = '\0';
    return 0;
}

int program_main(int argc, char** argv) {
    uint32_t size = 0u;
    char* cursor;
    char pending_file[MYAOS_PATH_MAX];
    const char* prefix = "/";
    uint32_t restored = 0u;

    if (argc < 2) {
        mya_putln("usage: restore <backup_file> [target_prefix]");
        return 1;
    }
    if (argc >= 3) {
        prefix = argv[2];
    }
    if (mya_fs_read(argv[1], g_buf, sizeof(g_buf) - 1u, &size) != 0) {
        mya_putln("restore: failed to read backup file");
        return 1;
    }
    g_buf[size] = '\0';

    cursor = (char*)g_buf;
    {
        char* line0 = next_line(&cursor);
        if (!line0 || !str_starts_with(line0, "MYABACK1")) {
            mya_putln("restore: bad backup format");
            return 1;
        }
    }

    pending_file[0] = '\0';
    while (1) {
        char* line = next_line(&cursor);
        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (str_starts_with(line, "file=")) {
            str_copy(pending_file, line + 5, sizeof(pending_file));
            continue;
        }
        if (str_starts_with(line, "hex=")) {
            uint32_t len = (uint32_t)str_len(line + 4) / 2u;
            uint8_t* decoded;
            uint32_t decoded_len = 0u;
            char target[MYAOS_PATH_MAX];

            if (!pending_file[0]) {
                mya_putln("restore: payload without file=");
                return 1;
            }
            if (join_prefix_path(prefix, pending_file, target, sizeof(target)) != 0) {
                mya_putln("restore: output path overflow");
                return 1;
            }

            decoded = (uint8_t*)mya_mem_map((len == 0u) ? 1u : len, MYAOS_MEM_MAP_WRITABLE);
            if (!decoded) {
                mya_putln("restore: memory map failed");
                return 1;
            }
            if (decode_hex(line + 4, decoded, len, &decoded_len) != 0) {
                (void)mya_mem_unmap(decoded);
                mya_putln("restore: bad hex payload");
                return 1;
            }
            ensure_parent_dirs(target);
            if (decoded_len == 0u) {
                (void)mya_fs_touch(target);
            }
            if (mya_fs_write(target, decoded, decoded_len) != 0) {
                (void)mya_mem_unmap(decoded);
                mya_puts("restore: failed to write ");
                mya_putln(target);
                return 1;
            }
            (void)mya_mem_unmap(decoded);
            restored++;
            pending_file[0] = '\0';
            continue;
        }
    }

    mya_puts("restore: restored files=");
    mya_put_u32(restored);
    mya_puts("\n");
    return restored > 0u ? 0 : 1;
}
