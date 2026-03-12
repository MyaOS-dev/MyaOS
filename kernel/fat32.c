#include "fat32.h"
#include <stddef.h>

#define FAT32_DEMO_SECTOR_SIZE 512u
#define FAT32_DEMO_TOTAL_SECTORS 64u
#define FAT32_DEMO_RESERVED_SECTORS 2u
#define FAT32_DEMO_FAT_COUNT 1u
#define FAT32_DEMO_SECTORS_PER_FAT 1u
#define FAT32_DEMO_ROOT_CLUSTER 2u

#define FAT32_ATTR_DIR 0x10u
#define FAT32_ATTR_FILE 0x20u
#define FAT32_ATTR_LFN 0x0Fu
#define FAT32_CLUSTER_END 0x0FFFFFF8u
#define FAT32_CLUSTER_FREE 0x00000000u

static uint8_t g_demo_image[FAT32_DEMO_SECTOR_SIZE * FAT32_DEMO_TOTAL_SECTORS];
static uint8_t g_demo_ready;

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void mem_zero(void* dst, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    for (size_t i = 0; i < size; i++) {
        out[i] = 0;
    }
}

static void mem_copy(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

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

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static int str_eq_ci(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (to_lower_ascii(a[i]) != to_lower_ascii(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int valid_short_char(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    return c == '_' || c == '-';
}

static int build_short_name(const char* name, uint8_t out[11]) {
    for (uint32_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }

    if (!name || name[0] == '\0') {
        return -1;
    }

    if (str_eq_ci(name, ".") || str_eq_ci(name, "..")) {
        if (name[1] == '\0') {
            out[0] = '.';
        } else {
            out[0] = '.';
            out[1] = '.';
        }
        return 0;
    }

    uint32_t name_pos = 0;
    uint32_t ext_pos = 8;
    uint8_t seen_dot = 0;

    for (size_t i = 0; name[i]; i++) {
        char c = to_upper_ascii(name[i]);
        if (c == '.') {
            if (seen_dot) {
                return -1;
            }
            seen_dot = 1;
            continue;
        }

        if (!valid_short_char(c)) {
            return -1;
        }

        if (!seen_dot) {
            if (name_pos >= 8) {
                return -1;
            }
            out[name_pos++] = (uint8_t)c;
        } else {
            if (ext_pos >= 11) {
                return -1;
            }
            out[ext_pos++] = (uint8_t)c;
        }
    }

    return 0;
}

static void format_short_name(const uint8_t* entry, char out[FAT32_NAME_MAX]) {
    size_t pos = 0;

    for (size_t i = 0; i < 8; i++) {
        char c = (char)entry[i];
        if (c == ' ') {
            break;
        }
        if (pos + 1 < FAT32_NAME_MAX) {
            out[pos++] = c;
        }
    }

    uint8_t has_ext = 0;
    for (size_t i = 8; i < 11; i++) {
        if (entry[i] != ' ') {
            has_ext = 1;
            break;
        }
    }

    if (has_ext && pos + 1 < FAT32_NAME_MAX) {
        out[pos++] = '.';
    }

    if (has_ext) {
        for (size_t i = 8; i < 11; i++) {
            char c = (char)entry[i];
            if (c == ' ') {
                break;
            }
            if (pos + 1 < FAT32_NAME_MAX) {
                out[pos++] = c;
            }
        }
    }

    out[pos] = '\0';
}

static void read_entry_to_dirent(const uint8_t* entry, fat32_dirent_t* out) {
    format_short_name(entry, out->name);
    out->is_dir = (entry[11] & FAT32_ATTR_DIR) ? 1u : 0u;
    out->first_cluster = ((uint32_t)rd16(&entry[20]) << 16) | (uint32_t)rd16(&entry[26]);
    out->size = rd32(&entry[28]);
}

static void write_short_entry(
    uint8_t* entry,
    const uint8_t short_name[11],
    uint8_t attr,
    uint32_t first_cluster,
    uint32_t size
) {
    mem_zero(entry, 32);
    mem_copy(entry, short_name, 11);
    entry[11] = attr;
    wr16(&entry[20], (uint16_t)((first_cluster >> 16) & 0xFFFFu));
    wr16(&entry[26], (uint16_t)(first_cluster & 0xFFFFu));
    wr32(&entry[28], size);
}

static const uint8_t* sector_ptr_const(const fat32_fs_t* fs, uint32_t sector) {
    if (!fs->mounted || sector >= fs->total_sectors) {
        return NULL;
    }

    uint64_t offset = (uint64_t)sector * fs->bytes_per_sector;
    if (offset + fs->bytes_per_sector > fs->image_size) {
        return NULL;
    }

    return fs->image + offset;
}

static uint8_t* sector_ptr_mut(fat32_fs_t* fs, uint32_t sector) {
    if (!fs->mounted || sector >= fs->total_sectors) {
        return NULL;
    }

    uint64_t offset = (uint64_t)sector * fs->bytes_per_sector;
    if (offset + fs->bytes_per_sector > fs->image_size) {
        return NULL;
    }

    return fs->image + offset;
}

static uint32_t cluster_to_sector(const fat32_fs_t* fs, uint32_t cluster) {
    if (cluster < 2) {
        return 0;
    }
    return fs->first_data_sector + (cluster - 2u) * fs->sectors_per_cluster;
}

static uint32_t fat_max_cluster(const fat32_fs_t* fs) {
    if (fs->total_sectors <= fs->first_data_sector || fs->sectors_per_cluster == 0) {
        return 1;
    }

    uint32_t data_sectors = fs->total_sectors - fs->first_data_sector;
    uint32_t cluster_count = data_sectors / fs->sectors_per_cluster;
    return cluster_count + 1u;
}

static uint32_t fat_get_next_cluster(const fat32_fs_t* fs, uint32_t cluster) {
    uint64_t fat_offset = (uint64_t)cluster * 4u;
    uint64_t fat_base = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector;
    uint64_t addr = fat_base + fat_offset;

    if (addr + 4u > fs->image_size) {
        return FAT32_CLUSTER_END;
    }

    return rd32(fs->image + addr) & 0x0FFFFFFFu;
}

static int fat_set_cluster(fat32_fs_t* fs, uint32_t cluster, uint32_t value) {
    uint64_t fat_offset = (uint64_t)cluster * 4u;
    uint64_t fat_span = (uint64_t)fs->sectors_per_fat * fs->bytes_per_sector;
    value &= 0x0FFFFFFFu;

    for (uint32_t fat_index = 0; fat_index < fs->fat_count; fat_index++) {
        uint64_t fat_base =
            ((uint64_t)fs->reserved_sectors + (uint64_t)fat_index * fs->sectors_per_fat) * fs->bytes_per_sector;
        uint64_t addr = fat_base + fat_offset;

        if (addr + 4u > fs->image_size || fat_offset >= fat_span) {
            continue;
        }
        wr32(fs->image + addr, value);
    }

    return 0;
}

static int zero_cluster_data(fat32_fs_t* fs, uint32_t cluster) {
    uint32_t first_sector = cluster_to_sector(fs, cluster);

    for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
        uint8_t* sec = sector_ptr_mut(fs, first_sector + s);
        if (!sec) {
            return -1;
        }
        mem_zero(sec, fs->bytes_per_sector);
    }

    return 0;
}

static int alloc_free_cluster(fat32_fs_t* fs, uint32_t* out_cluster) {
    uint32_t max_cluster = fat_max_cluster(fs);
    for (uint32_t cluster = 2; cluster <= max_cluster; cluster++) {
        if (fat_get_next_cluster(fs, cluster) == FAT32_CLUSTER_FREE) {
            fat_set_cluster(fs, cluster, 0x0FFFFFFFu);
            if (zero_cluster_data(fs, cluster) != 0) {
                fat_set_cluster(fs, cluster, FAT32_CLUSTER_FREE);
                return -1;
            }
            *out_cluster = cluster;
            return 0;
        }
    }

    return -1;
}

static void free_cluster_chain(fat32_fs_t* fs, uint32_t first_cluster) {
    uint32_t cluster = first_cluster;
    uint32_t guard = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_END && guard < 8192) {
        uint32_t next = fat_get_next_cluster(fs, cluster);
        fat_set_cluster(fs, cluster, FAT32_CLUSTER_FREE);

        if (next >= FAT32_CLUSTER_END) {
            break;
        }

        cluster = next;
        guard++;
    }
}

static int find_entry_in_dir(
    const fat32_fs_t* fs,
    uint32_t dir_cluster,
    const char* name,
    uint8_t out_entry[32],
    uint32_t* out_sector,
    uint32_t* out_off
) {
    uint8_t short_name[11];
    if (build_short_name(name, short_name) != 0) {
        return -1;
    }

    if (dir_cluster < 2) {
        dir_cluster = fs->root_cluster;
    }

    uint32_t cluster = dir_cluster;
    uint32_t guard = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_END && guard < 8192) {
        uint32_t first_sector = cluster_to_sector(fs, cluster);

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            const uint8_t* sec = sector_ptr_const(fs, first_sector + s);
            if (!sec) {
                return -1;
            }

            for (uint32_t off = 0; off < fs->bytes_per_sector; off += 32) {
                const uint8_t* entry = sec + off;
                if (entry[0] == 0x00) {
                    return -2;
                }
                if (entry[0] == 0xE5 || entry[11] == FAT32_ATTR_LFN) {
                    continue;
                }

                uint8_t same = 1;
                for (uint32_t i = 0; i < 11; i++) {
                    if (entry[i] != short_name[i]) {
                        same = 0;
                        break;
                    }
                }

                if (same) {
                    mem_copy(out_entry, entry, 32);
                    if (out_sector) {
                        *out_sector = first_sector + s;
                    }
                    if (out_off) {
                        *out_off = off;
                    }
                    return 0;
                }
            }
        }

        cluster = fat_get_next_cluster(fs, cluster);
        guard++;
    }

    return -2;
}

