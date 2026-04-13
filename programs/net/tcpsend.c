#include "../lib/myaos.h"

#define TCPSEND_MSG_MAX 256u

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

static int parse_port(const char* text, uint16_t* out_port) {
    uint64_t value = 0;

    if (!text || !out_port || mya_strto_u64(text, &value) != 0 || value == 0u || value > 65535u) {
        return -1;
    }
    *out_port = (uint16_t)value;
    return 0;
}

int program_main(int argc, char** argv) {
    uint8_t external = 0u;
    uint32_t dst_ip = 0u;
    uint16_t dst_port;
    int32_t fd = -1;
    uint32_t written = 0;
    char msg[TCPSEND_MSG_MAX];
    uint32_t pos = 0u;
    int text_from = 2;

    if (argc < 3) {
        mya_putln("usage: tcpsend <dst_port> <text> | tcpsend <dst_ip> <dst_port> <text>");
        return 1;
    }
    if (parse_ipv4(argv[1], &dst_ip) == 0) {
        if (argc < 4 || parse_port(argv[2], &dst_port) != 0) {
            mya_putln("tcpsend: invalid target");
            return 1;
        }
        external = 1u;
        text_from = 3;
    } else if (parse_port(argv[1], &dst_port) != 0) {
        mya_putln("tcpsend: invalid port");
        return 1;
    }

    msg[0] = '\0';
    for (int i = text_from; i < argc; i++) {
        for (uint32_t j = 0; argv[i][j]; j++) {
            if (pos + 1u >= TCPSEND_MSG_MAX) {
                break;
            }
            msg[pos++] = argv[i][j];
        }
        if (i + 1 < argc && pos + 1u < TCPSEND_MSG_MAX) {
            msg[pos++] = ' ';
        }
    }
    msg[pos] = '\0';

    if (mya_sock_open_ex(MYAOS_SOCK_PROTO_TCP, 0u, &fd) != 0) {
        mya_putln("tcpsend: socket open failed");
        return 1;
    }
    if ((external && mya_sock_connect4(fd, dst_ip, dst_port) != 0) || (!external && mya_sock_connect(fd, dst_port) != 0)) {
        (void)mya_sock_close(fd);
        mya_putln("tcpsend: connect failed");
        return 1;
    }
    if (mya_sock_send(fd, 0u, msg, pos, &written) != 0) {
        (void)mya_sock_close(fd);
        mya_putln("tcpsend: send failed");
        return 1;
    }

    mya_puts("sent=");
    mya_put_u32(written);
    mya_puts("\n");
    (void)mya_sock_close(fd);
    return 0;
}
