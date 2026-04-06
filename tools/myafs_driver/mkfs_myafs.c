#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "myafs_driver.h"
#include "../../include/myafs/ondisk.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif
#endif

static int str_eq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int str_eq_ci(const char* a, const char* b) {
    size_t i = 0;

    while (a[i] && b[i]) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

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

static int parse_u64(const char* text, uint64_t* out) {
    uint64_t value = 0;
    size_t i = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }

    while (text[i]) {
        char c = text[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        if (value > (UINT64_MAX / 10u)) {
            return -1;
        }
        value = value * 10u + (uint64_t)(c - '0');
        i++;
    }

    *out = value;
    return 0;
}

typedef struct {
    int is_block_device;
    uint64_t byte_size;
} mkfs_target_info_t;

static int probe_target(const char* path, mkfs_target_info_t* out) {
    if (!path || !out) {
        return -1;
    }

    out->is_block_device = 0;
    out->byte_size = 0;

#if !defined(_WIN32)
    {
        struct stat st;
        if (stat(path, &st) != 0) {
            return 0;
        }
        if (S_ISBLK(st.st_mode)) {
            out->is_block_device = 1;
        }
    }
#endif

#if defined(__linux__)
    if (out->is_block_device) {
        FILE* fp = fopen(path, "rb");
        if (fp) {
            int fd = fileno(fp);
            unsigned long long bytes = 0;
            if (fd >= 0 && ioctl(fd, BLKGETSIZE64, &bytes) == 0) {
                out->byte_size = (uint64_t)bytes;
            }
            fclose(fp);
        }
    }
#endif

    return 0;
}

static int path_is_mounted(const char* path) {
#if defined(__linux__)
    FILE* fp;
    char line[1024];

    if (!path || !path[0]) {
        return 0;
    }

    fp = fopen("/proc/mounts", "r");
    if (!fp) {
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        char dev[512];
        char mnt[512];
        if (sscanf(line, "%511s %511s", dev, mnt) == 2) {
            if (strcmp(dev, path) == 0) {
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
#else
    (void)path;
#endif
    return 0;
}

static uint32_t crc32c_bytes(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xffffffffu;

    for (size_t i = 0; i < size; i++) {
        crc ^= (uint32_t)p[i];
        for (uint32_t b = 0; b < 8u; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0x82f63b78u & mask);
        }
    }

    return ~crc;
}

static void fill_uuid(uint8_t out[16]) {
    FILE* urandom = fopen("/dev/urandom", "rb");
    size_t got = 0;

    if (urandom) {
        got = fread(out, 1u, 16u, urandom);
        fclose(urandom);
    }

    if (got != 16u) {
        uint64_t t = (uint64_t)time(NULL);
        uint64_t mix = (uint64_t)(uintptr_t)&out ^ ((uint64_t)getpid() << 32u);
        for (uint32_t i = 0; i < 16u; i++) {
            t = (t * 6364136223846793005ull) + 1ull + mix;
            out[i] = (uint8_t)(t >> 24u);
        }
    }

    out[6] = (uint8_t)((out[6] & 0x0fu) | 0x40u);
    out[8] = (uint8_t)((out[8] & 0x3fu) | 0x80u);
}

static uint64_t now_unix_ns(void) {
    time_t t = time(NULL);
    if (t < 0) {
        return 0;
    }
    return (uint64_t)t * 1000000000ull;
}

static void write_magic8(uint8_t out[8], const char* magic7) {
    memset(out, 0, 8u);
    if (magic7) {
        size_t len = str_len(magic7);
        if (len > 8u) {
            len = 8u;
        }
        memcpy(out, magic7, len);
    }
}

static int file_seek_abs(FILE* fp, uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(fp, (__int64)offset, SEEK_SET);
#else
    if (offset > (uint64_t)LONG_MAX) {
        return -1;
    }
    return fseek(fp, (long)offset, SEEK_SET);
#endif
}

static int write_block_struct(
    FILE* fp,
    uint64_t block_index,
    uint32_t block_size,
    const void* obj,
    size_t obj_size
) {
    uint8_t* block_buf;

    if (!fp || !obj || obj_size > block_size) {
        return -1;
    }

    block_buf = (uint8_t*)calloc(1u, block_size);
    if (!block_buf) {
        return -1;
    }
    memcpy(block_buf, obj, obj_size);

    if (file_seek_abs(fp, block_index * (uint64_t)block_size) != 0) {
        free(block_buf);
        return -1;
    }
    if (fwrite(block_buf, 1u, block_size, fp) != block_size) {
        free(block_buf);
        return -1;
    }
    free(block_buf);
    return 0;
}

static int zero_blocks(FILE* fp, uint64_t first_block, uint64_t block_count, uint32_t block_size) {
    uint8_t* zero;

    if (!fp || block_size == 0u) {
        return -1;
    }
    if (block_count == 0u) {
        return 0;
    }

    zero = (uint8_t*)calloc(1u, block_size);
    if (!zero) {
        return -1;
    }

    for (uint64_t i = 0; i < block_count; i++) {
        if (file_seek_abs(fp, (first_block + i) * (uint64_t)block_size) != 0) {
            free(zero);
            return -1;
        }
        if (fwrite(zero, 1u, block_size, fp) != block_size) {
            free(zero);
            return -1;
        }
    }

    free(zero);
    return 0;
}

static int mkfs_v2(const char* image_path, const char* label, uint64_t size_mib, const mkfs_target_info_t* target) {
    FILE* fp;
    uint64_t image_bytes;
    uint32_t block_size = MYAFS_V2_DEFAULT_BLOCK_SIZE;
    uint64_t total_blocks;
    uint64_t boot_start = 0u;
    uint64_t boot_blocks = MYAFS_V2_BOOT_BLOCKS;
    uint64_t super_start = boot_start + boot_blocks;
    uint32_t super_count = MYAFS_V2_SUPERBLOCK_RING_BLOCKS;
    uint64_t checkpoint_start = super_start + super_count;
    uint32_t checkpoint_count = MYAFS_V2_CHECKPOINT_RING_BLOCKS;
    uint32_t checkpoint_blocks_per_entry = 1u;
    uint64_t allocator_start = checkpoint_start + checkpoint_count;
    uint64_t allocator_blocks = MYAFS_V2_ALLOCATOR_AREA_BLOCKS;
    uint64_t tree_area_start = allocator_start + allocator_blocks;
    uint64_t object_tree_start = tree_area_start;
    uint64_t extent_tree_start = tree_area_start + 1u;
    uint64_t directory_tree_start = tree_area_start + 2u;
    uint64_t metadata_end = tree_area_start + MYAFS_V2_TREE_AREA_BLOCKS;
    uint64_t data_start;
    uint64_t recovery_log_start;
    uint64_t recovery_log_blocks = MYAFS_V2_RECOVERY_LOG_BLOCKS;
    uint64_t snapshot_meta_start;
    uint64_t snapshot_meta_blocks = 0u;
    uint8_t fs_uuid[16];
    myafs_checkpoint_v2_t checkpoint;
    int rc = -1;
    int is_block_target = (target && target->is_block_device) ? 1 : 0;

    if (!image_path || !image_path[0]) {
        return -1;
    }

    if (is_block_target) {
        if (target->byte_size == 0u) {
            fprintf(stderr, "mkfs.myafs: failed to query block device size for %s\n", image_path);
            return -1;
        }
        if (size_mib == 0u) {
            image_bytes = target->byte_size;
        } else {
            image_bytes = size_mib * 1024ull * 1024ull;
            if (image_bytes > target->byte_size) {
                fprintf(stderr, "mkfs.myafs: requested size exceeds device capacity\n");
                return -1;
            }
        }
        image_bytes -= (image_bytes % block_size);
        fp = fopen(image_path, "r+b");
    } else {
        if (size_mib == 0u) {
            size_mib = MYAFS_V2_DEFAULT_IMAGE_MIB;
        }
        image_bytes = size_mib * 1024ull * 1024ull;
        if ((image_bytes % block_size) != 0u) {
            return -1;
        }
        fp = fopen(image_path, "w+b");
    }

    if (!fp) {
        fprintf(stderr, "mkfs.myafs: open failed: %s\n", strerror(errno));
        return -1;
    }

#if !defined(_WIN32)
    if (!is_block_target) {
        int fd = fileno(fp);
        if (fd < 0 || ftruncate(fd, (off_t)image_bytes) != 0) {
            fprintf(stderr, "mkfs.myafs: size allocation failed\n");
            goto out;
        }
    }
#endif

    total_blocks = image_bytes / block_size;
    recovery_log_start = total_blocks - recovery_log_blocks - snapshot_meta_blocks;
    snapshot_meta_start = recovery_log_start + recovery_log_blocks;
    data_start = metadata_end;

    if (total_blocks < snapshot_meta_start + snapshot_meta_blocks || data_start >= recovery_log_start) {
        fprintf(stderr, "mkfs.myafs: image too small for v2 layout\n");
        goto out;
    }

    if (zero_blocks(fp, 0u, metadata_end, block_size) != 0) {
        fprintf(stderr, "mkfs.myafs: metadata zeroing failed\n");
        goto out;
    }

    fill_uuid(fs_uuid);

    memset(&checkpoint, 0, sizeof(checkpoint));
    write_magic8(checkpoint.magic, MYAFS_CHECKPOINT_MAGIC);
    checkpoint.version_major = MYAFS_ONDISK_VERSION_MAJOR;
    checkpoint.version_minor = MYAFS_ONDISK_VERSION_MINOR;
    checkpoint.header_bytes = (uint32_t)sizeof(checkpoint);
    checkpoint.transaction_id = 1u;
    checkpoint.generation = 1u;
    checkpoint.timestamp_unix_ns = now_unix_ns();
    memcpy(checkpoint.fs_uuid, fs_uuid, sizeof(checkpoint.fs_uuid));
    checkpoint.checksum = 0u;
    checkpoint.checksum = crc32c_bytes(&checkpoint, sizeof(checkpoint));

    if (write_block_struct(fp, checkpoint_start, block_size, &checkpoint, sizeof(checkpoint)) != 0) {
        fprintf(stderr, "mkfs.myafs: failed to write checkpoint\n");
        goto out;
    }

    for (uint32_t i = 0; i < super_count; i++) {
        myafs_superblock_v2_t sb;
        memset(&sb, 0, sizeof(sb));
        write_magic8(sb.magic, MYAFS_SUPERBLOCK_MAGIC);
        sb.version_major = MYAFS_ONDISK_VERSION_MAJOR;
        sb.version_minor = MYAFS_ONDISK_VERSION_MINOR;
        sb.header_bytes = (uint32_t)sizeof(sb);
        sb.block_size = block_size;
        sb.super_index = i;
        sb.total_blocks = total_blocks;

        sb.boot_start = boot_start;
        sb.boot_blocks = boot_blocks;

        sb.super_start = super_start;
        sb.super_count = super_count;
        sb.active_checkpoint = 0u;

        sb.checkpoint_start = checkpoint_start;
        sb.checkpoint_count = checkpoint_count;
        sb.checkpoint_blocks_per_entry = checkpoint_blocks_per_entry;

        sb.allocator_start = allocator_start;
        sb.allocator_blocks = allocator_blocks;

        sb.object_tree_start = object_tree_start;
        sb.extent_tree_start = extent_tree_start;
        sb.directory_tree_start = directory_tree_start;
        sb.data_start = data_start;

        sb.recovery_log_start = recovery_log_start;
        sb.recovery_log_blocks = recovery_log_blocks;
        sb.snapshot_meta_start = snapshot_meta_start;
        sb.snapshot_meta_blocks = snapshot_meta_blocks;

        memcpy(sb.fs_uuid, fs_uuid, sizeof(sb.fs_uuid));
        str_copy(sb.label, label ? label : "myafs", sizeof(sb.label));

        sb.generation = 1u;
        sb.transaction_id = 1u;
        sb.checkpoint_timestamp_unix_ns = checkpoint.timestamp_unix_ns;

        sb.checksum = 0u;
        sb.checksum = crc32c_bytes(&sb, sizeof(sb));

        if (write_block_struct(fp, super_start + i, block_size, &sb, sizeof(sb)) != 0) {
            fprintf(stderr, "mkfs.myafs: failed to write superblock copy %u\n", i);
            goto out;
        }
    }

    if (fflush(fp) != 0) {
        fprintf(stderr, "mkfs.myafs: flush failed\n");
        goto out;
    }

    printf(
        "mkfs.myafs: created %s (format=v2 label=%s size=%llu MiB%s)\n",
        image_path,
        label ? label : "myafs",
        (unsigned long long)(image_bytes / (1024ull * 1024ull)),
        is_block_target ? " target=block-device" : ""
    );
    rc = 0;

out:
    fclose(fp);
    return rc;
}

static void usage(const char* argv0) {
    fprintf(stderr, "usage: %s [--format legacy|v2] [-L LABEL] [-s SIZE_MIB] IMAGE\n", argv0);
    fprintf(stderr, "example: %s --format legacy -L MyaDisk disk.mya\n", argv0);
    fprintf(stderr, "example: %s --format v2 -L MyaDisk -s 128 disk_v2.mya\n", argv0);
    fprintf(stderr, "example: %s /dev/sdb   (auto-selects v2 for block devices)\n", argv0);
}

int main(int argc, char** argv) {
    const char* label = "myafs";
    const char* image = NULL;
    const char* format = NULL;
    uint64_t size_mib = 0u;
    mkfs_target_info_t target_info;
    myafs_driver_t fs;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (str_eq(arg, "-L") || str_eq(arg, "--label")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires a value\n", arg);
                usage(argv[0]);
                return 1;
            }
            label = argv[++i];
            continue;
        }
        if (str_eq(arg, "--format")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --format requires a value\n");
                usage(argv[0]);
                return 1;
            }
            format = argv[++i];
            continue;
        }
        if (str_eq(arg, "-s") || str_eq(arg, "--size-mib")) {
            if (i + 1 >= argc || parse_u64(argv[i + 1], &size_mib) != 0) {
                fprintf(stderr, "error: %s requires numeric MiB value\n", arg);
                usage(argv[0]);
                return 1;
            }
            i++;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "error: unknown option: %s\n", arg);
            usage(argv[0]);
            return 1;
        }
        if (!image) {
            image = arg;
            continue;
        }

        fprintf(stderr, "error: unexpected argument: %s\n", arg);
        usage(argv[0]);
        return 1;
    }

    if (!image) {
        usage(argv[0]);
        return 1;
    }

    if (probe_target(image, &target_info) != 0) {
        fprintf(stderr, "mkfs.myafs: target probe failed\n");
        return 1;
    }

    if (!format) {
        format = target_info.is_block_device ? "v2" : "legacy";
    }