static int find_free_dir_slot(
    fat32_fs_t* fs,
    uint32_t dir_cluster,
    uint32_t* out_sector,
    uint32_t* out_off
) {
    if (dir_cluster < 2) {
        dir_cluster = fs->root_cluster;
    }

    uint32_t cluster = dir_cluster;
    uint32_t last_cluster = dir_cluster;
    uint32_t guard = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_END && guard < 8192) {
        last_cluster = cluster;
        uint32_t first_sector = cluster_to_sector(fs, cluster);

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint8_t* sec = sector_ptr_mut(fs, first_sector + s);
            if (!sec) {
                return -1;
            }

            for (uint32_t off = 0; off < fs->bytes_per_sector; off += 32) {
                uint8_t first = sec[off];
                if (first == 0x00 || first == 0xE5) {
                    *out_sector = first_sector + s;
                    *out_off = off;
                    return 0;
                }
            }
        }

        uint32_t next = fat_get_next_cluster(fs, cluster);
        if (next >= FAT32_CLUSTER_END) {
            break;
        }

        cluster = next;
        guard++;
    }

    uint32_t new_cluster = 0;
    if (alloc_free_cluster(fs, &new_cluster) != 0) {
        return -1;
    }

    fat_set_cluster(fs, last_cluster, new_cluster);
    fat_set_cluster(fs, new_cluster, 0x0FFFFFFFu);

    uint32_t new_sector = cluster_to_sector(fs, new_cluster);
    *out_sector = new_sector;
    *out_off = 0;
    return 0;
}

