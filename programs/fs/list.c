#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_dirent_t entries[64];
    uint32_t count = 0;
    const char* path = (argc > 1) ? argv[1] : ".";

    if (mya_fs_list(path, entries, 64, &count) != 0) {
        mya_putln("ls failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(entries[i].name);
        mya_puts(mya_node_type_suffix(entries[i].type));
        if (entries[i].type == MYAOS_NODE_FILE) {
            mya_puts(" ");
            mya_put_u32(entries[i].size);
        }
        mya_puts("\n");
    }
    return 0;
}
