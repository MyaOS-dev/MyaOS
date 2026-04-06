#include "../lib/posix_compat.h"

int program_main(int argc, char** argv) {
    char cwd[MYAOS_PATH_MAX];
    (void)argc;
    (void)argv;

    mya_puts("posixcheck pid=");
    mya_put_u32((uint32_t)getpid());
    mya_puts(" uid=");
    mya_put_u32(getuid());
    mya_puts(" cwd=");
    if (getcwd(cwd, sizeof(cwd))) {
        mya_puts(cwd);
    } else {
        mya_puts("<error>");
    }
    mya_puts("\n");
    return 0;
}
