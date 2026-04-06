#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    if (argc < 2) {
        mya_putln("usage: touch FILE");
        return 1;
    }
    if (mya_fs_touch(argv[1]) != 0) {
        mya_putln("touch failed");
        return 1;
    }
    return 0;
}
