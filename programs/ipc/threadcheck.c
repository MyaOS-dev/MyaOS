#include "../lib/myaos.h"

#define THREAD_STACK_SIZE 16384u

static void worker_thread(void* arg) {
    volatile uint32_t* counter = (volatile uint32_t*)arg;

    for (uint32_t i = 0; i < 5u; i++) {
        *counter = *counter + 1u;
        mya_proc_yield();
    }

    mya_proc_exit(0);
}

int program_main(int argc, char** argv) {
    volatile uint32_t* counter;
    uint8_t* stack;
    int32_t tid = -1;
    int32_t exit_code = 0;

    (void)argc;
    (void)argv;

    counter = (volatile uint32_t*)mya_mem_map(sizeof(uint32_t), MYAOS_MEM_MAP_WRITABLE);
    if (!counter) {
        mya_putln("threadcheck: map counter failed");
        return 1;
    }

    stack = (uint8_t*)mya_mem_map(THREAD_STACK_SIZE, MYAOS_MEM_MAP_WRITABLE);
    if (!stack) {
        mya_putln("threadcheck: map stack failed");
        return 1;
    }

    *counter = 0u;

    if (mya_thread_create(worker_thread, (void*)counter, stack + THREAD_STACK_SIZE, &tid) != 0) {
        mya_putln("threadcheck: create failed");
        return 1;
    }

    for (uint32_t i = 0; i < 5u; i++) {
        *counter = *counter + 2u;
        mya_proc_yield();
    }

    if (mya_thread_join(tid, &exit_code) != 0 || exit_code != 0) {
        mya_putln("threadcheck: join failed");
        return 1;
    }

    mya_puts("threadcheck counter=");
    mya_put_u32(*counter);
    mya_puts(" expected=15\n");

    (void)mya_mem_unmap((void*)stack);
    (void)mya_mem_unmap((void*)counter);
    return (*counter == 15u) ? 0 : 1;
}
