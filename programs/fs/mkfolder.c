#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    if (argc < 2) {
        mya_putln("usage: mkdir DIR");
        return 1;
    }
    if (mya_fs_mkdir(argv[1]) != 0) {
        mya_putln("mkdir failed");
        return 1;
    }
    return 0;
}
