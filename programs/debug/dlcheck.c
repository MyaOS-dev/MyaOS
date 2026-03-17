#include "../lib/dynload.h"

typedef int (*demo_add_fn_t)(int, int);
typedef uint32_t (*demo_magic_fn_t)(void);

int program_main(int argc, char** argv) {
    const char* lib_path = "/lib/libdemo.so";
    mya_dl_handle_t handle;
    demo_add_fn_t demo_add;
    demo_magic_fn_t demo_magic;
    int sum;
    uint32_t magic;

    if (argc > 1) {
        lib_path = argv[1];
    }

    if (mya_dl_open(lib_path, &handle) != 0) {
        mya_putln("dlcheck: open failed");
        return 1;
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
