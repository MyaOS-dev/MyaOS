#include "../lib/myaos.h"

static void dummy_thread(void* arg) {
    (void)arg;
    mya_proc_exit(0);
}

static int expect_fail(const char* name, int64_t rc, uint32_t* out_passed, uint32_t* out_total) {
    if (out_total) {
        (*out_total)++;
    }
    if (rc < 0) {
        if (out_passed) {
            (*out_passed)++;
        }
        return 0;
    }

    mya_puts("seccheck: expected failure for ");
    mya_puts(name);
    mya_puts(", rc=");
    mya_put_u64((uint64_t)rc);
    mya_puts("\n");
    return -1;
}

int program_main(int argc, char** argv) {
    uint32_t passed = 0;
    uint32_t total = 0;
    uint64_t bad_ptr = 0xFFFF800000000000ull;
    uint32_t proc_count = 0;
    uint8_t buf[16];
    uint32_t read_size = 0;
    int32_t tid = -1;
    int32_t exit_code = 0;

    (void)argc;
    (void)argv;

    if (expect_fail(
            "PROC_INFO bad out ptr",
            mya_syscall(
                MYAOS_SYS_PROC_INFO,
                bad_ptr,
                1u,
                (uint64_t)(uintptr_t)&proc_count,
                0u,
                0u
            ),
            &passed,
            &total
        ) != 0) {
        return 1;
    }

    if (expect_fail(
            "FS_READ bad path ptr",
            mya_syscall(
                MYAOS_SYS_FS_READ,
                bad_ptr,
                (uint64_t)(uintptr_t)buf,
                (uint64_t)sizeof(buf),
                (uint64_t)(uintptr_t)&read_size,
                0u
            ),
            &passed,
            &total
        ) != 0) {
        return 1;
    }

    if (expect_fail(
            "THREAD_CREATE bad stack",
            mya_syscall(
                MYAOS_SYS_THREAD_CREATE,
                (uint64_t)(uintptr_t)dummy_thread,
                0u,
                16u,
                (uint64_t)(uintptr_t)&tid,
                0u
            ),
            &passed,
            &total
        ) != 0) {
        return 1;
    }

    if (expect_fail(
            "THREAD_JOIN unknown tid",
            mya_thread_join(123456, &exit_code),
            &passed,
            &total
        ) != 0) {
        return 1;
    }

    if (expect_fail(
            "THREAD_JOIN_POLL unknown tid",
            mya_thread_join_poll(123456, &exit_code),
            &passed,
            &total
        ) != 0) {
        return 1;
    }

    mya_puts("seccheck: passed ");
    mya_put_u32(passed);
    mya_puts("/");
    mya_put_u32(total);
    mya_puts(" checks\n");
    return (passed == total) ? 0 : 1;
}
