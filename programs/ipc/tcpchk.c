#include "../lib/myaos.h"

#define TCPCHK_BUF_MAX 96u

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
    int32_t listener = -1;
    int32_t client = -1;
    int32_t server = -1;
    uint16_t peer_port = 0;
    uint16_t src_port = 0;
    uint32_t written = 0;
    uint32_t read = 0;
    char buf[TCPCHK_BUF_MAX];
    const char* msg = "tcp-ok";
    int rc = 1;

    (void)argc;
    (void)argv;

    if (mya_sock_open_ex(MYAOS_SOCK_PROTO_TCP, 32001u, &listener) != 0) {
        mya_putln("tcpchk: listener open failed");
        goto done;
    }
    if (mya_sock_listen(listener, 4u) != 0) {
        mya_putln("tcpchk: listen failed");
        goto done;
    }
    if (mya_sock_open_ex(MYAOS_SOCK_PROTO_TCP, 0u, &client) != 0) {
        mya_putln("tcpchk: client open failed");
        goto done;
    }
    if (mya_sock_connect(client, 32001u) != 0) {
        mya_putln("tcpchk: connect failed");
        goto done;
    }
    if (mya_sock_accept(listener, &server, &peer_port) != 0 || server <= 0) {
        mya_putln("tcpchk: accept failed");
        goto done;
    }

    if (mya_sock_send(client, 0u, msg, (uint32_t)mya_strlen(msg), &written) != 0 || written == 0u) {
        mya_putln("tcpchk: send failed");
        goto done;
    }

    if (mya_sock_recv(server, buf, sizeof(buf) - 1u, &read, &src_port) != 0 || read == 0u) {
        mya_putln("tcpchk: recv failed");
        goto done;
    }

    buf[read] = '\0';
    if (!str_eq(buf, msg)) {
        mya_putln("tcpchk: payload mismatch");
        goto done;
    }

    mya_puts("tcpchk peer=");
    mya_put_u32((uint32_t)peer_port);
    mya_puts(" src=");
    mya_put_u32((uint32_t)src_port);
    mya_puts(" msg=");
    mya_puts(buf);
    mya_puts("\n");

    rc = 0;

done:
    if (server >= 0) {
        (void)mya_sock_close(server);
    }
    if (client >= 0) {
        (void)mya_sock_close(client);
    }
    if (listener >= 0) {
        (void)mya_sock_close(listener);
    }
    return rc;
}
