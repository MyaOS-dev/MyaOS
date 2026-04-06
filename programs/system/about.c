#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    mya_putln("MyaOS experimental kernel");
    mya_putln("features: vfs elf loader processes syscalls devices");
    return 0;
}
