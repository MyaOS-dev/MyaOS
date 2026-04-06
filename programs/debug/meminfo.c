#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_meminfo_t info;
    (void)argc;
    (void)argv;

    if (mya_meminfo(&info) != 0) {
        mya_putln("meminfo failed");
        return 1;
    }

    mya_puts("timer_ticks: ");
    mya_put_u64(info.timer_ticks);
    mya_puts("\n");
    mya_puts("pmm_managed_pages: ");
    mya_put_u64(info.pmm_managed_pages);
    mya_puts("\n");
    mya_puts("pmm_free_pages: ");
    mya_put_u64(info.pmm_free_pages);
    mya_puts("\n");
    mya_puts("pmm_allocated_pages: ");
    mya_put_u64(info.pmm_allocated_pages);
    mya_puts("\n");
    mya_puts("heap_page_count: ");
    mya_put_u64(info.heap_page_count);
    mya_puts("\n");
    mya_puts("heap_alloc_count: ");
    mya_put_u64(info.heap_alloc_count);
    mya_puts("\n");
    mya_puts("heap_bytes_used: ");
    mya_put_u64(info.heap_bytes_used);
    mya_puts("\n");
    mya_puts("heap_bytes_capacity: ");
    mya_put_u64(info.heap_bytes_capacity);
    mya_puts("\n");
    mya_puts("paging_mapped_bytes: ");
    mya_put_u64(info.paging_mapped_bytes);
    mya_puts("\n");
    mya_puts("paging_table_pages: ");
    mya_put_u64(info.paging_table_pages);
    mya_puts("\n");
    mya_puts("paging_cr3: ");
    mya_put_u64(info.paging_cr3);
    mya_puts("\n");
    mya_puts("swap_total_bytes: ");
    mya_put_u64(info.swap_total_bytes);
    mya_puts("\n");
    mya_puts("swap_used_bytes: ");
    mya_put_u64(info.swap_used_bytes);
    mya_puts("\n");
    mya_puts("swap_slots: ");
    mya_put_u32(info.swap_slots);
    mya_puts("\n");
    mya_puts("swap_used_slots: ");
    mya_put_u32(info.swap_used_slots);
    mya_puts("\n");
    return 0;
}
