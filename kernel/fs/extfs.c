#include "extfs.h"
#include <stddef.h>

#define EXT_SUPER_OFFSET 1024u
#define EXT_SUPER_MAGIC 0xEF53u

#define EXT_FEATURE_COMPAT_HAS_JOURNAL 0x0004u

#define EXT_FEATURE_INCOMPAT_FILETYPE 0x0002u
#define EXT_FEATURE_INCOMPAT_RECOVER 0x0004u
#define EXT_FEATURE_INCOMPAT_EXTENTS 0x0040u
#define EXT_FEATURE_INCOMPAT_64BIT 0x0080u

#define EXT_FEATURE_RO_COMPAT_HUGE_FILE 0x0008u
#define EXT_FEATURE_RO_COMPAT_METADATA_CSUM 0x0400u

#define EXT_S_IFDIR 0x4000u
#define EXT_S_IFREG 0x8000u
#define EXT_I_FLAG_EXTENTS 0x00080000u

#define EXT_EXTENT_MAGIC 0xF30Au

#define EXT_FILETYPE_UNKNOWN 0u
#define EXT_FILETYPE_REG 1u
#define EXT_FILETYPE_DIR 2u

typedef struct {
    uint64_t block;
    uint16_t off;
    uint16_t rec_len;
    uint32_t inode;
    uint8_t file_type;
    uint8_t found;
} extfs_dir_loc_t;

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
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

    if (!dst || dst_size == 0) {
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

static int str_eq_n(const char* a, const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if ((uint8_t)a[i] != b[i]) {
            return 0;
        }
    }
    return a[n] == '\0';
}

static void mem_copy(void* dst, const void* src, size_t n) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;

    for (size_t i = 0; i < n; i++) {
        out[i] = in[i];
    }
}

static void mem_zero(void* dst, size_t n) {
    uint8_t* out = (uint8_t*)dst;

    for (size_t i = 0; i < n; i++) {
        out[i] = 0;
    }
}

static uint16_t align4(uint16_t value) {
    return (uint16_t)((value + 3u) & ~3u);
}

static int name_valid_for_create(const char* name) {
    size_t len = str_len(name);

    if (!name || len == 0 || len > 255) {
        return 0;
    }
    if (len == 1 && name[0] == '.') {
        return 0;
    }
    if (len == 2 && name[0] == '.' && name[1] == '.') {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == '\0') {
            return 0;
        }
    }

    return 1;
}

static int detect_kind(uint32_t feature_compat, uint32_t feature_incompat, uint32_t feature_ro_compat) {
    if ((feature_incompat & EXT_FEATURE_INCOMPAT_EXTENTS) != 0 ||
        (feature_incompat & EXT_FEATURE_INCOMPAT_64BIT) != 0 ||
        (feature_ro_compat & EXT_FEATURE_RO_COMPAT_HUGE_FILE) != 0) {
        return 4;
    }
    if ((feature_compat & EXT_FEATURE_COMPAT_HAS_JOURNAL) != 0) {
        return 3;
    }
    return 2;
}

static int read_super(extfs_t* fs, uint8_t* image, uint64_t image_size) {
    const uint8_t* sb;
    uint32_t log_block_size;
    uint16_t desc_size;
    uint32_t inode_size;
    uint32_t first_inode;
    uint32_t blocks_lo;
    uint32_t blocks_hi = 0;
    uint64_t group_count;
    uint64_t gdt_off;
    uint64_t gdt_span;
    uint8_t needs_recovery;
    uint8_t metadata_csum;
    uint8_t has_extents;
    uint8_t has_64bit;
    uint8_t has_journal;

    if (!fs || !image || image_size < EXT_SUPER_OFFSET + 1024u) {
        return -1;
    }

    sb = image + EXT_SUPER_OFFSET;
    if (rd16(sb + 0x38u) != EXT_SUPER_MAGIC) {
        return -1;
    }

    log_block_size = rd32(sb + 0x18u);
    if (log_block_size > 2u) {
        return -1;
    }

    mem_zero(fs, sizeof(*fs));
    fs->image = image;
    fs->image_size = image_size;
    fs->block_size = 1024u << log_block_size;
    fs->inodes_count = rd32(sb + 0x00u);
    fs->blocks_per_group = rd32(sb + 0x20u);
    fs->inodes_per_group = rd32(sb + 0x28u);
    fs->first_data_block = rd32(sb + 0x14u);
    fs->feature_compat = rd32(sb + 0x5Cu);
    fs->feature_incompat = rd32(sb + 0x60u);
    fs->feature_ro_compat = rd32(sb + 0x64u);

    if (image_size >= EXT_SUPER_OFFSET + 0x154u) {
        blocks_hi = rd32(sb + 0x150u);
    }
    blocks_lo = rd32(sb + 0x04u);
    fs->blocks_count = (uint64_t)blocks_lo | ((uint64_t)blocks_hi << 32);

    inode_size = rd16(sb + 0x58u);
    fs->inode_size = (inode_size >= 128u) ? inode_size : 128u;
    first_inode = rd32(sb + 0x54u);
    fs->first_inode = first_inode ? first_inode : 11u;

    desc_size = rd16(sb + 0xFEu);
    fs->desc_size = (desc_size >= 32u) ? desc_size : 32u;

    if (fs->block_size < 1024u || fs->block_size > 4096u ||
        fs->blocks_count == 0 || fs->inodes_count == 0 ||
        fs->blocks_per_group == 0 || fs->inodes_per_group == 0 ||
        fs->inode_size > fs->block_size || fs->desc_size > fs->block_size) {
        return -1;
    }

    group_count = (fs->blocks_count + fs->blocks_per_group - 1u) / fs->blocks_per_group;
    if (group_count == 0 || group_count > 0xFFFFFFFFu) {
        return -1;
    }
    fs->group_count = (uint32_t)group_count;
    fs->gdt_block = (fs->block_size == 1024u) ? 2u : 1u;

    gdt_off = (uint64_t)fs->gdt_block * fs->block_size;
    gdt_span = (uint64_t)fs->group_count * fs->desc_size;
    if (gdt_off + gdt_span > fs->image_size) {
        return -1;
    }

    fs->fs_kind = (uint8_t)detect_kind(fs->feature_compat, fs->feature_incompat, fs->feature_ro_compat);

    needs_recovery = (fs->feature_incompat & EXT_FEATURE_INCOMPAT_RECOVER) != 0;
    metadata_csum = (fs->feature_ro_compat & EXT_FEATURE_RO_COMPAT_METADATA_CSUM) != 0;
    has_extents = (fs->feature_incompat & EXT_FEATURE_INCOMPAT_EXTENTS) != 0;
    has_64bit = (fs->feature_incompat & EXT_FEATURE_INCOMPAT_64BIT) != 0;
    has_journal = (fs->feature_compat & EXT_FEATURE_COMPAT_HAS_JOURNAL) != 0;

    if (needs_recovery) {
        fs->write_level = 0;
    } else if (has_extents || has_64bit || has_journal || metadata_csum) {
        fs->write_level = 1;
    } else {
        fs->write_level = 2;
    }

    fs->mounted = 1;
    fs->dirty = 0;
    return 0;
}

