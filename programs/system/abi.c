#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    uint8_t value[32];
    uint32_t size = 0;

    (void)argc;
    (void)argv;

    mya_puts("abi_compile=");
    mya_put_u32(MYAOS_ABI_VERSION);
    mya_puts("\n");

    if (mya_fs_read("/sys/abi_version", value, sizeof(value) - 1u, &size) == 0) {
        value[size] = '\0';
        mya_puts("abi_runtime=");
        mya_puts((const char*)value);
        mya_puts("\n");
    }

    return 0;
}
