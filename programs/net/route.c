#include "../lib/myaos.h"

#define ROUTE_KEY_MAX 64u

static int parse_u32(const char* text, uint32_t* out) {
    uint64_t value = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }
    for (uint32_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
        if (value > 0xFFFFFFFFull) {
            return -1;
        }
    }

    *out = (uint32_t)value;
    return 0;
}

static int read_sys_u32(const char* key, uint32_t* out) {
    char path[MYAOS_PATH_MAX];
    uint8_t buf[ROUTE_KEY_MAX];
    uint32_t size = 0;
    uint32_t pos = 0;

    if (!key || !out) {
        return -1;
    }

    path[pos++] = '/';
    path[pos++] = 's';
    path[pos++] = 'y';
    path[pos++] = 's';
    path[pos++] = '/';
    for (uint32_t i = 0; key[i] && pos + 1u < sizeof(path); i++) {
        path[pos++] = key[i];
    }
    path[pos] = '\0';

    if (mya_fs_read(path, buf, sizeof(buf) - 1u, &size) != 0) {
        return -1;
    }
    buf[size] = '\0';
    return parse_u32((const char*)buf, out);
}

static int read_sys_text(const char* key, char* out, uint32_t out_size) {
    char path[MYAOS_PATH_MAX];
    uint8_t buf[ROUTE_KEY_MAX];
    uint32_t size = 0;
    uint32_t pos = 0;

    if (!key || !out || out_size == 0u) {
        return -1;
    }

    path[pos++] = '/';
    path[pos++] = 's';
    path[pos++] = 'y';
    path[pos++] = 's';
    path[pos++] = '/';
    for (uint32_t i = 0; key[i] && pos + 1u < sizeof(path); i++) {
        path[pos++] = key[i];
    }
    path[pos] = '\0';

    if (mya_fs_read(path, buf, sizeof(buf) - 1u, &size) != 0) {
        return -1;
    }
    if (size >= out_size) {
        size = out_size - 1u;
    }
    for (uint32_t i = 0; i < size; i++) {
        out[i] = (char)buf[i];
    }
    out[size] = '\0';
    return 0;
}

static void print_ipv4(uint32_t ip) {
    mya_put_u32((ip >> 24) & 0xFFu);
    mya_puts(".");
    mya_put_u32((ip >> 16) & 0xFFu);
    mya_puts(".");
    mya_put_u32((ip >> 8) & 0xFFu);
    mya_puts(".");
    mya_put_u32(ip & 0xFFu);
}

int program_main(int argc, char** argv) {
    uint32_t gw_ip = 0;
    char if_name[MYAOS_NAME_MAX];

    (void)argc;
    (void)argv;

    if (read_sys_u32("net_gateway_ip", &gw_ip) != 0) {
        mya_putln("route: data unavailable");
        return 1;
    }
    if (read_sys_text("net_if_name", if_name, sizeof(if_name)) != 0 || if_name[0] == '\0') {
        if_name[0] = 'e';
        if_name[1] = 't';
        if_name[2] = 'h';
        if_name[3] = '0';
        if_name[4] = '\0';
    }

    mya_puts("default via ");
    print_ipv4(gw_ip);
    mya_puts(" dev ");
    mya_puts(if_name);
    mya_puts("\n");
    return 0;
}
