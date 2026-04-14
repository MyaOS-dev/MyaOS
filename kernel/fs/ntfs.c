#include "ntfs.h"
#include <stddef.h>

#define NTFS_ATTR_END 0xFFFFFFFFu
#define NTFS_ATTR_ATTRIBUTE_LIST 0x20u
#define NTFS_ATTR_FILE_NAME 0x30u
#define NTFS_ATTR_DATA 0x80u
#define NTFS_ATTR_INDEX_ROOT 0x90u
#define NTFS_ATTR_INDEX_ALLOCATION 0xA0u

#define NTFS_RECORD_FLAG_IN_USE 0x0001u
#define NTFS_RECORD_FLAG_DIRECTORY 0x0002u

#define NTFS_ATTR_FLAG_COMPRESSED 0x0001u
#define NTFS_ATTR_FLAG_ENCRYPTED 0x4000u

#define NTFS_FILE_ATTR_DIRECTORY 0x10000000u

#define NTFS_INDEX_ENTRY_NODE 0x0001u
#define NTFS_INDEX_ENTRY_END 0x0002u

typedef int (*ntfs_entry_cb_t)(const ntfs_dirent_t* entry, const uint8_t* key, uint32_t key_len, void* ctx);

typedef struct {
    ntfs_dirent_t* out;
    uint32_t max_entries;
    uint32_t count;
} ntfs_list_ctx_t;

