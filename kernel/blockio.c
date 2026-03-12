#include "blockio.h"
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

int blockio_writeback(boot_info_t* boot) {
    if (!boot || boot->boot_services_active == 0) {
        return -1;
    }
    if (boot->boot_disk_read_only) {
        return -2;
    }
    if (boot->efi_block_io == 0 || boot->boot_disk_base == 0) {
        return -3;
    }
    if (boot->boot_disk_block_size == 0 || boot->boot_disk_block_count == 0) {
        return -3;
    }

    efi_block_io_protocol_t* block_io = (efi_block_io_protocol_t*)(uintptr_t)boot->efi_block_io;
    if (!block_io->write_blocks) {
        return -3;
    }

    uint8_t* ptr = (uint8_t*)(uintptr_t)boot->boot_disk_base;
    uint64_t lba = boot->boot_disk_lba_start;
    uint64_t remaining_blocks = boot->boot_disk_block_count;
    uint64_t block_size = boot->boot_disk_block_size;
    const uint64_t chunk_blocks = 128;

    while (remaining_blocks > 0) {
        uint64_t blocks_now = (remaining_blocks < chunk_blocks) ? remaining_blocks : chunk_blocks;
        uint64_t bytes_now = blocks_now * block_size;

        efi_status_t status = block_io->write_blocks(
            block_io,
            boot->boot_disk_media_id,
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
