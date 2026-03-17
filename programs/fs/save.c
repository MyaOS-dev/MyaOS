#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    const char* path;
    char buffer[256];
    uint32_t pos = 0;

    if (argc < 3) {
        mya_putln("usage: write FILE TEXT");
        return 1;
    }

    path = argv[1];
    for (int i = 2; i < argc && pos + 1 < sizeof(buffer); i++) {
        const char* text = argv[i];
        for (size_t j = 0; text[j] && pos + 1 < sizeof(buffer); j++) {
            buffer[pos++] = text[j];
        }
        if (i + 1 < argc && pos + 1 < sizeof(buffer)) {
            buffer[pos++] = ' ';
        }
    }
    buffer[pos] = '\0';

    if (mya_fs_write(path, buffer, pos) != 0) {
        mya_putln("write failed");
        return 1;
    }
    return 0;
}
