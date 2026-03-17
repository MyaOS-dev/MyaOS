#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    volatile uint64_t x = 0x12345678u;
    uint64_t spins = 0u;

    if (argc > 1) {
        if (mya_strto_u64(argv[1], &spins) != 0) {
            mya_putln("cpuburn: invalid spin count");
            return 1;
        }
    }

    if (spins == 0u) {
        for (;;) {
            x = x * 1664525u + 1013904223u;
            if ((x & 0x3FFu) == 0u) {
                __asm__ __volatile__("pause");
            }
        }
    }

    for (uint64_t i = 0u; i < spins; i++) {
        x = x * 1664525u + 1013904223u;
    }
    return (int)(x & 0xFFu);
}
