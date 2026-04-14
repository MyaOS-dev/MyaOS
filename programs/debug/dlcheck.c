#include "../lib/dynload.h"

typedef int (*demo_add_fn_t)(int, int);
typedef uint32_t (*demo_magic_fn_t)(void);
typedef int (*linkdemo_sum_fn_t)(int, int);
typedef uint32_t (*linkdemo_magic_fn_t)(void);

static int dl_host_mul(int a, int b) {
    return a * b;
}

static uint32_t dl_host_magic(void) {
    return 0xA55A10F0u;
}

static void* dl_resolve(const char* symbol_name, void* user_ctx) {
    const mya_dl_symbol_t* table = (const mya_dl_symbol_t*)user_ctx;
    return mya_dl_resolve_from_table(table, 2u, symbol_name);
}

int program_main(int argc, char** argv) {
    const char* lib_path = "/lib/liblinkdemo.so";
    mya_dl_symbol_t exports[2];
    mya_dl_open_opts_t opts;
    mya_dl_handle_t handle;
    linkdemo_sum_fn_t linkdemo_sum;
    linkdemo_magic_fn_t linkdemo_magic;
    demo_add_fn_t demo_add;
    demo_magic_fn_t demo_magic;
    int linked_sum;
    uint32_t linked_magic;
    int sum = 0;
    uint32_t magic = 0u;

    if (argc > 1) {
        lib_path = argv[1];
    }

    exports[0].name = "dl_host_mul";
    exports[0].address = (void*)(uintptr_t)&dl_host_mul;
    exports[1].name = "dl_host_magic";
    exports[1].address = (void*)(uintptr_t)&dl_host_magic;
    opts.resolve = dl_resolve;
    opts.user_ctx = exports;

    if (mya_dl_open_ex(lib_path, &opts, &handle) != 0) {
        mya_putln("dlcheck: open failed");
        return 1;
    }

    linkdemo_sum = (linkdemo_sum_fn_t)mya_dl_sym(&handle, "linkdemo_sum");
    linkdemo_magic = (linkdemo_magic_fn_t)mya_dl_sym(&handle, "linkdemo_magic");

    if (linkdemo_sum && linkdemo_magic) {
        linked_sum = linkdemo_sum(6, 7);
        linked_magic = linkdemo_magic();
        if (linked_sum != 49 || linked_magic != 0xB60D8B2Fu) {
            mya_putln("dlcheck: relocation/resolve mismatch");
            (void)mya_dl_close(&handle);
            return 1;
        }

        mya_puts("dlcheck linked sum=");
        mya_put_u32((uint32_t)linked_sum);
        mya_puts(" magic=");
        mya_put_u32(linked_magic);
        mya_puts("\n");
        (void)mya_dl_close(&handle);
        return 0;
    }

    demo_add = (demo_add_fn_t)mya_dl_sym(&handle, "demo_add");
    demo_magic = (demo_magic_fn_t)mya_dl_sym(&handle, "demo_magic");
    if (!demo_add || !demo_magic) {
        mya_putln("dlcheck: symbol lookup failed");
        (void)mya_dl_close(&handle);
        return 1;
    }

    sum = demo_add(20, 22);
    magic = demo_magic();

    mya_puts("dlcheck add=");
    mya_put_u32((uint32_t)sum);
    mya_puts(" magic=");
    mya_put_u32(magic);
    mya_puts("\n");

    (void)mya_dl_close(&handle);
    return 0;
}
