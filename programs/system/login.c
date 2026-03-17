#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    uint32_t uid;

    if (argc < 2 || !argv[1] || !argv[1][0]) {
        mya_putln("usage: login <root|user|guest>");
        return 1;
    }

    if (mya_sec_login(argv[1]) != 0) {
        mya_putln("login: unknown user");
        return 1;
    }

    uid = mya_sec_whoami();
    mya_puts("login: uid=");
    mya_put_u32(uid);
    mya_puts("\n");
    return 0;
}
