static int program_meminfo_run(const char* args) {
    (void)args;

    pmm_stats_t pmm;
    heap_stats_t heap;
    paging_stats_t paging;

    pmm_get_stats(&pmm);
    heap_get_stats(&heap);
    paging_get_stats(&paging);

    console_write(&g_shell, "timer_ticks: ");
    console_write_u64(&g_shell, timer_ticks());
    console_put_char(&g_shell, '\n');

    console_write(&g_shell, "pmm_managed_pages: ");
    console_write_u64(&g_shell, pmm.managed_pages);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "pmm_free_pages: ");
    console_write_u64(&g_shell, pmm.free_pages);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "pmm_allocated_pages: ");
    console_write_u64(&g_shell, pmm.allocated_pages);
    console_put_char(&g_shell, '\n');

    console_write(&g_shell, "heap_page_count: ");
    console_write_u64(&g_shell, heap.page_count);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "heap_alloc_count: ");
    console_write_u64(&g_shell, heap.alloc_count);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "heap_bytes_used: ");
    console_write_u64(&g_shell, heap.bytes_used);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "heap_bytes_capacity: ");
    console_write_u64(&g_shell, heap.bytes_capacity);
    console_put_char(&g_shell, '\n');

    console_write(&g_shell, "paging_mapped_bytes: ");
    console_write_u64(&g_shell, paging.mapped_bytes);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "paging_table_pages: ");
    console_write_u64(&g_shell, paging.table_pages);
    console_put_char(&g_shell, '\n');
    console_write(&g_shell, "paging_cr3: ");
    console_write_u64(&g_shell, paging.cr3);
    console_put_char(&g_shell, '\n');

    return 0;
}
