#include "../lib/myaos.h"

#define NETRECV_BUF_MAX 512u

static int parse_port(const char* text, uint16_t* out_port) {
    uint64_t value = 0;

    if (!text || !out_port || mya_strto_u64(text, &value) != 0 || value == 0u || value > 65535u) {
        return -1;
    }
    *out_port = (uint16_t)value;
    return 0;
}

int program_main(int argc, char** argv) {
    uint16_t listen_port;
    int32_t fd = -1;
    uint32_t timeout_spins = 50000u;
    uint32_t spins = 0u;

    if (argc < 2) {
        mya_putln("usage: netrecv <port> [timeout_spins]");
        return 1;
    }
    if (parse_port(argv[1], &listen_port) != 0) {
        mya_putln("netrecv: invalid port");
        return 1;
    }
    if (argc > 2) {
        uint64_t parsed = 0;
        if (mya_strto_u64(argv[2], &parsed) == 0 && parsed <= 0xFFFFFFFFu) {
            timeout_spins = (uint32_t)parsed;
        }
    }

    if (mya_sock_open(listen_port, &fd) != 0) {
        mya_putln("netrecv: socket open failed");
        return 1;
    }

    for (;;) {
        char buf[NETRECV_BUF_MAX];
        uint32_t read = 0;
        uint16_t src_port = 0;

        if (mya_sock_recv(fd, buf, NETRECV_BUF_MAX - 1u, &read, &src_port) == 0) {
            buf[read] = '\0';
            mya_puts("from=");
            mya_put_u32((uint32_t)src_port);
            mya_puts(" len=");
            mya_put_u32(read);
            mya_puts("\n");
            mya_puts(buf);
            mya_puts("\n");
            (void)mya_sock_close(fd);
            return 0;
        }

        spins++;
        if (timeout_spins != 0u && spins >= timeout_spins) {
            mya_putln("netrecv: timeout");
            (void)mya_sock_close(fd);
            return 2;
        }
        mya_proc_yield();
    }
}
