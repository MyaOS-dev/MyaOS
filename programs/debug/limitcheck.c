#include "../lib/myaos.h"

#define LIMITCHECK_PAGE 4096u

int program_main(int argc, char** argv) {
    myaos_proc_limits_t old_limits;
    myaos_proc_limits_t test_limits;
    myaos_proc_limits_t now_limits;
    void* a;
    void* b;
    void* c;
    int ok = 0;

    (void)argc;
    (void)argv;

    if (mya_proc_get_limits(&old_limits) != 0) {
        mya_putln("limitcheck: get old limits failed");
        return 1;
    }

    test_limits = old_limits;
    test_limits.vm_limit_bytes = (uint64_t)LIMITCHECK_PAGE * 2u;
    if (mya_proc_set_limits(&test_limits) != 0) {
        mya_putln("limitcheck: set vm limit failed");
        return 1;
    }
    if (mya_proc_get_limits(&now_limits) != 0) {
        mya_putln("limitcheck: get new limits failed");
        return 1;
    }
    if (now_limits.vm_limit_bytes != test_limits.vm_limit_bytes) {
        mya_putln("limitcheck: vm limit mismatch");
        return 1;
    }

    a = mya_mem_map(LIMITCHECK_PAGE, MYAOS_MEM_MAP_WRITABLE);
    b = mya_mem_map(LIMITCHECK_PAGE, MYAOS_MEM_MAP_WRITABLE);
    c = mya_mem_map(LIMITCHECK_PAGE, MYAOS_MEM_MAP_WRITABLE);

    if (a && b && !c) {
        ok = 1;
    } else {
        mya_putln("limitcheck: vm limit enforcement failed");
    }

    if (a) {
        (void)mya_mem_unmap(a);
    }
    if (b) {
        (void)mya_mem_unmap(b);
    }
    if (c) {
        (void)mya_mem_unmap(c);
    }

    (void)mya_proc_set_limits(&old_limits);
    return ok ? 0 : 1;
}