static uint64_t group_first_block(const extfs_t* fs, uint32_t group) {
    return (uint64_t)fs->first_data_block + (uint64_t)group * fs->blocks_per_group;
}

static uint32_t group_blocks_count(const extfs_t* fs, uint32_t group) {
    uint64_t first = group_first_block(fs, group);
    uint64_t remaining;

    if (first >= fs->blocks_count) {
        return 0;
    }

    remaining = fs->blocks_count - first;
    return (remaining > fs->blocks_per_group) ? fs->blocks_per_group : (uint32_t)remaining;
}

static uint32_t group_inodes_count(const extfs_t* fs, uint32_t group) {
    uint64_t first = (uint64_t)group * fs->inodes_per_group;
    uint64_t remaining;

    if (first >= fs->inodes_count) {
        return 0;
    }

    remaining = fs->inodes_count - first;
    return (remaining > fs->inodes_per_group) ? fs->inodes_per_group : (uint32_t)remaining;
}

static const uint8_t* group_desc_const(const extfs_t* fs, uint32_t group) {
    uint64_t off;

    if (!fs || !fs->mounted || group >= fs->group_count) {
        return NULL;
    }

    off = (uint64_t)fs->gdt_block * fs->block_size + (uint64_t)group * fs->desc_size;
    if (off + fs->desc_size > fs->image_size) {
        return NULL;
    }
    return fs->image + off;
}

static uint64_t desc_block_num(const extfs_t* fs, const uint8_t* gd, uint32_t lo_off, uint32_t hi_off) {
    uint64_t value;

    if (!gd) {
        return 0;
    }

    value = rd32(gd + lo_off);
    if ((fs->feature_incompat & EXT_FEATURE_INCOMPAT_64BIT) != 0 && fs->desc_size >= hi_off + 4u) {
        value |= (uint64_t)rd32(gd + hi_off) << 32;
    }
    return value;
}

static uint64_t desc_block_bitmap(const extfs_t* fs, const uint8_t* gd) {
    return desc_block_num(fs, gd, 0u, 32u);
}

static uint64_t desc_inode_bitmap(const extfs_t* fs, const uint8_t* gd) {
    return desc_block_num(fs, gd, 4u, 36u);
}

static uint64_t desc_inode_table(const extfs_t* fs, const uint8_t* gd) {
    return desc_block_num(fs, gd, 8u, 40u);
}

static const uint8_t* block_ptr_const(const extfs_t* fs, uint64_t block) {
    uint64_t off;

    if (!fs || !fs->mounted || block == 0 || block >= fs->blocks_count) {
        return NULL;
    }

    off = block * fs->block_size;
    if (off + fs->block_size > fs->image_size) {
        return NULL;
    }
    return fs->image + off;
}

static uint8_t* block_ptr_mut(extfs_t* fs, uint64_t block) {
    uint64_t off;

    if (!fs || !fs->mounted || block == 0 || block >= fs->blocks_count) {
        return NULL;
    }

    off = block * fs->block_size;
    if (off + fs->block_size > fs->image_size) {
        return NULL;
    }
    return fs->image + off;
}

static int inode_ptr_const(const extfs_t* fs, uint32_t inode, const uint8_t** out, uint32_t* out_group, uint32_t* out_index) {
    const uint8_t* gd;
    uint64_t table_block;
    uint64_t off;
    uint32_t idx;
    uint32_t group;
    uint32_t local_index;

    if (!fs || !out || inode == 0 || inode > fs->inodes_count) {
        return -1;
    }

    idx = inode - 1u;
    group = idx / fs->inodes_per_group;
    local_index = idx % fs->inodes_per_group;

    gd = group_desc_const(fs, group);
    if (!gd) {
        return -1;
    }

    table_block = desc_inode_table(fs, gd);
    off = table_block * fs->block_size + (uint64_t)local_index * fs->inode_size;
    if (off + fs->inode_size > fs->image_size) {
        return -1;
    }

    *out = fs->image + off;
    if (out_group) {
        *out_group = group;
    }
    if (out_index) {
        *out_index = local_index;
    }
    return 0;
}

static int inode_ptr_mut(extfs_t* fs, uint32_t inode, uint8_t** out, uint32_t* out_group, uint32_t* out_index) {
    const uint8_t* ptr = NULL;

    if (!out) {
        return -1;
    }
    if (inode_ptr_const(fs, inode, &ptr, out_group, out_index) != 0) {
        return -1;
    }
    *out = (uint8_t*)ptr;
    return 0;
}

static uint16_t inode_mode(const uint8_t* inode) {
    return rd16(inode + 0u);
}

static uint32_t inode_flags(const uint8_t* inode) {
    return rd32(inode + 32u);
}

static uint64_t inode_size_bytes(const extfs_t* fs, const uint8_t* inode) {
    uint64_t lo = rd32(inode + 4u);
    uint64_t hi = 0;

    if (fs->inode_size >= 112u) {
        hi = rd32(inode + 108u);
    }

    return lo | (hi << 32);
}

static void inode_set_size_bytes(const extfs_t* fs, uint8_t* inode, uint64_t size) {
    (void)fs;
    wr32(inode + 4u, (uint32_t)(size & 0xFFFFFFFFu));
    if (fs->inode_size >= 112u) {
        wr32(inode + 108u, (uint32_t)((size >> 32) & 0xFFFFFFFFu));
    }
}

static uint16_t inode_links(const uint8_t* inode) {
    return rd16(inode + 26u);
}

static void inode_set_links(uint8_t* inode, uint16_t links) {
    wr16(inode + 26u, links);
}

static uint32_t inode_ptr_entry(const uint8_t* inode, uint32_t index) {
    if (index >= 15u) {
        return 0;
    }
    return rd32(inode + 40u + index * 4u);
}

static void inode_set_ptr_entry(uint8_t* inode, uint32_t index, uint32_t value) {
    if (index >= 15u) {
        return;
    }
    wr32(inode + 40u + index * 4u, value);
}

