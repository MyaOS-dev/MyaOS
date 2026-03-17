#include "../lib/myaos.h"

#define UDP4SEND_MSG_MAX 512u

static int parse_port(const char* text, uint16_t* out_port) {
    uint64_t value = 0;

    if (!text || !out_port || mya_strto_u64(text, &value) != 0 || value == 0u || value > 65535u) {
        return -1;
    }
    *out_port = (uint16_t)value;
    return 0;
}

static int parse_ipv4(const char* text, uint32_t* out_ip) {
    uint32_t part = 0u;
    uint32_t value = 0u;
    uint32_t p0 = 0u;
    uint32_t p1 = 0u;
    uint32_t p2 = 0u;
    uint32_t p3 = 0u;
    uint8_t had_digit = 0u;

    if (!text || !out_ip) {
        return -1;
    }

    for (uint32_t i = 0;; i++) {
        char c = text[i];

        if (c >= '0' && c <= '9') {
            value = value * 10u + (uint32_t)(c - '0');
            if (value > 255u) {
                return -1;
            }
            had_digit = 1u;
            continue;
        }

        if (c == '.' || c == '\0') {
            if (!had_digit || part >= 4u) {
                return -1;
            }
            if (part == 0u) {
                p0 = value;
            } else if (part == 1u) {
                p1 = value;
            } else if (part == 2u) {
                p2 = value;
            } else {
                p3 = value;
            }
            part++;
            value = 0u;
            had_digit = 0u;
            if (c == '\0') {
                break;
            }
            continue;
        }

        return -1;
    }

    if (part != 4u) {
        return -1;
    }

    *out_ip = (p0 << 24) | (p1 << 16) | (p2 << 8) | p3;
    return 0;
}

int program_main(int argc, char** argv) {
    uint32_t dst_ip;
    uint16_t dst_port;
    char msg[UDP4SEND_MSG_MAX];
    uint32_t pos = 0u;
    int sent;

    if (argc < 4) {
        mya_putln("usage: udp4send <dst_ip> <dst_port> <text>");
        return 1;
    }
    if (parse_ipv4(argv[1], &dst_ip) != 0 || parse_port(argv[2], &dst_port) != 0) {
        mya_putln("udp4send: invalid target");
        return 1;
    }

    msg[0] = '\0';
    for (int i = 3; i < argc; i++) {
        for (uint32_t j = 0; argv[i][j]; j++) {
            if (pos + 1u >= UDP4SEND_MSG_MAX) {
                break;
            }
            msg[pos++] = argv[i][j];
        }
        if (i + 1 < argc && pos + 1u < UDP4SEND_MSG_MAX) {
            msg[pos++] = ' ';
        }
    }
    msg[pos] = '\0';

    sent = mya_net_send_udp4(dst_ip, dst_port, 0u, msg, pos);
    if (sent < 0) {
        mya_putln("udp4send: send failed");
        return 2;
    }

    mya_puts("sent=");
    mya_put_u32((uint32_t)sent);
    mya_puts("\n");
    return 0;
}
