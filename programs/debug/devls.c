#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_device_info_t devs[16];
    uint32_t count = 0;
    (void)argc;
    (void)argv;

    if (mya_dev_list(devs, 16, &count) != 0) {
        mya_putln("devls failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_put_u32(devs[i].id);
        mya_puts(" ");
        mya_puts(devs[i].name);
        mya_puts(" ");
        mya_puts(devs[i].driver);
        mya_puts("\n");
    }
    return 0;
}
