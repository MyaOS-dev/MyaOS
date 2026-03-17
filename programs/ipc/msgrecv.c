#include "libc.h"

#define MSG_MAX 128

int program_main(int argc, char** argv) {
    char msg[MSG_MAX];
    uint32_t len = 0;
    int32_t from = -1;

    (void)argc;
    (void)argv;

    if (mya_proc_msg_recv(msg, sizeof(msg), &len, &from) != 0) {
        mya_putln("msgrecv: no message");
        return 1;
    }

    mya_puts("from=");
    mya_put_u32((uint32_t)from);
    mya_puts(" len=");
    mya_put_u32(len);
    mya_puts("\n");
    mya_puts(msg);
    mya_puts("\n");
    return 0;
}
