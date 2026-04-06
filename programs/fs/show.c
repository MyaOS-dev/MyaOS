#include "../lib/myaos.h"

#define CAT_BUF_SIZE 2048

int program_main(int argc, char** argv) {
    static uint8_t buf[CAT_BUF_SIZE + 1];
    uint32_t size = 0;

    if (argc < 2) {
        mya_putln("usage: cat FILE");
        return 1;
    }

    if (mya_fs_read(argv[1], buf, CAT_BUF_SIZE, &size) != 0) {
        mya_putln("cat failed");
        return 1;
    }

    buf[size] = 0;
    mya_puts((const char*)buf);
    if (size == 0 || buf[size - 1] != '\n') {
        mya_puts("\n");
    }
    return 0;
}
