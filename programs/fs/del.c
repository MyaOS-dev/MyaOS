#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    if (argc < 2) {
        mya_putln("usage: del FILE");
        return 1;
    }

    if (mya_fs_remove(argv[1]) != 0) {
        mya_putln("del failed");
        return 1;
    }
    return 0;
}
