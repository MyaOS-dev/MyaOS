#include "../lib/myaos.h"
#include <stdint.h>

#define SWAPCHECK_SIZE 4096u

int program_main(int argc, char** argv) {
    uint8_t* mem;
    uint32_t sum_before = 0;
    uint32_t sum_after = 0;
    (void)argc;
    (void)argv;

    mem = (uint8_t*)mya_mem_map(SWAPCHECK_SIZE, MYAOS_MEM_MAP_WRITABLE);
    if (!mem) {
        mya_putln("swapcheck: mmap failed");
        return 1;
    }

    for (uint32_t i = 0; i < SWAPCHECK_SIZE; i++) {
        mem[i] = (uint8_t)(i & 0xFFu);
        sum_before += mem[i];
    }

    if (mya_mem_swap_out(mem) != 0) {
        mya_putln("swapcheck: swap_out failed");
        (void)mya_mem_unmap(mem);
        return 1;
    }

    if (mya_mem_swap_in(mem) != 0) {
        mya_putln("swapcheck: swap_in failed");
        (void)mya_mem_unmap(mem);
        return 1;
    }

    for (uint32_t i = 0; i < SWAPCHECK_SIZE; i++) {
        sum_after += mem[i];
    }

    (void)mya_mem_unmap(mem);

    if (sum_before != sum_after) {
        mya_putln("swapcheck: data mismatch");
        return 1;
    }

    mya_putln("swapcheck: ok");
    return 0;
}
