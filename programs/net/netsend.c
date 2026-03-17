#include "../lib/myaos.h"

#define NETSEND_MSG_MAX 256u

static int parse_port(const char* text, uint16_t* out_port) {
    uint64_t value = 0;

    if (!text || !out_port || mya_strto_u64(text, &value) != 0 || value == 0u || value > 65535u) {
        return -1;
    }
    *out_port = (uint16_t)value;
    return 0;
}

int program_main(int argc, char** argv) {
    uint16_t src_port;
    uint16_t dst_port;
    int32_t fd = -1;
    uint32_t written = 0;
    char msg[NETSEND_MSG_MAX];
    uint32_t pos = 0;

    if (argc < 4) {
        mya_putln("usage: netsend <src_port> <dst_port> <text>");
        return 1;
    }

    if (parse_port(argv[1], &src_port) != 0 || parse_port(argv[2], &dst_port) != 0) {
        mya_putln("netsend: invalid port");
        return 1;
    }

    msg[0] = '\0';
    for (int i = 3; i < argc; i++) {
        for (uint32_t j = 0; argv[i][j]; j++) {
            if (pos + 1u >= NETSEND_MSG_MAX) {
                break;
            }
            msg[pos++] = argv[i][j];
        }
        if (i + 1 < argc && pos + 1u < NETSEND_MSG_MAX) {
            msg[pos++] = ' ';
        }
    }
    msg[pos] = '\0';

    if (mya_sock_open(src_port, &fd) != 0) {
        mya_putln("netsend: socket open failed");
        return 1;
    }
    if (mya_sock_send(fd, dst_port, msg, pos, &written) != 0) {
        (void)mya_sock_close(fd);
        mya_putln("netsend: send failed");
        return 1;
    }

    mya_puts("sent=");
    mya_put_u32(written);
    mya_puts("\n");
    (void)mya_sock_close(fd);
    return 0;
}