static int inode_is_dir_mode(uint16_t mode) {
    return (mode & 0xF000u) == EXT_S_IFDIR;
}

static int inode_is_file_mode(uint16_t mode) {
    return (mode & 0xF000u) == EXT_S_IFREG;
}

static void inode_update_block_sectors(uint8_t* inode, uint64_t bytes) {
    uint64_t sectors = (bytes + 511u) / 512u;
    wr32(inode + 28u, (uint32_t)(sectors & 0xFFFFFFFFu));
}

static int bitmap_get(const uint8_t* bitmap, uint32_t bit) {
    return (bitmap[bit / 8u] >> (bit % 8u)) & 1u;
}

static void bitmap_set(uint8_t* bitmap, uint32_t bit, int value) {
    uint8_t mask = (uint8_t)(1u << (bit % 8u));
    if (value) {
        bitmap[bit / 8u] |= mask;
    } else {
        bitmap[bit / 8u] &= (uint8_t)~mask;
    }
}

static int read_u32_from_block(const extfs_t* fs, uint64_t block, uint32_t index, uint32_t* out_value) {
    const uint8_t* ptr = block_ptr_const(fs, block);
    uint32_t per_block;

    if (!ptr || !out_value) {
        return -1;
    }
    per_block = fs->block_size / 4u;
    if (index >= per_block) {
        return -1;
    }

    *out_value = rd32(ptr + index * 4u);
    return 0;
}

static int write_u32_to_block(extfs_t* fs, uint64_t block, uint32_t index, uint32_t value) {
    uint8_t* ptr = block_ptr_mut(fs, block);
    uint32_t per_block;

    if (!ptr) {
        return -1;
    }
    per_block = fs->block_size / 4u;
    if (index >= per_block) {
        return -1;
    }

    wr32(ptr + index * 4u, value);
    return 0;
}

static int inode_get_data_block_legacy(const extfs_t* fs, const uint8_t* inode, uint32_t logical_block, uint64_t* out_block) {
    uint32_t per_block = fs->block_size / 4u;
    uint64_t lbn = logical_block;
    uint32_t ptr;
    uint32_t idx1;
    uint32_t idx2;

    if (!out_block || per_block == 0) {
        return -1;
    }

    if (lbn < 12u) {
        *out_block = inode_ptr_entry(inode, (uint32_t)lbn);
        return 0;
    }

    lbn -= 12u;
    if (lbn < per_block) {
        ptr = inode_ptr_entry(inode, 12u);
        if (ptr == 0) {
            *out_block = 0;
            return 0;
        }
        if (read_u32_from_block(fs, ptr, (uint32_t)lbn, &ptr) != 0) {
            return -1;
        }
        *out_block = ptr;
        return 0;
    }

    lbn -= per_block;
    if (lbn < (uint64_t)per_block * per_block) {
        ptr = inode_ptr_entry(inode, 13u);
        if (ptr == 0) {
            *out_block = 0;
            return 0;
        }

        idx1 = (uint32_t)(lbn / per_block);
        idx2 = (uint32_t)(lbn % per_block);

        if (read_u32_from_block(fs, ptr, idx1, &ptr) != 0) {
            return -1;
        }
        if (ptr == 0) {
            *out_block = 0;
            return 0;
        }
        if (read_u32_from_block(fs, ptr, idx2, &ptr) != 0) {
            return -1;
        }
        *out_block = ptr;
        return 0;
    }

    lbn -= (uint64_t)per_block * per_block;
    if (lbn < (uint64_t)per_block * per_block * per_block) {
        uint32_t idx3;
        uint32_t ptr2;

        ptr = inode_ptr_entry(inode, 14u);
        if (ptr == 0) {
            *out_block = 0;
            return 0;
        }

        idx1 = (uint32_t)(lbn / ((uint64_t)per_block * per_block));
        idx2 = (uint32_t)((lbn / per_block) % per_block);
        idx3 = (uint32_t)(lbn % per_block);

        if (read_u32_from_block(fs, ptr, idx1, &ptr2) != 0 || ptr2 == 0) {
            *out_block = 0;
            return 0;
        }
        if (read_u32_from_block(fs, ptr2, idx2, &ptr) != 0 || ptr == 0) {
            *out_block = 0;
            return 0;
        }
        if (read_u32_from_block(fs, ptr, idx3, &ptr) != 0) {
            return -1;
        }
        *out_block = ptr;
        return 0;
    }

    return -1;
}

static int extent_lookup_node(const extfs_t* fs, const uint8_t* node, uint32_t node_size, uint32_t logical_block, uint32_t guard, uint64_t* out_block) {
    uint16_t magic;
    uint16_t entries;
    uint16_t depth;
    uint32_t chosen;
    uint8_t found = 0;

    if (!node || !out_block || node_size < 12u || guard > 8u) {
        return -1;
    }

    magic = rd16(node + 0u);
    entries = rd16(node + 2u);
    depth = rd16(node + 6u);
    if (magic != EXT_EXTENT_MAGIC) {
        return -1;
    }
    if (12u + (uint32_t)entries * 12u > node_size) {
        return -1;
    }

    if (depth == 0) {
        for (uint32_t i = 0; i < entries; i++) {
            const uint8_t* ex = node + 12u + i * 12u;
            uint32_t start = rd32(ex + 0u);
            uint16_t len_raw = rd16(ex + 4u);
            uint16_t len = (uint16_t)(len_raw & 0x7FFFu);
            uint64_t phys_start;

            if (len == 0) {
                continue;
            }
            if (logical_block < start || logical_block >= start + len) {
                continue;
            }

            if ((len_raw & 0x8000u) != 0) {
                *out_block = 0;
                return 0;
            }

            phys_start = ((uint64_t)rd16(ex + 6u) << 32) | rd32(ex + 8u);
            *out_block = phys_start + (logical_block - start);
            return 0;
        }

        *out_block = 0;
        return 0;
    }

    chosen = 0;
    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t* idx = node + 12u + i * 12u;
        uint32_t block = rd32(idx + 0u);
        if (block > logical_block) {
            break;
        }
        chosen = i;
        found = 1;
    }
    if (!found && entries > 0) {
        chosen = 0;
    }

    if (entries == 0) {
        *out_block = 0;
        return 0;
    }

    {
        const uint8_t* idx = node + 12u + chosen * 12u;
        uint64_t leaf = ((uint64_t)rd16(idx + 8u) << 32) | rd32(idx + 4u);
        const uint8_t* child = block_ptr_const(fs, leaf);
        if (!child) {
            return -1;
        }
        return extent_lookup_node(fs, child, fs->block_size, logical_block, guard + 1u, out_block);
    }
}