static int allocate_cluster_chain(fat32_fs_t* fs, uint32_t count, uint32_t* out_first) {
    uint32_t first = 0;
    uint32_t prev = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t current = 0;
        if (alloc_free_cluster(fs, &current) != 0) {
            if (first != 0) {
                free_cluster_chain(fs, first);
            }
            return -1;
        }

        if (prev != 0) {
            fat_set_cluster(fs, prev, current);
        } else {
            first = current;
        }

        prev = current;
    }

    fat_set_cluster(fs, prev, 0x0FFFFFFFu);
    *out_first = first;
    return 0;
}

static int write_data_to_chain(fat32_fs_t* fs, uint32_t first_cluster, const uint8_t* data, uint32_t size) {
    uint32_t cluster = first_cluster;
    uint32_t remaining = size;
    uint32_t copied = 0;
    uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t guard = 0;

    while (remaining > 0 && cluster >= 2 && cluster < FAT32_CLUSTER_END && guard < 8192) {
        uint32_t first_sector = cluster_to_sector(fs, cluster);
        uint32_t chunk = (remaining < cluster_bytes) ? remaining : cluster_bytes;
        uint32_t local_copied = 0;

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint8_t* sec = sector_ptr_mut(fs, first_sector + s);
            if (!sec) {
                return -1;
            }

            uint32_t room = fs->bytes_per_sector;
            uint32_t todo = chunk - local_copied;
            if (todo > room) {
                todo = room;
            }

            if (todo > 0) {
                mem_copy(sec, data + copied + local_copied, todo);
            }
            local_copied += todo;
        }

        copied += chunk;
        remaining -= chunk;
        if (remaining == 0) {
            break;
        }

        cluster = fat_get_next_cluster(fs, cluster);
        guard++;
    }

    return (remaining == 0) ? 0 : -1;
}

