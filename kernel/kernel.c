#include "boot.h"
#include "graphics.h"
#include <stdint.h>
#include <stddef.h>

void kernel_main(boot_info_t* boot) {
    clear_screen(boot, 0x00000000);

    draw_string(boot, 16, 16, "Hello from MyaOS", 0x00FFFFFF, 0x00000000);
    draw_string(boot, 16, 32, "Kernel started", 0x00FFFFFF, 0x00000000);
    draw_string(boot, 16, 48, "Font: 8x16", 0x00FFFFFF, 0x00000000);

    draw_symbol(boot, 5, 10);

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}