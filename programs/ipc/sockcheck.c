#include "../lib/myaos.h"

#define SOCK_MSG_MAX 64u

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0u;

    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

int program_main(int argc, char** argv) {
    int32_t udp_a = -1;
    int32_t udp_b = -1;
    int32_t tcp_listener = -1;
    int32_t tcp_client = -1;
    int32_t tcp_server = -1;
    uint32_t written = 0;
    uint32_t read = 0;
    uint16_t src = 0;
    uint16_t peer = 0;
    char buf[SOCK_MSG_MAX];
    const char* udp_msg = "sock-ok";
    const char* tcp_msg = "tcp-ok";
    int rc = 1;

    (void)argc;
    (void)argv;

    if (mya_sock_open(30001u, &udp_a) != 0 || mya_sock_open(30002u, &udp_b) != 0) {
        mya_putln("sockcheck: udp open failed");
        goto done;
    }

    if (mya_sock_send(udp_a, 30002u, udp_msg, (uint32_t)mya_strlen(udp_msg), &written) != 0 || written == 0u) {
        mya_putln("sockcheck: udp send failed");
        goto done;
    }

    if (mya_sock_recv(udp_b, buf, sizeof(buf) - 1u, &read, &src) != 0) {
        mya_putln("sockcheck: udp recv failed");
        goto done;
    }
    buf[read] = '\0';
    if (!str_eq(buf, udp_msg)) {
        mya_putln("sockcheck: udp payload mismatch");
        goto done;
    }

    mya_puts("sockcheck udp src=");
    mya_put_u32((uint32_t)src);
    mya_puts(" msg=");
    mya_puts(buf);
    mya_puts("\n");

    if (mya_sock_open_ex(MYAOS_SOCK_PROTO_TCP, 31001u, &tcp_listener) != 0) {
        mya_putln("sockcheck: tcp listener open failed");
        goto done;
    }
    if (mya_sock_listen(tcp_listener, 4u) != 0) {
        mya_putln("sockcheck: tcp listen failed");
        goto done;
    }
    if (mya_sock_open_ex(MYAOS_SOCK_PROTO_TCP, 0u, &tcp_client) != 0) {
        mya_putln("sockcheck: tcp client open failed");
        goto done;
    }
    if (mya_sock_connect(tcp_client, 31001u) != 0) {
        mya_putln("sockcheck: tcp connect failed");
        goto done;
    }
    if (mya_sock_accept(tcp_listener, &tcp_server, &peer) != 0 || tcp_server <= 0) {
        mya_putln("sockcheck: tcp accept failed");
        goto done;
    }

    if (mya_sock_send(tcp_client, 0u, tcp_msg, (uint32_t)mya_strlen(tcp_msg), &written) != 0 || written == 0u) {
        mya_putln("sockcheck: tcp send failed");
        goto done;
    }
    if (mya_sock_recv(tcp_server, buf, sizeof(buf) - 1u, &read, &src) != 0 || read == 0u) {
        mya_putln("sockcheck: tcp recv failed");
        goto done;
    }
    buf[read] = '\0';
    if (!str_eq(buf, tcp_msg)) {
        mya_putln("sockcheck: tcp payload mismatch");
        goto done;
    }

    mya_puts("sockcheck tcp peer=");
    mya_put_u32((uint32_t)peer);
    mya_puts(" msg=");
    mya_puts(buf);
    mya_puts("\n");

    rc = 0;

done:
    if (udp_a >= 0) {
        (void)mya_sock_close(udp_a);
    }
    if (udp_b >= 0) {
        (void)mya_sock_close(udp_b);
    }
    if (tcp_server >= 0) {
        (void)mya_sock_close(tcp_server);
    }
    if (tcp_client >= 0) {
        (void)mya_sock_close(tcp_client);
    }
    if (tcp_listener >= 0) {
        (void)mya_sock_close(tcp_listener);
    }
    return rc;
}
