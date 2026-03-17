#include "blockio.h"
#include "device.h"
#include "heap.h"
#include <stddef.h>
#include <stdint.h>

typedef uint64_t efi_status_t;
typedef uint64_t efi_lba_t;

typedef struct efi_block_io_protocol efi_block_io_protocol_t;

typedef efi_status_t (__attribute__((ms_abi)) *efi_block_write_blocks_t)(
    efi_block_io_protocol_t* this_ptr,
    uint32_t media_id,
    efi_lba_t lba,
    uint64_t buffer_size,
    void* buffer
);

typedef efi_status_t (__attribute__((ms_abi)) *efi_block_flush_blocks_t)(
    efi_block_io_protocol_t* this_ptr
);

struct efi_block_io_protocol {
    uint64_t revision;
    void* media;
    void* reset;
    void* read_blocks;
    efi_block_write_blocks_t write_blocks;
    efi_block_flush_blocks_t flush_blocks;
};

typedef struct {
    const blockio_disk_t* disk;
} blockio_device_ctx_t;

/* Runtime hot-plug can add RAM-backed disks beyond firmware-provided entries. */
#define BLOCKIO_MAX_DISKS 8u
#define BLOCKIO_DEFAULT_BLOCK_SIZE 512u

static blockio_disk_t g_blockio_disks[BLOCKIO_MAX_DISKS];
static blockio_device_ctx_t g_blockio_ctx[BLOCKIO_MAX_DISKS];
static uint32_t g_blockio_disk_count;

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

    if (dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void u32_to_dec(uint32_t value, char* out, size_t out_size) {
    char rev[16];
    size_t n = 0;
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    do {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(rev));

    while (n > 0 && pos + 1 < out_size) {
        out[pos++] = rev[--n];
    }
    out[pos] = '\0';
}

static void mem_zero(void* ptr, uint64_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint64_t i = 0; i < size; i++) {
        p[i] = 0u;
    }
}

static void build_disk_name(uint32_t disk_id, char* out, size_t out_size) {
    char number[16];

    str_copy(out, "disk", out_size);
    u32_to_dec(disk_id, number, sizeof(number));

    for (size_t i = 0; out[i] && i + 1 < out_size; i++) {
        if (out[i + 1] == '\0') {
            size_t pos = i + 1;
            for (size_t j = 0; number[j] && pos + 1 < out_size; j++) {
                out[pos++] = number[j];
                out[pos] = '\0';
            }
            return;
        }
    }
}

static int blockio_sync_device(device_t* dev) {
    blockio_device_ctx_t* ctx = (blockio_device_ctx_t*)dev->ctx;
    return ctx ? blockio_writeback_disk(ctx->disk) : -1;
}

static const device_ops_t g_blockio_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = blockio_sync_device,
};

int blockio_init(boot_info_t* boot) {
    g_blockio_disk_count = 0;

    if (!boot) {
        return -1;
    }

    if (boot->disk_count > 0) {
        for (uint32_t i = 0; i < boot->disk_count && g_blockio_disk_count < BLOCKIO_MAX_DISKS; i++) {
            blockio_disk_t* disk = &g_blockio_disks[g_blockio_disk_count];
            disk->boot = boot;
            disk->disk_id = g_blockio_disk_count;
            disk->image_base = (uint8_t*)(uintptr_t)boot->disks[i].image_base;
            disk->image_size = boot->disks[i].image_size;
            disk->efi_block_io = boot->disks[i].efi_block_io;
            disk->lba_start = boot->disks[i].lba_start;
            disk->block_count = boot->disks[i].block_count;
            disk->media_id = boot->disks[i].media_id;
            disk->block_size = boot->disks[i].block_size;
            disk->read_only = boot->disks[i].read_only;
            disk->is_boot = boot->disks[i].is_boot;
            build_disk_name(disk->disk_id, disk->name, sizeof(disk->name));

            g_blockio_ctx[g_blockio_disk_count].disk = disk;
            (void)device_register(
                MYAOS_DEV_BLOCK,
                disk->name,
                "efi-blockio",
                &g_blockio_ctx[g_blockio_disk_count],
                &g_blockio_ops,
                NULL
            );
            g_blockio_disk_count++;
        }
        return g_blockio_disk_count > 0 ? 0 : -1;
    }

    if (boot->boot_disk_base == 0 || boot->boot_disk_size == 0) {
        return -1;
    }

    g_blockio_disks[0].boot = boot;
    g_blockio_disks[0].disk_id = 0;
    g_blockio_disks[0].image_base = (uint8_t*)(uintptr_t)boot->boot_disk_base;
    g_blockio_disks[0].image_size = boot->boot_disk_size;
    g_blockio_disks[0].efi_block_io = boot->efi_block_io;
    g_blockio_disks[0].lba_start = boot->boot_disk_lba_start;
    g_blockio_disks[0].block_count = boot->boot_disk_block_count;
    g_blockio_disks[0].media_id = boot->boot_disk_media_id;
    g_blockio_disks[0].block_size = boot->boot_disk_block_size;
    g_blockio_disks[0].read_only = boot->boot_disk_read_only;
    g_blockio_disks[0].is_boot = 1u;
    build_disk_name(0, g_blockio_disks[0].name, sizeof(g_blockio_disks[0].name));

    g_blockio_ctx[0].disk = &g_blockio_disks[0];
    g_blockio_disk_count = 1;
    return device_register(MYAOS_DEV_BLOCK, "disk0", "efi-blockio", &g_blockio_ctx[0], &g_blockio_ops, NULL);
}

