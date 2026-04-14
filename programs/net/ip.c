#include "../lib/myaos.h"

#define IP_KEY_MAX 64u

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
    uint8_t buf[IP_KEY_MAX];
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
    uint32_t src_ip = 0;
    uint32_t gw_ip = 0;

    (void)argc;
    (void)argv;

    if (read_sys_u32("net_src_ip", &src_ip) != 0 || read_sys_u32("net_gateway_ip", &gw_ip) != 0) {
        mya_putln("ip: data unavailable");
        return 1;
    }

    mya_puts("inet ");
    print_ipv4(src_ip);
    mya_puts("\n");
    mya_puts("gateway ");
    print_ipv4(gw_ip);
    mya_puts("\n");
    return 0;
}