static void build_demo_image(void) {
    if (g_demo_ready) {
        return;
    }

    mem_zero(g_demo_image, sizeof(g_demo_image));

    uint8_t* boot = g_demo_image + 0 * FAT32_DEMO_SECTOR_SIZE;
    boot[0] = 0xEB;
    boot[1] = 0x58;
    boot[2] = 0x90;
    mem_copy(&boot[3], "MYAOS   ", 8);
    wr16(&boot[11], FAT32_DEMO_SECTOR_SIZE);
    boot[13] = 1;
    wr16(&boot[14], FAT32_DEMO_RESERVED_SECTORS);
    boot[16] = FAT32_DEMO_FAT_COUNT;
    wr16(&boot[17], 0);
    wr16(&boot[19], 0);
    boot[21] = 0xF8;
    wr16(&boot[22], 0);
    wr16(&boot[24], 63);
    wr16(&boot[26], 255);
    wr32(&boot[28], 0);
    wr32(&boot[32], FAT32_DEMO_TOTAL_SECTORS);
    wr32(&boot[36], FAT32_DEMO_SECTORS_PER_FAT);
    wr16(&boot[40], 0);
    wr16(&boot[42], 0);
    wr32(&boot[44], FAT32_DEMO_ROOT_CLUSTER);
    wr16(&boot[48], 1);
    wr16(&boot[50], 6);
    boot[64] = 0x80;
    boot[66] = 0x29;
    wr32(&boot[67], 0x4D59414Fu);
    mem_copy(&boot[71], "MYAOSVOL   ", 11);
    mem_copy(&boot[82], "FAT32   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;

    uint8_t* fsinfo = g_demo_image + 1 * FAT32_DEMO_SECTOR_SIZE;
    wr32(&fsinfo[0], 0x41615252u);
    wr32(&fsinfo[484], 0x61417272u);
    wr32(&fsinfo[488], 0xFFFFFFFFu);
    wr32(&fsinfo[492], 0xFFFFFFFFu);
    fsinfo[510] = 0x55;
    fsinfo[511] = 0xAA;

    uint8_t* fat = g_demo_image + 2 * FAT32_DEMO_SECTOR_SIZE;
    wr32(&fat[0 * 4], 0x0FFFFFF8u);
    wr32(&fat[1 * 4], 0xFFFFFFFFu);
    wr32(&fat[2 * 4], 0x0FFFFFFFu);
    wr32(&fat[3 * 4], 0x0FFFFFFFu);

    uint8_t* root = g_demo_image + 3 * FAT32_DEMO_SECTOR_SIZE;
    mem_copy(&root[0], "README  TXT", 11);
    root[11] = FAT32_ATTR_FILE;
    wr16(&root[20], 0);
    wr16(&root[26], 3);

    const char* text =
        "MyaOS FAT32 demo volume\n"
        "Use: fatls, fatinfo, fatcat README.TXT\n";
    uint32_t text_len = (uint32_t)str_len(text);
    wr32(&root[28], text_len);
    root[32] = 0x00;

    uint8_t* file_data = g_demo_image + 4 * FAT32_DEMO_SECTOR_SIZE;
    mem_copy(file_data, text, text_len);

    g_demo_ready = 1;
}

int fat32_mount(fat32_fs_t* fs, uint8_t* image, uint64_t image_size) {
    if (!fs || !image || image_size < 512) {
        return -1;
    }

    const uint8_t* bpb = image;
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) {
        return -1;
    }

    uint16_t bytes_per_sector = rd16(&bpb[11]);
    uint8_t sectors_per_cluster = bpb[13];
    uint16_t reserved_sectors = rd16(&bpb[14]);
    uint8_t fat_count = bpb[16];
    uint32_t total_sectors = rd16(&bpb[19]);
    if (total_sectors == 0) {
        total_sectors = rd32(&bpb[32]);
    }
    uint32_t sectors_per_fat = rd16(&bpb[22]);
    if (sectors_per_fat == 0) {
        sectors_per_fat = rd32(&bpb[36]);
    }
    uint32_t root_cluster = rd32(&bpb[44]);

    if (bytes_per_sector == 0 || sectors_per_cluster == 0 || reserved_sectors == 0 ||
        fat_count == 0 || sectors_per_fat == 0 || total_sectors == 0 || root_cluster < 2) {
        return -1;
    }

    uint32_t first_data_sector = (uint32_t)reserved_sectors + (uint32_t)fat_count * sectors_per_fat;
    if (first_data_sector >= total_sectors) {
        return -1;
    }

    uint32_t root_first_sector = first_data_sector + (root_cluster - 2u) * sectors_per_cluster;
    if (root_first_sector >= total_sectors) {
        return -1;
    }

    uint64_t min_required = (uint64_t)(root_first_sector + sectors_per_cluster) * bytes_per_sector;
    if (min_required > image_size) {
        return -1;
    }

    fs->image = image;
    fs->image_size = image_size;
    fs->bytes_per_sector = bytes_per_sector;
    fs->sectors_per_cluster = sectors_per_cluster;
    fs->reserved_sectors = reserved_sectors;
    fs->fat_count = fat_count;
    fs->sectors_per_fat = sectors_per_fat;
    fs->total_sectors = total_sectors;
    fs->root_cluster = root_cluster;
    fs->first_data_sector = first_data_sector;
    fs->mounted = 1;
    return 0;
}

