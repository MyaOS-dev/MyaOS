#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    char cwd[MYAOS_PATH_MAX];
    (void)argc;
    (void)argv;

    if (mya_fs_getcwd(cwd, sizeof(cwd)) != 0) {
        mya_putln("pwd failed");
        return 1;
    }

    mya_putln(cwd);
    return 0;
}