#if !defined(_WIN32)
    if (target_info.is_block_device) {
        if (geteuid() != 0) {
            fprintf(stderr, "mkfs.myafs: block device formatting requires root\n");
            return 1;
        }
        if (path_is_mounted(image)) {
            fprintf(stderr, "mkfs.myafs: %s appears mounted; unmount it first\n", image);
            return 1;
        }
    }
#endif

    if (str_eq_ci(format, "legacy")) {
        if (target_info.is_block_device) {
            fprintf(stderr, "mkfs.myafs: legacy format is not supported on block devices\n");
            fprintf(stderr, "use: mkfs.myafs --format v2 %s\n", image);
            return 1;
        }
        if (myafs_driver_init(&fs, label) != 0) {
            fprintf(stderr, "mkfs.myafs: init failed\n");
            return 1;
        }

        if (myafs_driver_save(&fs, image) != 0) {
            fprintf(stderr, "mkfs.myafs: failed to write image: %s\n", image);
            return 1;
        }

        printf("mkfs.myafs: created %s (format=legacy label=%s)\n", image, label);
        return 0;
    }

    if (str_eq_ci(format, "v2")) {
        return mkfs_v2(image, label, size_mib, &target_info) == 0 ? 0 : 1;
    }

    fprintf(stderr, "mkfs.myafs: unknown format: %s\n", format);
    usage(argv[0]);
    return 1;
}