static int inode_get_data_block(const extfs_t* fs, const uint8_t* inode, uint32_t logical_block, uint64_t* out_block) {
    if ((inode_flags(inode) & EXT_I_FLAG_EXTENTS) != 0) {
        return extent_lookup_node(fs, inode + 40u, 60u, logical_block, 0u, out_block);
    }
    return inode_get_data_block_legacy(fs, inode, logical_block, out_block);
}

static int inode_set_data_block(extfs_t* fs, uint8_t* inode, uint32_t logical_block, uint64_t phys_block, uint8_t allow_indirect);
static int alloc_block(extfs_t* fs, uint64_t* out_block);
static void free_block(extfs_t* fs, uint64_t block);

static int free_indirect_level(extfs_t* fs, uint64_t block, uint32_t depth) {
    const uint8_t* ptr;
    uint32_t per_block = fs->block_size / 4u;

    if (block == 0) {
        return 0;
    }

    ptr = block_ptr_const(fs, block);
    if (!ptr) {
        return -1;
    }

    for (uint32_t i = 0; i < per_block; i++) {
        uint32_t value = rd32(ptr + i * 4u);
        if (value == 0) {
            continue;
        }

        if (depth <= 1u) {
            free_block(fs, value);
        } else {
            if (free_indirect_level(fs, value, depth - 1u) != 0) {
                return -1;
            }
        }
    }

    free_block(fs, block);
    return 0;
}

static int inode_free_data_legacy(extfs_t* fs, uint8_t* inode) {
    for (uint32_t i = 0; i < 12u; i++) {
        uint32_t block = inode_ptr_entry(inode, i);
        if (block != 0) {
            free_block(fs, block);
            inode_set_ptr_entry(inode, i, 0);
        }
    }

    if (inode_ptr_entry(inode, 12u) != 0) {
        if (free_indirect_level(fs, inode_ptr_entry(inode, 12u), 1u) != 0) {
            return -1;
        }
        inode_set_ptr_entry(inode, 12u, 0);
    }
    if (inode_ptr_entry(inode, 13u) != 0) {
        if (free_indirect_level(fs, inode_ptr_entry(inode, 13u), 2u) != 0) {
            return -1;
        }
        inode_set_ptr_entry(inode, 13u, 0);
    }
    if (inode_ptr_entry(inode, 14u) != 0) {
        if (free_indirect_level(fs, inode_ptr_entry(inode, 14u), 3u) != 0) {
            return -1;
        }
        inode_set_ptr_entry(inode, 14u, 0);
    }

    inode_set_size_bytes(fs, inode, 0);
    inode_update_block_sectors(inode, 0);
    return 0;
}

static int dir_walk(
    const extfs_t* fs,
    uint32_t dir_inode,
    int (*cb)(const uint8_t*, uint8_t, uint32_t, uint8_t, uint64_t, uint16_t, uint16_t, void*),
    void* ctx
) {
    const uint8_t* inode = NULL;
    uint64_t size;
    uint64_t block_count;
    uint64_t remaining;

    if (!fs || !cb || inode_ptr_const(fs, dir_inode, &inode, NULL, NULL) != 0) {
        return -1;
    }
    if (!inode_is_dir_mode(inode_mode(inode))) {
        return -1;
    }

    size = inode_size_bytes(fs, inode);
    block_count = (size + fs->block_size - 1u) / fs->block_size;
    remaining = size;

    for (uint64_t lbn = 0; lbn < block_count; lbn++) {
        uint64_t pblock = 0;
        const uint8_t* block;
        uint32_t limit;
        uint32_t off = 0;
        int rc;

        if (inode_get_data_block(fs, inode, (uint32_t)lbn, &pblock) != 0) {
            return -1;
        }
        if (pblock == 0) {
            if (remaining > fs->block_size) {
                remaining -= fs->block_size;
            } else {
                remaining = 0;
            }
            continue;
        }

        block = block_ptr_const(fs, pblock);
        if (!block) {
            return -1;
        }

        limit = (remaining > fs->block_size) ? fs->block_size : (uint32_t)remaining;
        while (off + 8u <= limit) {
            uint32_t inode_no = rd32(block + off + 0u);
            uint16_t rec_len = rd16(block + off + 4u);
            uint8_t name_len;
            uint8_t file_type;

            if (rec_len < 8u || off + rec_len > limit) {
                return -1;
            }

            name_len = block[off + 6u];
            file_type = block[off + 7u];
            if (name_len > rec_len - 8u) {
                return -1;
            }

            if (inode_no != 0 && name_len > 0) {
                rc = cb(
                    block + off + 8u,
                    name_len,
                    inode_no,
                    file_type,
                    pblock,
                    off,
                    rec_len,
                    ctx
                );
                if (rc != 0) {
                    return rc;
                }
            }

            off += rec_len;
        }

        if (remaining > fs->block_size) {
            remaining -= fs->block_size;
        } else {
            remaining = 0;
        }
    }

    return 0;
}

typedef struct {
    const char* name;
    extfs_dirent_t* out;
    extfs_dir_loc_t* out_loc;
} lookup_ctx_t;

static int lookup_cb(
    const uint8_t* name,
    uint8_t name_len,
    uint32_t inode_no,
    uint8_t file_type,
    uint64_t block,
    uint16_t off,
    uint16_t rec_len,
    void* ctx
) {
    lookup_ctx_t* lctx = (lookup_ctx_t*)ctx;

    if (!lctx || !lctx->name) {
        return -1;
    }
    if (!str_eq_n(lctx->name, name, name_len)) {
        return 0;
    }

    if (lctx->out) {
        size_t copy_len = (name_len < EXTFS_NAME_MAX - 1u) ? name_len : (EXTFS_NAME_MAX - 1u);
        mem_copy(lctx->out->name, name, copy_len);
        lctx->out->name[copy_len] = '\0';
        lctx->out->inode = inode_no;
        lctx->out->size = 0;
        lctx->out->is_dir = (file_type == EXT_FILETYPE_DIR) ? 1u : 0u;
    }

    if (lctx->out_loc) {
        lctx->out_loc->block = block;
        lctx->out_loc->off = off;
        lctx->out_loc->rec_len = rec_len;
        lctx->out_loc->inode = inode_no;
        lctx->out_loc->file_type = file_type;
        lctx->out_loc->found = 1u;
    }

    return 1;
}

