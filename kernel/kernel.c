#include "boot.h"
#include "heap.h"
#include "interrupts.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "shell.h"
#include "syscall.h"
#include "timer.h"
#include <stdint.h>

extern char _kernel_start;
extern char _kernel_end;

static inline void dbg_putc(char c) {
    __asm__ __volatile__("outb %0, $0xe9" : : "a"(c));
}

static void shell_task(void* ctx) {
    (void)ctx;
    shell_step();
}

static void idle_task(void* ctx) {
    (void)ctx;
    __asm__ __volatile__("hlt");
}

void kernel_main(boot_info_t* boot) {
    dbg_putc('a');
    pmm_init(boot, (uint64_t)(uintptr_t)&_kernel_start, (uint64_t)(uintptr_t)&_kernel_end);
    dbg_putc('b');

    if (paging_init(boot) != 0) {
        dbg_putc('x');
    } else {
        dbg_putc('c');
    }

    heap_init();
    dbg_putc('d');

    interrupts_init();
    dbg_putc('e');
    timer_init(100u);
    dbg_putc('t');

    shell_init(boot);
    dbg_putc('s');

    syscall_set_boot_info(boot);
    scheduler_init(1u);
    (void)scheduler_add_task("shell", shell_task, NULL);
    (void)scheduler_add_task("idle", idle_task, NULL);
    dbg_putc('q');

    dbg_putc('f');
    scheduler_run();
}