int fat32_mount_demo(fat32_fs_t* fs) {
    build_demo_image();
    return fat32_mount(fs, g_demo_image, sizeof(g_demo_image));
}

uint32_t fat32_root_cluster(const fat32_fs_t* fs) {
    return fs ? fs->root_cluster : 0;
}

int fat32_list_dir(
    const fat32_fs_t* fs,
    uint32_t dir_cluster,
    fat32_dirent_t* entries,
    size_t max_entries,
    size_t* out_count
) {
    if (!fs || !entries || !out_count || !fs->mounted) {
        return -1;
    }

    if (dir_cluster < 2) {
        dir_cluster = fs->root_cluster;
    }

    size_t count = 0;
    uint32_t cluster = dir_cluster;
    uint32_t guard = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_END && guard < 8192) {
        uint32_t first_sector = cluster_to_sector(fs, cluster);

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            const uint8_t* sec = sector_ptr_const(fs, first_sector + s);
            if (!sec) {
                return -1;
            }

            for (uint32_t off = 0; off < fs->bytes_per_sector; off += 32) {
                const uint8_t* entry = sec + off;
                if (entry[0] == 0x00) {
                    *out_count = count;
                    return 0;
                }
                if (entry[0] == 0xE5 || entry[11] == FAT32_ATTR_LFN) {
                    continue;
                }
                if (entry[11] & 0x08u) {
                    continue;
                }
                if (count >= max_entries) {
                    *out_count = count;
                    return -2;
                }

                read_entry_to_dirent(entry, &entries[count]);
                count++;
            }
        }

        cluster = fat_get_next_cluster(fs, cluster);
        guard++;
    }

    *out_count = count;
    return 0;
}