typedef struct {
    extfs_dirent_t* out;
    size_t max_entries;
    size_t count;
    const extfs_t* fs;
} list_ctx_t;

static int list_cb(
    const uint8_t* name,
    uint8_t name_len,
    uint32_t inode_no,
    uint8_t file_type,
    uint64_t block,
    uint16_t off,
    uint16_t rec_len,
    void* ctx
) {
    list_ctx_t* lctx = (list_ctx_t*)ctx;
    extfs_dirent_t* out_entry;
    size_t copy_len;
    int is_dir;
    uint32_t size = 0;
    const uint8_t* inode = NULL;
    (void)block;
    (void)off;
    (void)rec_len;

    if (!lctx) {
        return -1;
    }

    if ((name_len == 1u && name[0] == '.') ||
        (name_len == 2u && name[0] == '.' && name[1] == '.')) {
        return 0;
    }

    if (lctx->count >= lctx->max_entries) {
        return 0;
    }

    is_dir = (file_type == EXT_FILETYPE_DIR);
    if (file_type == EXT_FILETYPE_UNKNOWN) {
        if (inode_ptr_const(lctx->fs, inode_no, &inode, NULL, NULL) != 0) {
            return -1;
        }
        is_dir = inode_is_dir_mode(inode_mode(inode));
    } else if (file_type != EXT_FILETYPE_DIR && file_type != EXT_FILETYPE_REG) {
        if (inode_ptr_const(lctx->fs, inode_no, &inode, NULL, NULL) != 0) {
            return -1;
        }
        is_dir = inode_is_dir_mode(inode_mode(inode));
    }

    if (!is_dir) {
        if (!inode) {
            if (inode_ptr_const(lctx->fs, inode_no, &inode, NULL, NULL) != 0) {
                return -1;
            }
        }
        size = (uint32_t)inode_size_bytes(lctx->fs, inode);
    }

    out_entry = &lctx->out[lctx->count++];
    copy_len = (name_len < EXTFS_NAME_MAX - 1u) ? name_len : (EXTFS_NAME_MAX - 1u);
    mem_copy(out_entry->name, name, copy_len);
    out_entry->name[copy_len] = '\0';
    out_entry->inode = inode_no;
    out_entry->size = size;
    out_entry->is_dir = is_dir ? 1u : 0u;
    return 0;
}

static int dir_find_entry(const extfs_t* fs, uint32_t dir_inode, const char* name, extfs_dirent_t* out_entry, extfs_dir_loc_t* out_loc) {
    lookup_ctx_t ctx;
    int rc;

    ctx.name = name;
    ctx.out = out_entry;
    ctx.out_loc = out_loc;
    if (out_loc) {
        out_loc->found = 0;
    }

    rc = dir_walk(fs, dir_inode, lookup_cb, &ctx);
    if (rc == 1) {
        if (out_entry) {
            const uint8_t* inode = NULL;
            if (inode_ptr_const(fs, out_entry->inode, &inode, NULL, NULL) != 0) {
                return -1;
            }
            out_entry->is_dir = inode_is_dir_mode(inode_mode(inode)) ? 1u : 0u;
            out_entry->size = (uint32_t)inode_size_bytes(fs, inode);
        }
        return 0;
    }
    if (rc == 0) {
        return -2;
    }
    return -1;
}

static int alloc_block(extfs_t* fs, uint64_t* out_block) {
    for (uint32_t g = 0; g < fs->group_count; g++) {
        const uint8_t* gd = group_desc_const(fs, g);
        uint64_t bitmap_block;
        uint8_t* bitmap;
        uint32_t bits;
        uint64_t first;

        if (!gd) {
            continue;
        }

        bitmap_block = desc_block_bitmap(fs, gd);
        bitmap = block_ptr_mut(fs, bitmap_block);
        if (!bitmap) {
            continue;
        }

        bits = group_blocks_count(fs, g);
        first = group_first_block(fs, g);
        for (uint32_t i = 0; i < bits; i++) {
            uint64_t blk = first + i;
            uint8_t* data;

            if (blk < fs->first_data_block) {
                continue;
            }
            if (bitmap_get(bitmap, i)) {
                continue;
            }

            bitmap_set(bitmap, i, 1);
            data = block_ptr_mut(fs, blk);
            if (!data) {
                bitmap_set(bitmap, i, 0);
                return -1;
            }
            mem_zero(data, fs->block_size);
            fs->dirty = 1;
            *out_block = blk;
            return 0;
        }
    }

    return -1;
}

static void free_block(extfs_t* fs, uint64_t block) {
    uint32_t group;
    uint32_t idx;
    const uint8_t* gd;
    uint8_t* bitmap;
    uint64_t first;

    if (!fs || block < fs->first_data_block || block >= fs->blocks_count) {
        return;
    }

    group = (uint32_t)((block - fs->first_data_block) / fs->blocks_per_group);
    if (group >= fs->group_count) {
        return;
    }

    gd = group_desc_const(fs, group);
    if (!gd) {
        return;
    }

    first = group_first_block(fs, group);
    idx = (uint32_t)(block - first);
    if (idx >= group_blocks_count(fs, group)) {
        return;
    }

    bitmap = block_ptr_mut(fs, desc_block_bitmap(fs, gd));
    if (!bitmap) {
        return;
    }

    bitmap_set(bitmap, idx, 0);
    fs->dirty = 1;
}

static int alloc_inode(extfs_t* fs, uint16_t mode, uint32_t* out_inode, uint8_t** out_inode_ptr) {
    for (uint32_t g = 0; g < fs->group_count; g++) {
        const uint8_t* gd = group_desc_const(fs, g);
        uint64_t bitmap_block;
        uint8_t* bitmap;
        uint32_t bits;

        if (!gd) {
            continue;
        }

        bitmap_block = desc_inode_bitmap(fs, gd);
        bitmap = block_ptr_mut(fs, bitmap_block);
        if (!bitmap) {
            continue;
        }

        bits = group_inodes_count(fs, g);
        for (uint32_t i = 0; i < bits; i++) {
            uint32_t ino = g * fs->inodes_per_group + i + 1u;
            uint8_t* inode;

            if (ino < fs->first_inode) {
                continue;
            }
            if (bitmap_get(bitmap, i)) {
                continue;
            }

            bitmap_set(bitmap, i, 1);
            if (inode_ptr_mut(fs, ino, &inode, NULL, NULL) != 0) {
                bitmap_set(bitmap, i, 0);
                return -1;
            }

            mem_zero(inode, fs->inode_size);
            wr16(inode + 0u, mode);
            inode_set_links(inode, inode_is_dir_mode(mode) ? 2u : 1u);
            inode_set_size_bytes(fs, inode, 0);
            inode_update_block_sectors(inode, 0);
            fs->dirty = 1;

            if (out_inode) {
                *out_inode = ino;
            }
            if (out_inode_ptr) {
                *out_inode_ptr = inode;
            }
            return 0;
        }
    }

    return -1;
}

