#include "../lib/myaos.h"
#include <stdint.h>

static void usage(void) {
    mya_putln("usage: hotplug <size_mib> [block_size]");
}

int program_main(int argc, char** argv) {
    uint64_t size_mib = 0;
    uint64_t block_size_u64 = 512u;
    uint64_t size_bytes;
    uint32_t disk_id = 0;

    if (argc < 2) {
        usage();
        return 1;
    }

    if (mya_strto_u64(argv[1], &size_mib) != 0 || size_mib == 0u) {
        mya_putln("hotplug: bad size");
        return 1;
    }
    if (argc > 2 && (mya_strto_u64(argv[2], &block_size_u64) != 0 || block_size_u64 == 0u ||
                     block_size_u64 > 0xFFFFFFFFu)) {
        mya_putln("hotplug: bad block size");
        return 1;
    }

    size_bytes = size_mib * 1024ull * 1024ull;
    if (size_bytes == 0u) {
        mya_putln("hotplug: size overflow");
        return 1;
    }

    if (mya_hotplug_ramdisk(size_bytes, (uint32_t)block_size_u64, &disk_id) != 0) {
        mya_putln("hotplug: failed");
        return 1;
    }

    mya_puts("hotplug: added disk");
    mya_put_u32(disk_id);
    mya_puts("\n");
    return 0;
}
