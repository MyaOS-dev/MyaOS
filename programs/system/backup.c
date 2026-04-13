#include "../lib/myaos.h"
#include <stdint.h>

#define BACKUP_OUT_MAX (1024u * 1024u)
#define BACKUP_FILE_MAX (128u * 1024u)
#define BACKUP_LIST_MAX 64u
#define BACKUP_MAX_DEPTH 24u

static char g_out[BACKUP_OUT_MAX];
static uint8_t g_file[BACKUP_FILE_MAX];
static uint32_t g_out_pos;
static uint32_t g_file_count;

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

static int append_text(const char* text) {
    if (!text) {
        return -1;
    }

    for (uint32_t i = 0; text[i]; i++) {
        if (g_out_pos + 1u >= sizeof(g_out)) {
            return -1;
        }
        g_out[g_out_pos++] = text[i];
    }
    g_out[g_out_pos] = '\0';
    return 0;
}

static int append_hex(const uint8_t* data, uint32_t size) {
    static const char* k_hex = "0123456789abcdef";

    if ((size != 0u) && !data) {
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        if (g_out_pos + 2u >= sizeof(g_out)) {
            return -1;
        }
        g_out[g_out_pos++] = k_hex[(data[i] >> 4) & 0xFu];
        g_out[g_out_pos++] = k_hex[data[i] & 0xFu];
    }
    g_out[g_out_pos] = '\0';
    return 0;
}

static int build_child_path(const char* base, const char* name, char* out, uint32_t out_size) {
    uint32_t pos = 0;

    if (!base || !name || !out || out_size == 0u) {
        return -1;
    }

    for (uint32_t i = 0; base[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = base[i];
    }

    if (pos == 0u) {
        return -1;
    }
    if (pos > 1u && out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = '/';
    }

    for (uint32_t i = 0; name[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = name[i];
    }

    out[pos] = '\0';
    return 0;
}

static int backup_one_file(const char* path) {
    uint32_t size = 0;

    if (mya_fs_read(path, g_file, sizeof(g_file), &size) != 0) {
        return -1;
    }

    if (append_text("file=") != 0 ||
        append_text(path) != 0 ||
        append_text("\nhex=") != 0 ||
        append_hex(g_file, size) != 0 ||
        append_text("\n") != 0) {
        return -1;
    }

    g_file_count++;
    return 0;
}

static int walk_path(const char* path, uint32_t depth) {
    myaos_dirent_t entries[BACKUP_LIST_MAX];
    uint32_t count = 0;

    if (depth == 0u) {
        return -1;
    }

    if (mya_fs_list(path, entries, BACKUP_LIST_MAX, &count) != 0) {
        return backup_one_file(path);
    }

    for (uint32_t i = 0; i < count; i++) {
        char child[MYAOS_PATH_MAX];
        int is_dir;

        if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) {
            continue;
        }
        if (build_child_path(path, entries[i].name, child, sizeof(child)) != 0) {
            continue;
        }

        is_dir = (entries[i].type == MYAOS_NODE_DIR || entries[i].type == MYAOS_NODE_MOUNT) ? 1 : 0;
        if (is_dir) {
            if (walk_path(child, depth - 1u) != 0) {
                return -1;
            }
        } else {
            if (backup_one_file(child) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

int program_main(int argc, char** argv) {
    myaos_sched_info_t sched;

    if (argc < 3) {
        mya_putln("usage: backup <source_path> <out_file>");
        return 1;
    }

    g_out_pos = 0u;
    g_file_count = 0u;
    g_out[0] = '\0';

    if (append_text("MYABACK1\nsource=") != 0 ||
        append_text(argv[1]) != 0 ||
        append_text("\n") != 0) {
        mya_putln("backup: output buffer overflow");
        return 1;
    }

    if (mya_sched_info(&sched) == 0) {
        if (append_text("ticks=") != 0) {
            mya_putln("backup: output buffer overflow");
            return 1;
        }
        {
            char num[32];
            mya_u64_to_dec(sched.timer_ticks, num, sizeof(num));
            if (append_text(num) != 0 || append_text("\n") != 0) {
                mya_putln("backup: output buffer overflow");
                return 1;
            }
        }
    }

    if (walk_path(argv[1], BACKUP_MAX_DEPTH) != 0 || g_file_count == 0u) {
        mya_puts("backup: failed to read source: ");
        mya_putln(argv[1]);
        return 1;
    }

    if (append_text("files=") != 0) {
        mya_putln("backup: output buffer overflow");
        return 1;
    }
    {
        char num[16];
        mya_u32_to_dec(g_file_count, num, sizeof(num));
        if (append_text(num) != 0 || append_text("\n") != 0) {
            mya_putln("backup: output buffer overflow");
            return 1;
        }
    }

    if (mya_fs_write(argv[2], g_out, g_out_pos) != 0) {
        mya_puts("backup: failed to write ");
        mya_putln(argv[2]);
        return 1;
    }

    mya_puts("backup: wrote ");
    mya_puts(argv[2]);
    mya_puts(" files=");
    mya_put_u32(g_file_count);
    mya_puts("\n");
    return 0;
}