static void free_inode(extfs_t* fs, uint32_t inode) {
    uint8_t* inode_ptr;
    uint8_t* bitmap;
    const uint8_t* gd;
    uint32_t group;
    uint32_t index;
    uint32_t idx;

    if (!fs || inode == 0 || inode > fs->inodes_count || inode < fs->first_inode) {
        return;
    }

    idx = inode - 1u;
    group = idx / fs->inodes_per_group;
    index = idx % fs->inodes_per_group;
    if (group >= fs->group_count) {
        return;
    }

    gd = group_desc_const(fs, group);
    if (!gd) {
        return;
    }

    bitmap = block_ptr_mut(fs, desc_inode_bitmap(fs, gd));
    if (!bitmap) {
        return;
    }

    bitmap_set(bitmap, index, 0);
    if (inode_ptr_mut(fs, inode, &inode_ptr, NULL, NULL) == 0) {
        mem_zero(inode_ptr, fs->inode_size);
    }
    fs->dirty = 1;
}

static int inode_set_data_block(extfs_t* fs, uint8_t* inode, uint32_t logical_block, uint64_t phys_block, uint8_t allow_indirect) {
    uint32_t per_block = fs->block_size / 4u;
    uint64_t lbn = logical_block;
    uint64_t meta_block;
    uint32_t ptr;
    uint32_t idx1;
    uint32_t idx2;
    uint32_t idx3;
    uint32_t ptr2;

    if (!inode || phys_block > 0xFFFFFFFFu || per_block == 0) {
        return -1;
    }
    if ((inode_flags(inode) & EXT_I_FLAG_EXTENTS) != 0) {
        return -1;
    }

    if (lbn < 12u) {
        inode_set_ptr_entry(inode, (uint32_t)lbn, (uint32_t)phys_block);
        return 0;
    }
    if (!allow_indirect) {
        return -1;
    }

    lbn -= 12u;
    if (lbn < per_block) {
        ptr = inode_ptr_entry(inode, 12u);
        if (ptr == 0) {
            if (alloc_block(fs, &meta_block) != 0) {
                return -1;
            }
            ptr = (uint32_t)meta_block;
            inode_set_ptr_entry(inode, 12u, ptr);
        }
        return write_u32_to_block(fs, ptr, (uint32_t)lbn, (uint32_t)phys_block);
    }

    lbn -= per_block;
    if (lbn < (uint64_t)per_block * per_block) {
        ptr = inode_ptr_entry(inode, 13u);
        if (ptr == 0) {
            if (alloc_block(fs, &meta_block) != 0) {
                return -1;
            }
            ptr = (uint32_t)meta_block;
            inode_set_ptr_entry(inode, 13u, ptr);
        }

        idx1 = (uint32_t)(lbn / per_block);
        idx2 = (uint32_t)(lbn % per_block);

        if (read_u32_from_block(fs, ptr, idx1, &ptr2) != 0) {
            return -1;
        }
        if (ptr2 == 0) {
            if (alloc_block(fs, &meta_block) != 0) {
                return -1;
            }
            ptr2 = (uint32_t)meta_block;
            if (write_u32_to_block(fs, ptr, idx1, ptr2) != 0) {
                return -1;
            }
        }
        return write_u32_to_block(fs, ptr2, idx2, (uint32_t)phys_block);
    }

    lbn -= (uint64_t)per_block * per_block;
    if (lbn < (uint64_t)per_block * per_block * per_block) {
        ptr = inode_ptr_entry(inode, 14u);
        if (ptr == 0) {
            if (alloc_block(fs, &meta_block) != 0) {
                return -1;
            }
            ptr = (uint32_t)meta_block;
            inode_set_ptr_entry(inode, 14u, ptr);
        }

        idx1 = (uint32_t)(lbn / ((uint64_t)per_block * per_block));
        idx2 = (uint32_t)((lbn / per_block) % per_block);
        idx3 = (uint32_t)(lbn % per_block);

        if (read_u32_from_block(fs, ptr, idx1, &ptr2) != 0) {
            return -1;
        }
        if (ptr2 == 0) {
            if (alloc_block(fs, &meta_block) != 0) {
                return -1;
            }
            ptr2 = (uint32_t)meta_block;
            if (write_u32_to_block(fs, ptr, idx1, ptr2) != 0) {
                return -1;
            }
        }

        if (read_u32_from_block(fs, ptr2, idx2, &ptr) != 0) {
            return -1;
        }
        if (ptr == 0) {
            if (alloc_block(fs, &meta_block) != 0) {
                return -1;
            }
            ptr = (uint32_t)meta_block;
            if (write_u32_to_block(fs, ptr2, idx2, ptr) != 0) {
                return -1;
            }
        }

        return write_u32_to_block(fs, ptr, idx3, (uint32_t)phys_block);
    }

    return -1;
}

static void write_dir_entry(
    uint8_t* entry,
    uint32_t inode_no,
    uint16_t rec_len,
    uint8_t file_type,
    const char* name,
    uint8_t name_len
) {
    mem_zero(entry, rec_len);
    wr32(entry + 0u, inode_no);
    wr16(entry + 4u, rec_len);
    entry[6u] = name_len;
    entry[7u] = file_type;
    mem_copy(entry + 8u, name, name_len);
}