typedef struct {
    const char* target;
    ntfs_dirent_t best;
    uint8_t found;
    uint8_t found_preferred;
} ntfs_lookup_ctx_t;

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t* p) {
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static void mem_copy(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;

    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static void mem_zero(void* dst, size_t size) {
    uint8_t* out = (uint8_t*)dst;

    for (size_t i = 0; i < size; i++) {
        out[i] = 0u;
    }
}

static int mem_is_zero(const void* ptr, size_t size) {
    const uint8_t* data = (const uint8_t*)ptr;

    for (size_t i = 0; i < size; i++) {
        if (data[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int is_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static char ascii_tolower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static size_t str_len(const char* text) {
    size_t len = 0u;

    while (text && text[len]) {
        len++;
    }
    return len;
}

static int str_eq_ci(const char* a, const char* b) {
    size_t i = 0u;

    if (!a || !b) {
        return 0;
    }

    while (a[i] && b[i]) {
        if (ascii_tolower(a[i]) != ascii_tolower(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int signature_eq(const uint8_t* data, char a, char b, char c, char d) {
    return data && data[0] == (uint8_t)a && data[1] == (uint8_t)b && data[2] == (uint8_t)c && data[3] == (uint8_t)d;
}

static uint32_t ntfs_calc_record_size(int8_t encoded, uint32_t cluster_size) {
    if (encoded > 0) {
        return (uint32_t)encoded * cluster_size;
    }
    if (encoded < 0) {
        return 1u << (uint32_t)(-(int32_t)encoded);
    }
    return 0u;
}

static int ntfs_copy_image(const ntfs_fs_t* fs, uint64_t offset, void* out_buf, uint32_t size) {
    if (!fs || !fs->image || !out_buf) {
        return -1;
    }
    if ((uint64_t)size > fs->image_size || offset > fs->image_size - (uint64_t)size) {
        return -1;
    }

    mem_copy(out_buf, fs->image + offset, size);
    return 0;
}

static int ntfs_store_image(ntfs_fs_t* fs, uint64_t offset, const void* src_buf, uint32_t size) {
    if (!fs || !fs->image || !src_buf) {
        return -1;
    }
    if ((uint64_t)size > fs->image_size || offset > fs->image_size - (uint64_t)size) {
        return -1;
    }

    mem_copy(fs->image + offset, src_buf, size);
    return 0;
}

static int64_t ntfs_read_signed_le(const uint8_t* data, uint32_t size) {
    int64_t value = 0;

    if (!data || size == 0u || size > 8u) {
        return 0;
    }
    for (uint32_t i = 0u; i < size; i++) {
        value |= ((int64_t)data[i]) << (i * 8u);
    }
    if ((data[size - 1u] & 0x80u) != 0u && size < 8u) {
        value |= -((int64_t)1 << (size * 8u));
    }
    return value;
}

static uint64_t ntfs_read_unsigned_le(const uint8_t* data, uint32_t size) {
    uint64_t value = 0u;

    if (!data || size == 0u || size > 8u) {
        return 0u;
    }
    for (uint32_t i = 0u; i < size; i++) {
        value |= ((uint64_t)data[i]) << (i * 8u);
    }
    return value;
}

static int ntfs_parse_runlist(
    const uint8_t* runlist,
    uint32_t runlist_size,
    ntfs_run_t* out_runs,
    uint32_t max_runs,
    uint32_t* out_count
) {
    uint32_t pos = 0u;
    uint32_t count = 0u;
    uint64_t current_vcn = 0u;
    int64_t current_lcn = 0;

    if (!runlist || !out_runs || max_runs == 0u || !out_count) {
        return -1;
    }

    while (pos < runlist_size) {
        uint8_t header = runlist[pos++];
        uint32_t len_size;
        uint32_t off_size;
        uint64_t run_len;
        int64_t lcn;

        if (header == 0u) {
            *out_count = count;
            return 0;
        }

        len_size = (uint32_t)(header & 0x0Fu);
        off_size = (uint32_t)(header >> 4);
        if (len_size == 0u || pos + len_size + off_size > runlist_size || count >= max_runs) {
            return -1;
        }

        run_len = ntfs_read_unsigned_le(runlist + pos, len_size);
        pos += len_size;
        if (run_len == 0u) {
            return -1;
        }

        if (off_size == 0u) {
            lcn = -1;
        } else {
            int64_t delta = ntfs_read_signed_le(runlist + pos, off_size);
            pos += off_size;
            current_lcn += delta;
            lcn = current_lcn;
        }

        out_runs[count].vcn = current_vcn;
        out_runs[count].lcn = lcn;
        out_runs[count].len_clusters = run_len;
        current_vcn += run_len;
        count++;
    }

    return -1;
}

static int ntfs_apply_fixup(const ntfs_fs_t* fs, uint8_t* record, uint32_t record_size) {
    uint16_t usa_off;
    uint16_t usa_count;
    uint16_t usn;

    if (!fs || !record || fs->bytes_per_sector == 0u || record_size < fs->bytes_per_sector) {
        return -1;
    }

    usa_off = rd16(record + 4u);
    usa_count = rd16(record + 6u);
    if (usa_count == 0u || usa_off + (uint32_t)usa_count * 2u > record_size) {
        return -1;
    }

    usn = rd16(record + usa_off);
    for (uint32_t i = 1u; i < usa_count; i++) {
        uint32_t sector_end = i * (uint32_t)fs->bytes_per_sector;
        uint16_t replacement;

        if (sector_end < 2u || sector_end > record_size) {
            return -1;
        }
        sector_end -= 2u;
        if (rd16(record + sector_end) != usn) {
            return -1;
        }

        replacement = rd16(record + usa_off + i * 2u);
        record[sector_end] = (uint8_t)(replacement & 0xFFu);
        record[sector_end + 1u] = (uint8_t)((replacement >> 8) & 0xFFu);
    }

    return 0;
}

static int ntfs_prepare_fixup_for_write(const ntfs_fs_t* fs, uint8_t* record, uint32_t record_size) {
    uint16_t usa_off;
    uint16_t usa_count;
    uint16_t usn;

    if (!fs || !record || fs->bytes_per_sector == 0u || record_size < fs->bytes_per_sector) {
        return -1;
    }

    usa_off = rd16(record + 4u);
    usa_count = rd16(record + 6u);
    if (usa_count == 0u || usa_off + (uint32_t)usa_count * 2u > record_size) {
        return -1;
    }

    usn = (uint16_t)(rd16(record + usa_off) + 1u);
    if (usn == 0u) {
        usn = 1u;
    }
    record[usa_off] = (uint8_t)(usn & 0xFFu);
    record[usa_off + 1u] = (uint8_t)((usn >> 8) & 0xFFu);

    for (uint32_t i = 1u; i < usa_count; i++) {
        uint32_t sector_end = i * (uint32_t)fs->bytes_per_sector;
        uint16_t replacement;

        if (sector_end < 2u || sector_end > record_size) {
            return -1;
        }
        sector_end -= 2u;
        replacement = rd16(record + sector_end);
        record[usa_off + i * 2u] = (uint8_t)(replacement & 0xFFu);
        record[usa_off + i * 2u + 1u] = (uint8_t)((replacement >> 8) & 0xFFu);
        record[sector_end] = (uint8_t)(usn & 0xFFu);
        record[sector_end + 1u] = (uint8_t)((usn >> 8) & 0xFFu);
    }

    return 0;
}

static int ntfs_resident_value(const uint8_t* attr, uint32_t attr_len, const uint8_t** out_value, uint32_t* out_len) {
    uint32_t value_len;
    uint16_t value_off;

    if (!attr || attr_len < 24u || attr[8] != 0u || !out_value || !out_len) {
        return -1;
    }

    value_len = rd32(attr + 16u);
    value_off = rd16(attr + 20u);
    if (value_off > attr_len || value_len > attr_len - value_off) {
        return -1;
    }

    *out_value = attr + value_off;
    *out_len = value_len;
    return 0;
}

static int ntfs_nonresident_info(
    const uint8_t* attr,
    uint32_t attr_len,
    const uint8_t** out_runlist,
    uint32_t* out_runlist_size,
    uint64_t* out_data_size
) {
    uint16_t run_off;
    uint16_t flags;

    if (!attr || attr_len < 64u || attr[8] == 0u || !out_runlist || !out_runlist_size || !out_data_size) {
        return -1;
    }

    flags = rd16(attr + 12u);
    if ((flags & (NTFS_ATTR_FLAG_COMPRESSED | NTFS_ATTR_FLAG_ENCRYPTED)) != 0u) {
        return -1;
    }

    run_off = rd16(attr + 32u);
    if (run_off >= attr_len) {
        return -1;
    }

    *out_runlist = attr + run_off;
    *out_runlist_size = attr_len - (uint32_t)run_off;
    *out_data_size = rd64(attr + 48u);
    return 0;
}

static int ntfs_find_attr(const uint8_t* record, uint32_t record_size, uint32_t type, uint8_t unnamed_only, const uint8_t** out_attr) {
    uint32_t attr_off;
    uint32_t bytes_in_use;

    if (!record || record_size < 24u || !out_attr) {
        return -1;
    }

    attr_off = rd16(record + 20u);
    bytes_in_use = rd32(record + 24u);
    if (bytes_in_use == 0u || bytes_in_use > record_size) {
        bytes_in_use = record_size;
    }

    while (attr_off + 8u <= bytes_in_use) {
        const uint8_t* attr = record + attr_off;
        uint32_t attr_type = rd32(attr + 0u);
        uint32_t attr_len = rd32(attr + 4u);

        if (attr_type == NTFS_ATTR_END) {
            break;
        }
        if (attr_len < 8u || attr_off + attr_len > bytes_in_use) {
            return -1;
        }
        if (attr_type == type && (!unnamed_only || attr[9] == 0u)) {
            *out_attr = attr;
            return 0;
        }

        attr_off += attr_len;
    }

    return -1;
}

static int ntfs_read_runs(
    const ntfs_fs_t* fs,
    const ntfs_run_t* runs,
    uint32_t run_count,
    uint64_t stream_size,
    uint64_t stream_offset,
    uint8_t* out_buf,
    uint32_t size
) {
    uint64_t end_offset;
    uint32_t written = 0u;

    if (!fs || !runs || (!out_buf && size != 0u)) {
        return -1;
    }
    if (size == 0u) {
        return 0;
    }

    end_offset = stream_offset + (uint64_t)size;
    if (end_offset < stream_offset || end_offset > stream_size) {
        return -1;
    }

    for (uint32_t i = 0u; i < run_count && written < size; i++) {
        uint64_t run_start = runs[i].vcn * (uint64_t)fs->cluster_size;
        uint64_t run_end = run_start + runs[i].len_clusters * (uint64_t)fs->cluster_size;
        uint64_t copy_start;
        uint64_t copy_end;
        uint32_t chunk;

        if (run_end <= stream_offset || run_start >= end_offset) {
            continue;
        }

        copy_start = (stream_offset > run_start) ? stream_offset : run_start;
        copy_end = (end_offset < run_end) ? end_offset : run_end;
        if (copy_end <= copy_start) {
            continue;
        }

        chunk = (uint32_t)(copy_end - copy_start);
        if (runs[i].lcn < 0) {
            mem_zero(out_buf + written, chunk);
        } else {
            uint64_t image_off = (uint64_t)runs[i].lcn * (uint64_t)fs->cluster_size + (copy_start - run_start);
            if (ntfs_copy_image(fs, image_off, out_buf + written, chunk) != 0) {
                return -1;
            }
        }
        written += chunk;
    }

    return written == size ? 0 : -1;
}

static int ntfs_write_runs(
    ntfs_fs_t* fs,
    const ntfs_run_t* runs,
    uint32_t run_count,
    uint64_t stream_size,
    uint64_t stream_offset,
    const uint8_t* in_buf,
    uint32_t size
) {
    uint64_t end_offset;
    uint32_t written = 0u;

    if (!fs || !runs || (!in_buf && size != 0u)) {
        return -1;
    }
    if (size == 0u) {
        return 0;
    }

    end_offset = stream_offset + (uint64_t)size;
    if (end_offset < stream_offset || end_offset > stream_size) {
        return -1;
    }

    for (uint32_t i = 0u; i < run_count && written < size; i++) {
        uint64_t run_start = runs[i].vcn * (uint64_t)fs->cluster_size;
        uint64_t run_end = run_start + runs[i].len_clusters * (uint64_t)fs->cluster_size;
        uint64_t copy_start;
        uint64_t copy_end;
        uint32_t chunk;

        if (run_end <= stream_offset || run_start >= end_offset) {
            continue;
        }

        copy_start = (stream_offset > run_start) ? stream_offset : run_start;
        copy_end = (end_offset < run_end) ? end_offset : run_end;
        if (copy_end <= copy_start) {
            continue;
        }

        chunk = (uint32_t)(copy_end - copy_start);
        if (runs[i].lcn < 0) {
            if (!mem_is_zero(in_buf + written, chunk)) {
                return -1;
            }
        } else {
            uint64_t image_off = (uint64_t)runs[i].lcn * (uint64_t)fs->cluster_size + (copy_start - run_start);
            if (ntfs_store_image(fs, image_off, in_buf + written, chunk) != 0) {
                return -1;
            }
        }
        written += chunk;
    }

    return written == size ? 0 : -1;
}

static int ntfs_read_nonresident_attr(
    const ntfs_fs_t* fs,
    const uint8_t* attr,
    uint32_t attr_len,
    uint64_t offset,
    uint8_t* out_buf,
    uint32_t size,
    uint64_t* out_data_size
) {
    ntfs_run_t runs[NTFS_MAX_RUNS];
    const uint8_t* runlist;
    uint32_t runlist_size;
    uint32_t run_count = 0u;
    uint64_t data_size;

    if (ntfs_nonresident_info(attr, attr_len, &runlist, &runlist_size, &data_size) != 0) {
        return -1;
    }
    if (ntfs_parse_runlist(runlist, runlist_size, runs, NTFS_MAX_RUNS, &run_count) != 0) {
        return -1;
    }
    if (out_data_size) {
        *out_data_size = data_size;
    }
    return ntfs_read_runs(fs, runs, run_count, data_size, offset, out_buf, size);
}

static int ntfs_write_nonresident_attr(
    ntfs_fs_t* fs,
    const uint8_t* attr,
    uint32_t attr_len,
    uint64_t offset,
    const uint8_t* in_buf,
    uint32_t size,
    uint64_t* out_data_size
) {
    ntfs_run_t runs[NTFS_MAX_RUNS];
    const uint8_t* runlist;
    uint32_t runlist_size;
    uint32_t run_count = 0u;
    uint64_t data_size;

    if (ntfs_nonresident_info(attr, attr_len, &runlist, &runlist_size, &data_size) != 0) {
        return -1;
    }
    if (ntfs_parse_runlist(runlist, runlist_size, runs, NTFS_MAX_RUNS, &run_count) != 0) {
        return -1;
    }
    if (out_data_size) {
        *out_data_size = data_size;
    }
    return ntfs_write_runs(fs, runs, run_count, data_size, offset, in_buf, size);
}

static int ntfs_load_record(const ntfs_fs_t* fs, uint64_t record_no, uint8_t* out_record) {
    uint64_t record_offset;

    if (!fs || !out_record || fs->record_size == 0u || fs->record_size > NTFS_MAX_RECORD_SIZE) {
        return -1;
    }

    if (record_no == 0u && fs->mft_run_count == 0u) {
        uint64_t image_off = fs->mft_lcn * (uint64_t)fs->cluster_size;
        if (ntfs_copy_image(fs, image_off, out_record, fs->record_size) != 0) {
            return -1;
        }
    } else {
        record_offset = record_no * (uint64_t)fs->record_size;
        if (ntfs_read_runs(fs, fs->mft_runs, fs->mft_run_count, fs->mft_data_size, record_offset, out_record, fs->record_size) != 0) {
            return -1;
        }
    }

    if (!signature_eq(out_record, 'F', 'I', 'L', 'E')) {
        return -1;
    }
    if (ntfs_apply_fixup(fs, out_record, fs->record_size) != 0) {
        return -1;
    }
    if ((rd16(out_record + 22u) & NTFS_RECORD_FLAG_IN_USE) == 0u && record_no != 0u) {
        return -1;
    }
    return 0;
}

static int ntfs_store_record(ntfs_fs_t* fs, uint64_t record_no, const uint8_t* record) {
    uint8_t on_disk[NTFS_MAX_RECORD_SIZE];
    uint64_t record_offset;

    if (!fs || !record || fs->record_size == 0u || fs->record_size > sizeof(on_disk)) {
        return -1;
    }

    mem_copy(on_disk, record, fs->record_size);
    if (ntfs_prepare_fixup_for_write(fs, on_disk, fs->record_size) != 0) {
        return -1;
    }

    if (record_no == 0u && fs->mft_run_count == 0u) {
        return ntfs_store_image(fs, fs->mft_lcn * (uint64_t)fs->cluster_size, on_disk, fs->record_size);
    }

    record_offset = record_no * (uint64_t)fs->record_size;
    return ntfs_write_runs(fs, fs->mft_runs, fs->mft_run_count, fs->mft_data_size, record_offset, on_disk, fs->record_size);
}

static uint8_t ntfs_dirent_quality(const ntfs_dirent_t* entry) {
    if (!entry) {
        return 0u;
    }
    switch (entry->name_namespace) {
    case 3u:
        return 3u;
    case 1u:
        return 2u;
    case 0u:
        return 1u;
    default:
        return 0u;
    }
}

static int ntfs_key_to_dirent(uint64_t record_no, const uint8_t* key, uint32_t key_len, ntfs_dirent_t* out_entry) {
    uint8_t name_len;
    uint32_t flags;
    uint64_t size;

    if (!key || key_len < 66u || !out_entry) {
        return -1;
    }

    name_len = key[64u];
    if (66u + (uint32_t)name_len * 2u > key_len) {
        return -1;
    }

    mem_zero(out_entry, sizeof(*out_entry));
    out_entry->record_no = record_no;
    out_entry->name_namespace = key[65u];
    flags = rd32(key + 56u);
    size = rd64(key + 48u);
    out_entry->is_dir = (flags & NTFS_FILE_ATTR_DIRECTORY) != 0u ? 1u : 0u;
    out_entry->size = out_entry->is_dir ? 0u : size;

    for (uint32_t i = 0u; i < (uint32_t)name_len && i + 1u < sizeof(out_entry->name); i++) {
        uint16_t ch = rd16(key + 66u + i * 2u);
        char out_ch = '?';

        if (ch >= 0x20u && ch < 0x80u) {
            out_ch = (char)ch;
            if (out_ch == '/' || out_ch == '\\') {
                out_ch = '_';
            }
        }
        out_entry->name[i] = out_ch;
        out_entry->name[i + 1u] = '\0';
    }

    return out_entry->name[0] ? 0 : -1;
}

static int ntfs_key_name_equals(const uint8_t* key, uint32_t key_len, const char* name) {
    uint8_t name_len;
    size_t query_len;

    if (!key || key_len < 66u || !name) {
        return 0;
    }

    name_len = key[64u];
    query_len = str_len(name);
    if ((uint32_t)query_len != (uint32_t)name_len || 66u + (uint32_t)name_len * 2u > key_len) {
        return 0;
    }

    for (uint32_t i = 0u; i < (uint32_t)name_len; i++) {
        uint16_t ch = rd16(key + 66u + i * 2u);
        if (ch >= 0x80u) {
            return 0;
        }
        if (ascii_tolower((char)ch) != ascii_tolower(name[i])) {
            return 0;
        }
    }

    return 1;
}

static int ntfs_parse_index_entries(
    const uint8_t* base,
    uint32_t total_size,
    uint32_t index_header_off,
    ntfs_entry_cb_t cb,
    void* ctx
) {
    const uint8_t* header;
    const uint8_t* entry;
    const uint8_t* limit;

    if (!base || !cb || index_header_off + 16u > total_size) {
        return -1;
    }

    header = base + index_header_off;
    if (rd32(header + 4u) < rd32(header + 0u) || index_header_off + rd32(header + 4u) > total_size) {
        return -1;
    }

    entry = header + rd32(header + 0u);
    limit = header + rd32(header + 4u);

    while (entry + 16u <= limit) {
        uint64_t file_ref = rd64(entry + 0u) & 0x0000FFFFFFFFFFFFull;
        uint16_t entry_len = rd16(entry + 8u);
        uint16_t key_len = rd16(entry + 10u);
        uint16_t flags = rd16(entry + 12u);

        if (entry_len < 16u || entry + entry_len > limit) {
            return -1;
        }

        if ((flags & NTFS_INDEX_ENTRY_END) == 0u) {
            const uint8_t* key = entry + 16u;
            ntfs_dirent_t parsed;
            int rc;

            if ((uint32_t)key_len > (uint32_t)entry_len - 16u) {
                return -1;
            }
            if (file_ref != 0u && ntfs_key_to_dirent(file_ref, key, key_len, &parsed) == 0) {
                rc = cb(&parsed, key, key_len, ctx);
                if (rc != 0) {
                    return rc;
                }
            }
        }

        entry += entry_len;
        if ((flags & NTFS_INDEX_ENTRY_END) != 0u) {
            break;
        }
    }

    return 0;
}

static int ntfs_dir_list_cb(const ntfs_dirent_t* entry, const uint8_t* key, uint32_t key_len, void* ctx) {
    ntfs_list_ctx_t* list = (ntfs_list_ctx_t*)ctx;
    (void)key;
    (void)key_len;

    if (!list || !entry) {
        return -1;
    }
    if (entry->name[0] == '.' && entry->name[1] == '\0') {
        return 0;
    }
    if (entry->name[0] == '.' && entry->name[1] == '.' && entry->name[2] == '\0') {
        return 0;
    }

    for (uint32_t i = 0u; i < list->count; i++) {
        int same_record;
        int same_name;
        int dos_alias_pair;

        same_record = list->out[i].record_no == entry->record_no;
        same_name = str_eq_ci(list->out[i].name, entry->name);
        dos_alias_pair = (list->out[i].name_namespace == 2u || entry->name_namespace == 2u);
        if (!same_record || (!same_name && !dos_alias_pair)) {
            continue;
        }
        if (ntfs_dirent_quality(entry) > ntfs_dirent_quality(&list->out[i])) {
            list->out[i] = *entry;
        }
        return 0;
    }

    if (list->count >= list->max_entries) {
        return 0;
    }

    list->out[list->count++] = *entry;
    return 0;
}

static int ntfs_dir_lookup_cb(const ntfs_dirent_t* entry, const uint8_t* key, uint32_t key_len, void* ctx) {
    ntfs_lookup_ctx_t* lookup = (ntfs_lookup_ctx_t*)ctx;

    if (!lookup || !entry || !lookup->target) {
        return -1;
    }
    if (!ntfs_key_name_equals(key, key_len, lookup->target)) {
        return 0;
    }

    if (!lookup->found || ntfs_dirent_quality(entry) > ntfs_dirent_quality(&lookup->best)) {
        lookup->best = *entry;
        lookup->found = 1u;
        lookup->found_preferred = entry->name_namespace != 2u ? 1u : 0u;
    }

    return lookup->found_preferred ? 1 : 0;
}

static int ntfs_scan_directory(const ntfs_fs_t* fs, uint64_t dir_record, ntfs_entry_cb_t cb, void* ctx) {
    uint8_t record[NTFS_MAX_RECORD_SIZE];
    uint8_t index_block[NTFS_MAX_INDEX_RECORD_SIZE];
    const uint8_t* index_root_attr;
    const uint8_t* index_alloc_attr;
    const uint8_t* root_value;
    uint32_t root_value_len;
    uint64_t alloc_size = 0u;
    int root_rc;

    if (!fs || !cb) {
        return -1;
    }
    if (ntfs_load_record(fs, dir_record, record) != 0) {
        return -1;
    }
    if ((rd16(record + 22u) & NTFS_RECORD_FLAG_DIRECTORY) == 0u) {
        return -1;
    }

    if (ntfs_find_attr(record, fs->record_size, NTFS_ATTR_INDEX_ROOT, 0u, &index_root_attr) != 0) {
        return -1;
    }
    if (ntfs_resident_value(index_root_attr, rd32(index_root_attr + 4u), &root_value, &root_value_len) != 0 || root_value_len < 32u) {
        return -1;
    }

    root_rc = ntfs_parse_index_entries(root_value, root_value_len, 16u, cb, ctx);
    if (root_rc != 0) {
        return root_rc < 0 ? -1 : root_rc;
    }

    if (ntfs_find_attr(record, fs->record_size, NTFS_ATTR_INDEX_ALLOCATION, 0u, &index_alloc_attr) != 0) {
        return 0;
    }
    if (ntfs_nonresident_info(index_alloc_attr, rd32(index_alloc_attr + 4u), &root_value, &root_value_len, &alloc_size) != 0) {
        return 0;
    }

    for (uint64_t offset = 0u; offset + (uint64_t)fs->index_record_size <= alloc_size; offset += (uint64_t)fs->index_record_size) {
        int rc;

        if (fs->index_record_size > sizeof(index_block)) {
            return -1;
        }
        if (ntfs_read_nonresident_attr(
                fs,
                index_alloc_attr,
                rd32(index_alloc_attr + 4u),
                offset,
                index_block,
                fs->index_record_size,
                NULL
            ) != 0) {
            return -1;
        }
        if (!signature_eq(index_block, 'I', 'N', 'D', 'X')) {
            continue;
        }
        if (ntfs_apply_fixup(fs, index_block, fs->index_record_size) != 0) {
            return -1;
        }

        rc = ntfs_parse_index_entries(index_block, fs->index_record_size, 24u, cb, ctx);
        if (rc != 0) {
            return rc < 0 ? -1 : rc;
        }
    }

    return 0;
}

int ntfs_probe_image(const uint8_t* image, uint64_t image_size) {
    static const char ntfs_oem[8] = {'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;

    if (!image || image_size < 512u) {
        return 0;
    }

    for (uint32_t i = 0u; i < sizeof(ntfs_oem); i++) {
        if (image[3u + i] != (uint8_t)ntfs_oem[i]) {
            return 0;
        }
    }

    bytes_per_sector = rd16(image + 11u);
    sectors_per_cluster = image[13u];
    if (!is_power_of_two(bytes_per_sector) || bytes_per_sector < 256u || sectors_per_cluster == 0u) {
        return 0;
    }
    if (rd64(image + 40u) == 0u) {
        return 0;
    }

    return 1;
}

int ntfs_mount(ntfs_fs_t* fs, const uint8_t* image, uint64_t image_size) {
    uint8_t record0[NTFS_MAX_RECORD_SIZE];
    const uint8_t* mft_data_attr;
    const uint8_t* runlist;
    uint32_t runlist_size;
    uint64_t data_size = 0u;
    int8_t record_size_code;
    int8_t index_size_code;

    if (!fs || !ntfs_probe_image(image, image_size)) {
        return -1;
    }

    mem_zero(fs, sizeof(*fs));
    fs->image = (uint8_t*)image;
    fs->image_size = image_size;
    fs->bytes_per_sector = rd16(image + 11u);
    fs->sectors_per_cluster = image[13u];
    fs->cluster_size = (uint32_t)fs->bytes_per_sector * (uint32_t)fs->sectors_per_cluster;
    fs->total_sectors = rd64(image + 40u);
    fs->mft_lcn = rd64(image + 48u);

    record_size_code = (int8_t)image[64u];
    index_size_code = (int8_t)image[68u];
    fs->record_size = ntfs_calc_record_size(record_size_code, fs->cluster_size);
    fs->index_record_size = ntfs_calc_record_size(index_size_code, fs->cluster_size);
    if (fs->cluster_size == 0u || fs->record_size == 0u || fs->index_record_size == 0u ||
        fs->record_size > NTFS_MAX_RECORD_SIZE || fs->index_record_size > NTFS_MAX_INDEX_RECORD_SIZE) {
        return -1;
    }

    if (ntfs_load_record(fs, 0u, record0) != 0) {
        return -1;
    }
    if (ntfs_find_attr(record0, fs->record_size, NTFS_ATTR_DATA, 1u, &mft_data_attr) != 0) {
        return -1;
    }
    if (ntfs_nonresident_info(mft_data_attr, rd32(mft_data_attr + 4u), &runlist, &runlist_size, &data_size) != 0) {
        return -1;
    }
    if (ntfs_parse_runlist(runlist, runlist_size, fs->mft_runs, NTFS_MAX_RUNS, &fs->mft_run_count) != 0) {
        return -1;
    }

    fs->mft_data_size = data_size;
    return ntfs_load_record(fs, NTFS_ROOT_RECORD, record0) == 0 ? 0 : -1;
}

int ntfs_list_dir(const ntfs_fs_t* fs, uint64_t dir_record, ntfs_dirent_t* out, uint32_t max_entries, uint32_t* out_count) {
    ntfs_list_ctx_t ctx;

    if (!out || !out_count) {
        return -1;
    }
    *out_count = 0u;
    if (max_entries == 0u) {
        return 0;
    }

    ctx.out = out;
    ctx.max_entries = max_entries;
    ctx.count = 0u;
    if (ntfs_scan_directory(fs, dir_record, ntfs_dir_list_cb, &ctx) != 0) {
        return -1;
    }

    *out_count = ctx.count;
    return 0;
}

int ntfs_lookup(const ntfs_fs_t* fs, uint64_t dir_record, const char* name, ntfs_dirent_t* out) {
    ntfs_lookup_ctx_t ctx;
    int rc;

    if (!fs || !name || !name[0] || !out) {
        return -1;
    }

    mem_zero(&ctx, sizeof(ctx));
    ctx.target = name;
    rc = ntfs_scan_directory(fs, dir_record, ntfs_dir_lookup_cb, &ctx);
    if (rc < 0 || !ctx.found) {
        return -1;
    }

    *out = ctx.best;
    return 0;
}

int ntfs_read_file(const ntfs_fs_t* fs, uint64_t record_no, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    uint8_t record[NTFS_MAX_RECORD_SIZE];
    const uint8_t* data_attr;
    uint64_t data_size = 0u;

    if (!fs || (!out_buf && out_buf_size != 0u) || !out_size) {
        return -1;
    }
    *out_size = 0u;

    if (ntfs_load_record(fs, record_no, record) != 0) {
        return -1;
    }
    if ((rd16(record + 22u) & NTFS_RECORD_FLAG_DIRECTORY) != 0u) {
        return -1;
    }

    if (ntfs_find_attr(record, fs->record_size, NTFS_ATTR_DATA, 1u, &data_attr) != 0) {
        if (ntfs_find_attr(record, fs->record_size, NTFS_ATTR_ATTRIBUTE_LIST, 0u, &data_attr) == 0) {
            return -1;
        }
        return 0;
    }

    if (data_attr[8] == 0u) {
        const uint8_t* value;
        uint32_t value_len;

        if (ntfs_resident_value(data_attr, rd32(data_attr + 4u), &value, &value_len) != 0) {
            return -1;
        }
        *out_size = value_len;
        if (value_len == 0u) {
            return 0;
        }
        if (!out_buf) {
            return 0;
        }
        if (value_len > out_buf_size) {
            return -1;
        }
        mem_copy(out_buf, value, value_len);
        return 0;
    }

    if (ntfs_read_nonresident_attr(fs, data_attr, rd32(data_attr + 4u), 0u, NULL, 0u, &data_size) != 0) {
        return -1;
    }
    *out_size = (uint32_t)data_size;
    if (data_size == 0u) {
        return 0;
    }
    if (!out_buf) {
        return 0;
    }
    if (data_size > (uint64_t)out_buf_size) {
        return -1;
    }
    if (ntfs_read_nonresident_attr(fs, data_attr, rd32(data_attr + 4u), 0u, out_buf, (uint32_t)data_size, NULL) != 0) {
        return -1;
    }

    return 0;
}

int ntfs_write_file(ntfs_fs_t* fs, uint64_t record_no, const uint8_t* data, uint32_t size) {
    uint8_t record[NTFS_MAX_RECORD_SIZE];
    const uint8_t* data_attr;

    if (!fs || (!data && size != 0u)) {
        return -1;
    }
    if (ntfs_load_record(fs, record_no, record) != 0) {
        return -1;
    }
    if ((rd16(record + 22u) & NTFS_RECORD_FLAG_DIRECTORY) != 0u) {
        return -1;
    }

    if (ntfs_find_attr(record, fs->record_size, NTFS_ATTR_DATA, 1u, &data_attr) != 0) {
        return -1;
    }

    if (data_attr[8] == 0u) {
        uint16_t value_off = rd16(data_attr + 20u);
        uint32_t value_len = rd32(data_attr + 16u);
        uint32_t attr_off = (uint32_t)(data_attr - record);

        if (size != value_len) {
            return -1;
        }
        if (attr_off + value_off > fs->record_size || size > fs->record_size - (attr_off + value_off)) {
            return -1;
        }
        if (size != 0u) {
            mem_copy(record + attr_off + value_off, data, size);
        }
        return ntfs_store_record(fs, record_no, record);
    }

    {
        uint64_t data_size = 0u;

        if (ntfs_write_nonresident_attr(fs, data_attr, rd32(data_attr + 4u), 0u, NULL, 0u, &data_size) != 0) {
            return -1;
        }
        if ((uint64_t)size != data_size) {
            return -1;
        }
        if (size == 0u) {
            return 0;
        }
        return ntfs_write_nonresident_attr(fs, data_attr, rd32(data_attr + 4u), 0u, data, size, NULL);
    }
}

int ntfs_record_is_dir(const ntfs_fs_t* fs, uint64_t record_no, int* out_is_dir) {
    uint8_t record[NTFS_MAX_RECORD_SIZE];

    if (!fs || !out_is_dir) {
        return -1;
    }
    if (record_no == NTFS_ROOT_RECORD) {
        *out_is_dir = 1;
        return 0;
    }
    if (ntfs_load_record(fs, record_no, record) != 0) {
        return -1;
    }

    *out_is_dir = (rd16(record + 22u) & NTFS_RECORD_FLAG_DIRECTORY) != 0u ? 1 : 0;
    return 0;
}
