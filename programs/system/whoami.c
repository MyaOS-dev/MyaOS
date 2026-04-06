#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    uint32_t uid;
    (void)argc;
    (void)argv;

    uid = mya_sec_whoami();
    mya_puts("uid=");
    mya_put_u32(uid);
    mya_puts("\n");
    return 0;
}