static int dir_add_entry(extfs_t* fs, uint32_t dir_inode, uint32_t child_inode, const char* name, uint8_t file_type) {
    uint8_t* dir_node = NULL;
    uint64_t size;
    uint32_t needed;
    uint64_t block_count;
    uint64_t new_block;
    uint8_t dir_extents;

    if (!name_valid_for_create(name)) {
        return -1;
    }
    if (inode_ptr_mut(fs, dir_inode, &dir_node, NULL, NULL) != 0) {
        return -1;
    }
    if (!inode_is_dir_mode(inode_mode(dir_node))) {
        return -1;
    }
    dir_extents = (inode_flags(dir_node) & EXT_I_FLAG_EXTENTS) != 0;

    needed = align4((uint16_t)(8u + str_len(name)));
    size = inode_size_bytes(fs, dir_node);
    block_count = (size + fs->block_size - 1u) / fs->block_size;

    for (uint64_t lbn = 0; lbn < block_count; lbn++) {
        uint64_t pblock = 0;
        uint8_t* block;
        uint32_t off = 0;

        if (inode_get_data_block(fs, dir_node, (uint32_t)lbn, &pblock) != 0 || pblock == 0) {
            continue;
        }

        block = block_ptr_mut(fs, pblock);
        if (!block) {
            continue;
        }

        while (off + 8u <= fs->block_size) {
            uint32_t inode_no = rd32(block + off + 0u);
            uint16_t rec_len = rd16(block + off + 4u);
            uint8_t name_len = block[off + 6u];
            uint16_t used_len = align4((uint16_t)(8u + name_len));

            if (rec_len < 8u || off + rec_len > fs->block_size) {
                break;
            }

            if (inode_no == 0 && rec_len >= needed) {
                write_dir_entry(block + off, child_inode, rec_len, file_type, name, (uint8_t)str_len(name));
                fs->dirty = 1;
                return 0;
            }

            if (inode_no != 0 && rec_len >= used_len + needed) {
                wr16(block + off + 4u, used_len);
                write_dir_entry(
                    block + off + used_len,
                    child_inode,
                    (uint16_t)(rec_len - used_len),
                    file_type,
                    name,
                    (uint8_t)str_len(name)
                );
                fs->dirty = 1;
                return 0;
            }

            off += rec_len;
        }
    }

    if (dir_extents) {
        return -1;
    }

    if (alloc_block(fs, &new_block) != 0) {
        return -1;
    }
    if (inode_set_data_block(fs, dir_node, (uint32_t)block_count, new_block, 1u) != 0) {
        free_block(fs, new_block);
        return -1;
    }

    {
        uint8_t* block = block_ptr_mut(fs, new_block);
        if (!block) {
            free_block(fs, new_block);
            return -1;
        }
        write_dir_entry(block, child_inode, (uint16_t)fs->block_size, file_type, name, (uint8_t)str_len(name));
    }

    inode_set_size_bytes(fs, dir_node, size + fs->block_size);
    inode_update_block_sectors(dir_node, size + fs->block_size);
    fs->dirty = 1;
    return 0;
}

static int dir_mark_unused(extfs_t* fs, const extfs_dir_loc_t* loc) {
    uint8_t* block;
    if (!loc || !loc->found) {
        return -1;
    }

    block = block_ptr_mut(fs, loc->block);
    if (!block || loc->off + 4u > fs->block_size) {
        return -1;
    }

    wr32(block + loc->off, 0u);
    fs->dirty = 1;
    return 0;
}

static int inode_write_payload(extfs_t* fs, uint8_t* inode, const uint8_t* data, uint32_t size) {
    uint32_t blocks_needed;
    uint64_t written = 0;

    if ((inode_flags(inode) & EXT_I_FLAG_EXTENTS) != 0) {
        return -1;
    }

    if (inode_free_data_legacy(fs, inode) != 0) {
        return -1;
    }
    if (size == 0) {
        fs->dirty = 1;
        return 0;
    }

    blocks_needed = (size + fs->block_size - 1u) / fs->block_size;
    if (fs->write_level < 2u && blocks_needed > 12u) {
        return -1;
    }

    for (uint32_t lbn = 0; lbn < blocks_needed; lbn++) {
        uint64_t data_block = 0;
        uint8_t* block_ptr;
        uint32_t chunk = fs->block_size;

        if (alloc_block(fs, &data_block) != 0) {
            return -1;
        }
        if (inode_set_data_block(fs, inode, lbn, data_block, (fs->write_level >= 2u) ? 1u : 0u) != 0) {
            free_block(fs, data_block);
            return -1;
        }

        block_ptr = block_ptr_mut(fs, data_block);
        if (!block_ptr) {
            return -1;
        }

        if (written + chunk > size) {
            chunk = size - (uint32_t)written;
        }
        mem_zero(block_ptr, fs->block_size);
        mem_copy(block_ptr, data + written, chunk);
        written += chunk;
    }

    inode_set_size_bytes(fs, inode, size);
    inode_update_block_sectors(inode, size);
    fs->dirty = 1;
    return 0;
}

int extfs_probe_image(const uint8_t* image, uint64_t image_size, char* out_name, size_t out_size) {
    extfs_t fs;

    if (read_super(&fs, (uint8_t*)image, image_size) != 0) {
        return 0;
    }

    if (out_name && out_size > 0) {
        (void)extfs_kind_name(&fs, out_name, out_size);
    }
    return 1;
}

int extfs_mount(extfs_t* fs, uint8_t* image, uint64_t image_size) {
    return read_super(fs, image, image_size);
}

int extfs_kind_name(const extfs_t* fs, char* out_name, size_t out_size) {
    if (!fs || !out_name || out_size == 0) {
        return -1;
    }

    if (fs->fs_kind == 4u) {
        str_copy(out_name, "ext4", out_size);
    } else if (fs->fs_kind == 3u) {
        str_copy(out_name, "ext3", out_size);
    } else {
        str_copy(out_name, "ext2", out_size);
    }
    return 0;
}

uint8_t extfs_write_level(const extfs_t* fs) {
    return fs ? fs->write_level : 0u;
}

int extfs_list_dir(
    const extfs_t* fs,
    uint32_t dir_inode,
    extfs_dirent_t* entries,
    size_t max_entries,
    size_t* out_count
) {
    list_ctx_t ctx;
    int rc;

    if (!fs || !entries || !out_count) {
        return -1;
    }

    ctx.out = entries;
    ctx.max_entries = max_entries;
    ctx.count = 0;
    ctx.fs = fs;

    rc = dir_walk(fs, dir_inode, list_cb, &ctx);
    if (rc != 0 && rc != 1) {
        return -1;
    }

    *out_count = ctx.count;
    return 0;
}

int extfs_lookup(const extfs_t* fs, uint32_t dir_inode, const char* name, extfs_dirent_t* out_entry) {
    if (!fs || !name || !out_entry) {
        return -1;
    }
    return dir_find_entry(fs, dir_inode, name, out_entry, NULL);
}

