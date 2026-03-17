#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        mya_puts(argv[i]);
        if (i + 1 < argc) {
            mya_puts(" ");
        }
    }
    mya_puts("\n");
    return 0;
}
