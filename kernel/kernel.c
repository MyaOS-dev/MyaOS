#include "boot.h"
#include "shell.h"

void kernel_main(boot_info_t* boot) {
    shell_start(boot);

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