int extfs_read_file(
    const extfs_t* fs,
    uint32_t inode_no,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
) {
    const uint8_t* inode = NULL;
    uint64_t size;
    uint64_t block_count;
    uint64_t copied = 0;

    if (!fs || !out_buf || !out_size) {
        return -1;
    }
    if (inode_ptr_const(fs, inode_no, &inode, NULL, NULL) != 0) {
        return -1;
    }
    if (!inode_is_file_mode(inode_mode(inode))) {
        return -1;
    }

    size = inode_size_bytes(fs, inode);
    if (size > out_buf_size) {
        return -1;
    }

    block_count = (size + fs->block_size - 1u) / fs->block_size;
    for (uint64_t lbn = 0; lbn < block_count; lbn++) {
        uint64_t pblock = 0;
        const uint8_t* block;
        uint32_t chunk = fs->block_size;

        if (inode_get_data_block(fs, inode, (uint32_t)lbn, &pblock) != 0) {
            return -1;
        }

        if (copied + chunk > size) {
            chunk = (uint32_t)(size - copied);
        }

        if (pblock == 0) {
            mem_zero(out_buf + copied, chunk);
        } else {
            block = block_ptr_const(fs, pblock);
            if (!block) {
                return -1;
            }
            mem_copy(out_buf + copied, block, chunk);
        }
        copied += chunk;
    }

    *out_size = (uint32_t)size;
    return 0;
}

int extfs_is_dir(const extfs_t* fs, uint32_t inode_no, int* out_is_dir) {
    const uint8_t* inode = NULL;
    if (!fs || !out_is_dir) {
        return -1;
    }
    if (inode_ptr_const(fs, inode_no, &inode, NULL, NULL) != 0) {
        return -1;
    }

    *out_is_dir = inode_is_dir_mode(inode_mode(inode));
    return 0;
}

int extfs_create_file(extfs_t* fs, uint32_t dir_inode, const char* name) {
    uint32_t inode_no;
    uint8_t* inode;

    if (!fs || fs->write_level == 0u || !name_valid_for_create(name)) {
        return -1;
    }

    if (dir_find_entry(fs, dir_inode, name, NULL, NULL) == 0) {
        return -2;
    }

    if (alloc_inode(fs, EXT_S_IFREG | 0644u, &inode_no, &inode) != 0) {
        return -1;
    }

    if (dir_add_entry(fs, dir_inode, inode_no, name, EXT_FILETYPE_REG) != 0) {
        free_inode(fs, inode_no);
        return -1;
    }

    inode_set_size_bytes(fs, inode, 0);
    inode_update_block_sectors(inode, 0);
    fs->dirty = 1;
    return 0;
}

int extfs_mkdir(extfs_t* fs, uint32_t dir_inode, const char* name) {
    uint32_t inode_no;
    uint8_t* inode;
    uint64_t block;
    uint8_t* data_block;
    uint8_t* parent_inode;

    if (!fs || fs->write_level == 0u || !name_valid_for_create(name)) {
        return -1;
    }

    if (dir_find_entry(fs, dir_inode, name, NULL, NULL) == 0) {
        return -2;
    }

    if (inode_ptr_mut(fs, dir_inode, &parent_inode, NULL, NULL) != 0) {
        return -1;
    }

    if (alloc_inode(fs, EXT_S_IFDIR | 0755u, &inode_no, &inode) != 0) {
        return -1;
    }

    if (alloc_block(fs, &block) != 0) {
        free_inode(fs, inode_no);
        return -1;
    }

    inode_set_ptr_entry(inode, 0u, (uint32_t)block);
    inode_set_size_bytes(fs, inode, fs->block_size);
    inode_update_block_sectors(inode, fs->block_size);
    inode_set_links(inode, 2u);

    data_block = block_ptr_mut(fs, block);
    if (!data_block) {
        free_block(fs, block);
        free_inode(fs, inode_no);
        return -1;
    }
    mem_zero(data_block, fs->block_size);
    write_dir_entry(data_block, inode_no, 12u, EXT_FILETYPE_DIR, ".", 1u);
    write_dir_entry(data_block + 12u, dir_inode, (uint16_t)(fs->block_size - 12u), EXT_FILETYPE_DIR, "..", 2u);

    if (dir_add_entry(fs, dir_inode, inode_no, name, EXT_FILETYPE_DIR) != 0) {
        free_block(fs, block);
        free_inode(fs, inode_no);
        return -1;
    }

    inode_set_links(parent_inode, (uint16_t)(inode_links(parent_inode) + 1u));
    fs->dirty = 1;
    return 0;
}

int extfs_write_file(extfs_t* fs, uint32_t dir_inode, const char* name, const uint8_t* data, uint32_t size) {
    extfs_dirent_t entry;
    uint8_t* inode = NULL;
    int rc;

    if (!fs || fs->write_level == 0u || !name_valid_for_create(name) || (!data && size > 0u)) {
        return -1;
    }

    rc = dir_find_entry(fs, dir_inode, name, &entry, NULL);
    if (rc == -2) {
        if (extfs_create_file(fs, dir_inode, name) != 0) {
            return -1;
        }
        rc = dir_find_entry(fs, dir_inode, name, &entry, NULL);
    }
    if (rc != 0) {
        return -1;
    }
    if (entry.is_dir) {
        return -1;
    }

    if (inode_ptr_mut(fs, entry.inode, &inode, NULL, NULL) != 0) {
        return -1;
    }
    if (!inode_is_file_mode(inode_mode(inode))) {
        return -1;
    }

    if (inode_write_payload(fs, inode, data ? data : (const uint8_t*)"", size) != 0) {
        return -1;
    }

    fs->dirty = 1;
    return 0;
}

int extfs_delete_file(extfs_t* fs, uint32_t dir_inode, const char* name) {
    extfs_dirent_t entry;
    extfs_dir_loc_t loc;
    uint8_t* inode = NULL;
    uint16_t links;

    if (!fs || fs->write_level == 0u || !name_valid_for_create(name)) {
        return -1;
    }

    if (dir_find_entry(fs, dir_inode, name, &entry, &loc) != 0 || !loc.found) {
        return -1;
    }
    if (entry.is_dir) {
        return -1;
    }
    if (inode_ptr_mut(fs, entry.inode, &inode, NULL, NULL) != 0) {
        return -1;
    }
    if (!inode_is_file_mode(inode_mode(inode))) {
        return -1;
    }

    links = inode_links(inode);
    if (links > 1u) {
        inode_set_links(inode, (uint16_t)(links - 1u));
    } else {
        if ((inode_flags(inode) & EXT_I_FLAG_EXTENTS) != 0) {
            return -1;
        }
        if (inode_free_data_legacy(fs, inode) != 0) {
            return -1;
        }
        free_inode(fs, entry.inode);
    }

    if (dir_mark_unused(fs, &loc) != 0) {
        return -1;
    }

    fs->dirty = 1;
    return 0;
}