int fat32_lookup(
    const fat32_fs_t* fs,
    uint32_t dir_cluster,
    const char* name,
    fat32_dirent_t* out
) {
    uint8_t entry[32];
    if (!fs || !name || !out || !fs->mounted) {
        return -1;
    }

    int rc = find_entry_in_dir(fs, dir_cluster, name, entry, NULL, NULL);
    if (rc != 0) {
        return rc;
    }

    read_entry_to_dirent(entry, out);
    return 0;
}

int fat32_read_file(
    const fat32_fs_t* fs,
    uint32_t dir_cluster,
    const char* name,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
) {
    if (!fs || !name || !out_buf || !out_size || !fs->mounted) {
        return -1;
    }

    uint8_t entry[32];
    int rc = find_entry_in_dir(fs, dir_cluster, name, entry, NULL, NULL);
    if (rc != 0) {
        return -2;
    }

    if (entry[11] & FAT32_ATTR_DIR) {
        return -3;
    }

    uint32_t file_size = rd32(&entry[28]);
    uint32_t cluster = ((uint32_t)rd16(&entry[20]) << 16) | (uint32_t)rd16(&entry[26]);

    if (file_size > out_buf_size) {
        return -4;
    }

    if (file_size == 0) {
        *out_size = 0;
        return 0;
    }

    uint32_t copied = 0;
    uint32_t remaining = file_size;
    uint32_t guard = 0;

    while (remaining > 0 && cluster >= 2 && cluster < FAT32_CLUSTER_END && guard < 8192) {
        uint32_t first_sector = cluster_to_sector(fs, cluster);

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            const uint8_t* sec = sector_ptr_const(fs, first_sector + s);
            if (!sec) {
                return -5;
            }

            uint32_t chunk = fs->bytes_per_sector;
            if (chunk > remaining) {
                chunk = remaining;
            }

            mem_copy(out_buf + copied, sec, chunk);
            copied += chunk;
            remaining -= chunk;

            if (remaining == 0) {
                break;
            }
        }

        if (remaining == 0) {
            break;
        }

        cluster = fat_get_next_cluster(fs, cluster);
        guard++;
    }

    if (remaining != 0) {
        return -5;
    }

    *out_size = copied;
    return 0;
}

int fat32_mkdir(fat32_fs_t* fs, uint32_t dir_cluster, const char* name) {
    if (!fs || !name || !fs->mounted) {
        return -1;
    }

    uint8_t short_name[11];
    if (build_short_name(name, short_name) != 0) {
        return -1;
    }

    uint8_t existing[32];
    if (find_entry_in_dir(fs, dir_cluster, name, existing, NULL, NULL) == 0) {
        return -2;
    }

    uint32_t new_cluster = 0;
    if (alloc_free_cluster(fs, &new_cluster) != 0) {
        return -3;
    }

    uint32_t slot_sector = 0;
    uint32_t slot_off = 0;
    if (find_free_dir_slot(fs, dir_cluster, &slot_sector, &slot_off) != 0) {
        free_cluster_chain(fs, new_cluster);
        return -4;
    }

    uint8_t* parent_sec = sector_ptr_mut(fs, slot_sector);
    if (!parent_sec) {
        free_cluster_chain(fs, new_cluster);
        return -4;
    }
    write_short_entry(parent_sec + slot_off, short_name, FAT32_ATTR_DIR, new_cluster, 0);

    uint32_t this_cluster = new_cluster;
    uint32_t parent_cluster = (dir_cluster < 2) ? fs->root_cluster : dir_cluster;
    uint32_t first_sector = cluster_to_sector(fs, new_cluster);
    uint8_t* new_sec = sector_ptr_mut(fs, first_sector);
    if (!new_sec) {
        return -4;
    }

    uint8_t dot_name[11];
    uint8_t dotdot_name[11];
    build_short_name(".", dot_name);
    build_short_name("..", dotdot_name);
    write_short_entry(new_sec + 0, dot_name, FAT32_ATTR_DIR, this_cluster, 0);
    write_short_entry(new_sec + 32, dotdot_name, FAT32_ATTR_DIR, parent_cluster, 0);
    return 0;
}