uint32_t blockio_disk_count(void) {
    return g_blockio_disk_count;
}

const blockio_disk_t* blockio_get_disk(uint32_t disk_id) {
    if (disk_id >= g_blockio_disk_count) {
        return NULL;
    }
    return &g_blockio_disks[disk_id];
}

int blockio_disk_info(uint32_t disk_id, myaos_disk_info_t* out) {
    const blockio_disk_t* disk = blockio_get_disk(disk_id);

    if (!disk || !out) {
        return -1;
    }

    out->id = disk->disk_id;
    out->block_size = disk->block_size;
    out->block_count = disk->block_count;
    out->size_bytes = disk->image_size;
    out->read_only = (uint8_t)disk->read_only;
    out->is_boot = (uint8_t)disk->is_boot;
    str_copy(out->name, disk->name, sizeof(out->name));
    out->fs_name[0] = '\0';
    return 0;
}

int blockio_list_disks(myaos_disk_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t count = g_blockio_disk_count;

    if (!out_count) {
        return -1;
    }
    if (count > max_entries) {
        count = max_entries;
    }

    for (uint32_t i = 0; i < count; i++) {
        (void)blockio_disk_info(i, &out[i]);
    }
    *out_count = count;
    return 0;
}

int blockio_hotplug_ramdisk(uint64_t size_bytes, uint32_t block_size, uint32_t* out_disk_id) {
    blockio_disk_t* disk;
    uint32_t disk_id;

    if (!out_disk_id || size_bytes == 0u) {
        return -1;
    }
    if (g_blockio_disk_count >= BLOCKIO_MAX_DISKS) {
        return -1;
    }
    if (block_size == 0u) {
        block_size = BLOCKIO_DEFAULT_BLOCK_SIZE;
    }
    if ((size_bytes % block_size) != 0u || size_bytes < block_size) {
        return -1;
    }
    if (size_bytes > (64ull * 1024ull * 1024ull)) {
        return -1;
    }

    disk_id = g_blockio_disk_count;
    disk = &g_blockio_disks[disk_id];
    disk->boot = NULL;
    disk->disk_id = disk_id;
    disk->image_base = (uint8_t*)kmalloc((size_t)size_bytes);
    if (!disk->image_base) {
        return -1;
    }
    mem_zero(disk->image_base, size_bytes);

    disk->image_size = size_bytes;
    disk->efi_block_io = 0u;
    disk->lba_start = 0u;
    disk->block_count = size_bytes / block_size;
    disk->media_id = 0u;
    disk->block_size = block_size;
    disk->read_only = 0u;
    disk->is_boot = 0u;
    build_disk_name(disk_id, disk->name, sizeof(disk->name));

    g_blockio_ctx[disk_id].disk = disk;
    if (device_register(
            MYAOS_DEV_BLOCK,
            disk->name,
            "ramdisk-hotplug",
            &g_blockio_ctx[disk_id],
            &g_blockio_ops,
            NULL
        ) != 0) {
        kfree(disk->image_base);
        disk->image_base = NULL;
        return -1;
    }

    g_blockio_disk_count++;
    *out_disk_id = disk_id;
    return 0;
}

int blockio_writeback_disk(const blockio_disk_t* disk) {
    if (!disk || !disk->boot || disk->boot->boot_services_active == 0) {
        return -1;
    }
    if (disk->read_only) {
        return -2;
    }
    if (disk->efi_block_io == 0 || !disk->image_base) {
        return -3;
    }
    if (disk->block_size == 0 || disk->block_count == 0) {
        return -3;
    }

    efi_block_io_protocol_t* block_io = (efi_block_io_protocol_t*)(uintptr_t)disk->efi_block_io;
    if (!block_io->write_blocks) {
        return -3;
    }

    uint8_t* ptr = disk->image_base;
    uint64_t lba = disk->lba_start;
    uint64_t remaining_blocks = disk->block_count;
    uint64_t block_size = disk->block_size;
    const uint64_t chunk_blocks = 128;

    while (remaining_blocks > 0) {
        uint64_t blocks_now = (remaining_blocks < chunk_blocks) ? remaining_blocks : chunk_blocks;
        uint64_t bytes_now = blocks_now * block_size;

        efi_status_t status = block_io->write_blocks(
            block_io,
            disk->media_id,
            lba,
            bytes_now,
            (void*)ptr
        );
        if (status != 0) {
            return -4;
        }

        ptr += bytes_now;
        lba += blocks_now;
        remaining_blocks -= blocks_now;
    }

    if (block_io->flush_blocks) {
        efi_status_t flush_status = block_io->flush_blocks(block_io);
        if (flush_status != 0) {
            return -5;
        }
    }

    return 0;
}

int blockio_writeback(boot_info_t* boot) {
    (void)boot;
    return blockio_writeback_disk(blockio_get_disk(0));
}
