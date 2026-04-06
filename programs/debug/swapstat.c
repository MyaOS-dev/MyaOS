#include "../lib/libc.h"

int program_main(int argc, char** argv) {
    myaos_swap_info_t info;
    (void)argc;
    (void)argv;

    if (mya_swap_info(&info) != 0) {
        mya_putln("swapstat: failed");
        return 1;
    }

    mya_print_kv_u64("swap.total_bytes", info.total_bytes);
    mya_print_kv_u64("swap.used_bytes", info.used_bytes);
    mya_print_kv_u32("swap.slots", info.slot_count);
    mya_print_kv_u32("swap.used_slots", info.used_slots);
    mya_print_kv_u64("swap.out_ops", info.swap_out_ops);
    mya_print_kv_u64("swap.in_ops", info.swap_in_ops);
    return 0;
}