int fat32_create_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* name) {
    if (!fs || !name || !fs->mounted) {
        return -1;
    }

    uint8_t short_name[11];
    if (build_short_name(name, short_name) != 0) {
        return -1;
    }

    uint8_t existing[32];
    if (find_entry_in_dir(fs, dir_cluster, name, existing, NULL, NULL) == 0) {
        return -2;
    }

    uint32_t slot_sector = 0;
    uint32_t slot_off = 0;
    if (find_free_dir_slot(fs, dir_cluster, &slot_sector, &slot_off) != 0) {
        return -3;
    }

    uint8_t* sec = sector_ptr_mut(fs, slot_sector);
    if (!sec) {
        return -3;
    }

    write_short_entry(sec + slot_off, short_name, FAT32_ATTR_FILE, 0, 0);
    return 0;
}

int fat32_write_file(
    fat32_fs_t* fs,
    uint32_t dir_cluster,
    const char* name,
    const uint8_t* data,
    uint32_t size
) {
    if (!fs || !name || !fs->mounted) {
        return -1;
    }

    uint8_t entry[32];
    uint32_t entry_sector = 0;
    uint32_t entry_off = 0;

    int rc = find_entry_in_dir(fs, dir_cluster, name, entry, &entry_sector, &entry_off);
    if (rc == -2) {
        rc = fat32_create_file(fs, dir_cluster, name);
        if (rc != 0) {
            return -2;
        }
        rc = find_entry_in_dir(fs, dir_cluster, name, entry, &entry_sector, &entry_off);
    }
    if (rc != 0) {
        return -2;
    }

    if (entry[11] & FAT32_ATTR_DIR) {
        return -3;
    }

    uint32_t old_cluster = ((uint32_t)rd16(&entry[20]) << 16) | (uint32_t)rd16(&entry[26]);
    if (old_cluster >= 2) {
        free_cluster_chain(fs, old_cluster);
    }

    uint32_t new_first = 0;
    if (size > 0) {
        uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
        uint32_t needed = (size + cluster_bytes - 1u) / cluster_bytes;

        if (allocate_cluster_chain(fs, needed, &new_first) != 0) {
            return -4;
        }

        if (write_data_to_chain(fs, new_first, data, size) != 0) {
            free_cluster_chain(fs, new_first);
            return -4;
        }
    }

    uint8_t* sec = sector_ptr_mut(fs, entry_sector);
    if (!sec) {
        if (new_first >= 2) {
            free_cluster_chain(fs, new_first);
        }
        return -5;
    }

    uint8_t* dst = sec + entry_off;
    wr16(&dst[20], (uint16_t)((new_first >> 16) & 0xFFFFu));
    wr16(&dst[26], (uint16_t)(new_first & 0xFFFFu));
    wr32(&dst[28], size);
    return 0;
}

int fat32_list_root(const fat32_fs_t* fs, fat32_dirent_t* entries, size_t max_entries, size_t* out_count) {
    return fat32_list_dir(fs, fs->root_cluster, entries, max_entries, out_count);
}

int fat32_read_root_file(
    const fat32_fs_t* fs,
    const char* name,
    uint8_t* out_buf,
    uint32_t out_buf_size,
    uint32_t* out_size
) {
    return fat32_read_file(fs, fs->root_cluster, name, out_buf, out_buf_size, out_size);
}
