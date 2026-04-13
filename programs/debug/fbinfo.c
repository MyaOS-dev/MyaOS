#include "../lib/myaos.h"

#define FBINFO_VALUE_MAX 2048u

static int read_key(const char* key) {
    char path[MYAOS_PATH_MAX];
    uint8_t value[FBINFO_VALUE_MAX];
    uint32_t size = 0;
    uint32_t pos = 0;

    path[pos++] = '/';
    path[pos++] = 's';
    path[pos++] = 'y';
    path[pos++] = 's';
    path[pos++] = '/';
    for (uint32_t i = 0; key[i] && pos + 1u < sizeof(path); i++) {
        path[pos++] = key[i];
    }
    path[pos] = '\0';

    if (mya_fs_read(path, value, sizeof(value) - 1u, &size) != 0) {
        mya_puts(key);
        mya_putln("=<error>");
        return -1;
    }
    value[size] = '\0';
    mya_puts(key);
    mya_puts("=");
    mya_puts((const char*)value);
    mya_puts("\n");
    return 0;
}

int program_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    (void)read_key("fb_width");
    (void)read_key("fb_height");
    (void)read_key("fb_stride");
    (void)read_key("fb_format");
    (void)read_key("fb_size");
    (void)read_key("fb_mode");
    (void)read_key("fb_mode_count");
    (void)read_key("fb_mode_total");
    (void)read_key("fb_modes");
    return 0;
}
