// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>
#include <linux/vmalloc.h>

#define MYAFS_HOST_MAGIC 0x4d594146u /* MYAF */
#define MYAFS_HOST_NAME "myafs"
#define MYAFS_HOST_MAX_PARTS 64u
#define MYAFS_HOST_IMAGE_MAX (32u * 1024u * 1024u)
#define MYAFS_HOST_V2_SB_BLOCK 8u
#define MYAFS_HOST_V2_BLOCK_SIZE 4096u
#define MYAFS_HOST_V2_DATA_MAX MYAFS_HOST_IMAGE_MAX

enum myafs_host_format {
    MYAFS_HOST_FMT_UNKNOWN = 0,
    MYAFS_HOST_FMT_LEGACY = 1,
    MYAFS_HOST_FMT_V2 = 2,
};

static const u8 g_myafs_host_v2_sb_magic[8] = { 'M', 'Y', 'A', 'F', 'S', 'B', '2', '\0' };

struct myafs_host_node {
    u64 ino;
    bool is_dir;
    bool linked;
    char *name;
    char *path;
    u8 *data;
    size_t size;
    struct myafs_host_node *parent;
    struct myafs_host_node **children;
    u32 child_count;
    u32 child_cap;
};

struct myafs_host_sb_info {
    char *label;
    char *image_path;
    struct myafs_host_node *root;
    struct myafs_host_node **nodes;
    u32 node_count;
    u32 node_cap;
    u64 next_ino;
    enum myafs_host_format format;
    u32 v2_block_size;
    u64 v2_data_start;
    u64 v2_recovery_log_start;
    struct mutex lock;
};

struct myafs_host_superblock_v2 {
    u8 magic[8];
    u16 version_major;
    u16 version_minor;
    u32 header_bytes;

    u32 block_size;
    u32 super_index;
    u64 total_blocks;

    u64 boot_start;
    u64 boot_blocks;

    u64 super_start;
    u32 super_count;
    u32 active_checkpoint;

    u64 checkpoint_start;
    u32 checkpoint_count;
    u32 checkpoint_blocks_per_entry;

    u64 allocator_start;
    u64 allocator_blocks;

    u64 object_tree_start;
    u64 extent_tree_start;
    u64 directory_tree_start;

    u64 data_start;

    u64 recovery_log_start;
    u64 recovery_log_blocks;

    u64 snapshot_meta_start;
    u64 snapshot_meta_blocks;

    u8 fs_uuid[16];
    char label[32];
} __packed;

struct myafs_host_mount_opts {
    char image_path[PATH_MAX];
};

static const struct super_operations myafs_host_super_ops;
static const struct inode_operations myafs_host_dir_inode_ops;
static const struct file_operations myafs_host_dir_file_ops;
static const struct file_operations myafs_host_file_ops;
static const struct fs_context_operations myafs_host_context_ops;

static int myafs_host_save_image_locked(struct myafs_host_sb_info *sbi);
static void myafs_host_free_node(struct myafs_host_node *node);

static struct myafs_host_sb_info *myafs_host_sbi(struct super_block *sb)
{
    return (struct myafs_host_sb_info *)sb->s_fs_info;
}

static struct myafs_host_node *myafs_host_find_child(const struct myafs_host_node *dir, const char *name, size_t name_len)
{
    u32 i;

    if (!dir || !dir->is_dir) {
        return NULL;
    }

    for (i = 0; i < dir->child_count; i++) {
        if (strlen(dir->children[i]->name) == name_len &&
            strncmp(dir->children[i]->name, name, name_len) == 0) {
            return dir->children[i];
        }
    }
    return NULL;
}

static int myafs_host_add_node_ref(struct myafs_host_sb_info *sbi, struct myafs_host_node *node)
{
    struct myafs_host_node **new_nodes;
    u32 new_cap;

    if (sbi->node_count < sbi->node_cap) {
        sbi->nodes[sbi->node_count++] = node;
        return 0;
    }

    new_cap = sbi->node_cap ? sbi->node_cap * 2u : 64u;
    new_nodes = krealloc(sbi->nodes, sizeof(*new_nodes) * new_cap, GFP_KERNEL);
    if (!new_nodes) {
        return -ENOMEM;
    }

    sbi->nodes = new_nodes;
    sbi->node_cap = new_cap;
    sbi->nodes[sbi->node_count++] = node;
    return 0;
}

static int myafs_host_add_child(struct myafs_host_node *parent, struct myafs_host_node *child)
{
    struct myafs_host_node **new_children;
    u32 new_cap;

    if (parent->child_count < parent->child_cap) {
        parent->children[parent->child_count++] = child;
        return 0;
    }

    new_cap = parent->child_cap ? parent->child_cap * 2u : 8u;
    new_children = krealloc(parent->children, sizeof(*new_children) * new_cap, GFP_KERNEL);
    if (!new_children) {
        return -ENOMEM;
    }

    parent->children = new_children;
    parent->child_cap = new_cap;
    parent->children[parent->child_count++] = child;
    return 0;
}

static int myafs_host_remove_child(struct myafs_host_node *parent, struct myafs_host_node *child)
{
    u32 i;

    if (!parent || !child || !parent->is_dir) {
        return -EINVAL;
    }

    for (i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            for (; i + 1u < parent->child_count; i++) {
                parent->children[i] = parent->children[i + 1u];
            }
            parent->child_count--;
            return 0;
        }
    }

    return -ENOENT;
}

static void myafs_host_remove_node_ref(struct myafs_host_sb_info *sbi, struct myafs_host_node *node)
{
    u32 i;

    if (!sbi || !node) {
        return;
    }

    for (i = 0; i < sbi->node_count; i++) {
        if (sbi->nodes[i] == node) {
            for (; i + 1u < sbi->node_count; i++) {
                sbi->nodes[i] = sbi->nodes[i + 1u];
            }
            sbi->node_count--;
            return;
        }
    }
}

static char *myafs_host_build_child_path(const char *parent_path, const char *name)
{
    size_t parent_len = strlen(parent_path);
    size_t name_len = strlen(name);
    size_t total;
    char *path;

    if (strcmp(parent_path, "/") == 0) {
        total = 1u + name_len + 1u;
        path = kmalloc(total, GFP_KERNEL);
        if (!path) {
            return NULL;
        }
        path[0] = '/';
        memcpy(path + 1u, name, name_len);
        path[1u + name_len] = '\0';
        return path;
    }

    total = parent_len + 1u + name_len + 1u;
    path = kmalloc(total, GFP_KERNEL);
    if (!path) {
        return NULL;
    }

    memcpy(path, parent_path, parent_len);
    path[parent_len] = '/';
    memcpy(path + parent_len + 1u, name, name_len);
    path[parent_len + 1u + name_len] = '\0';
    return path;
}

static struct myafs_host_node *myafs_host_alloc_node(struct myafs_host_sb_info *sbi, const char *name, const char *path, bool is_dir)
{
    struct myafs_host_node *node;

    node = kzalloc(sizeof(*node), GFP_KERNEL);
    if (!node) {
        return NULL;
    }

    node->name = kstrdup(name, GFP_KERNEL);
    node->path = kstrdup(path, GFP_KERNEL);
    if (!node->name || !node->path) {
        kfree(node->name);
        kfree(node->path);
        kfree(node);
        return NULL;
    }

    node->ino = ++sbi->next_ino;
    node->is_dir = is_dir;
    node->linked = false;
    return node;
}

static int myafs_host_create_child_locked(
    struct myafs_host_sb_info *sbi,
    struct myafs_host_node *parent,
    const char *name,
    size_t name_len,
    bool is_dir,
    struct myafs_host_node **out_node
) {
    char *name_copy;
    char *child_path;
    struct myafs_host_node *node;
    int rc;

    if (!sbi || !parent || !name || name_len == 0u || !out_node || !parent->is_dir) {
        return -EINVAL;
    }
    if (memchr(name, '/', name_len) != NULL) {
        return -EINVAL;
    }
    if (myafs_host_find_child(parent, name, name_len)) {
        return -EEXIST;
    }

    name_copy = kmalloc(name_len + 1u, GFP_KERNEL);
    if (!name_copy) {
        return -ENOMEM;
    }
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    child_path = myafs_host_build_child_path(parent->path, name_copy);
    if (!child_path) {
        kfree(name_copy);
        return -ENOMEM;
    }

    node = myafs_host_alloc_node(sbi, name_copy, child_path, is_dir);
    kfree(name_copy);
    kfree(child_path);
    if (!node) {
        return -ENOMEM;
    }

    node->parent = parent;
    node->linked = true;
    rc = myafs_host_add_child(parent, node);
    if (rc != 0) {
        kfree(node->name);
        kfree(node->path);
        kfree(node);
        return rc;
    }

    rc = myafs_host_add_node_ref(sbi, node);
    if (rc != 0) {
        (void)myafs_host_remove_child(parent, node);
        kfree(node->name);
        kfree(node->path);
        kfree(node);
        return rc;
    }

    *out_node = node;
    return 0;
}

static int myafs_host_ensure_root(struct myafs_host_sb_info *sbi)
{
    struct myafs_host_node *root;

    if (sbi->root) {
        return 0;
    }

    root = myafs_host_alloc_node(sbi, "/", "/", true);
    if (!root) {
        return -ENOMEM;
    }
    if (myafs_host_add_node_ref(sbi, root) != 0) {
        kfree(root->name);
        kfree(root->path);
        kfree(root);
        return -ENOMEM;
    }

    sbi->root = root;
    root->linked = true;
    return 0;
}

static size_t myafs_host_serialized_size_locked(const struct myafs_host_sb_info *sbi)
{
    size_t total = 0u;
    u32 i;

    if (!sbi) {
        return 0u;
    }

    total += 7u; /* MYAFS1\n */
    total += 6u + strlen(sbi->label ? sbi->label : "myafs") + 1u; /* LABEL|...\n */

    for (i = 0; i < sbi->node_count; i++) {
        const struct myafs_host_node *node = sbi->nodes[i];
        size_t path_len;

        if (!node || !node->path || !node->linked) {
            continue;
        }

        path_len = strlen(node->path);
        if (node->is_dir) {
            total += 9u + path_len; /* NODE|D|path|\n */
        } else {
            if (node->size > (SIZE_MAX - total - 9u - path_len) / 2u) {
                return 0u;
            }
            total += 9u + path_len + (node->size * 2u); /* NODE|F|path|hex...\n */
        }
    }

    return total;
}

static int myafs_host_serialize_locked(struct myafs_host_sb_info *sbi, char **out_buf, size_t *out_size)
{
    static const char g_hex[] = "0123456789abcdef";
    char *buf;
    size_t cap;
    size_t pos = 0u;
    int wrote;
    u32 i;

    if (!sbi || !out_buf || !out_size) {
        return -EINVAL;
    }

    cap = myafs_host_serialized_size_locked(sbi);
    if (cap == 0u) {
        return -EOVERFLOW;
    }

    buf = vmalloc(cap + 1u);
    if (!buf) {
        return -ENOMEM;
    }

    wrote = snprintf(buf + pos, cap + 1u - pos, "MYAFS1\n");
    if (wrote < 0 || (size_t)wrote >= cap + 1u - pos) {
        vfree(buf);
        return -EOVERFLOW;
    }
    pos += (size_t)wrote;

    wrote = snprintf(buf + pos, cap + 1u - pos, "LABEL|%s\n", sbi->label ? sbi->label : "myafs");
    if (wrote < 0 || (size_t)wrote >= cap + 1u - pos) {
        vfree(buf);
        return -EOVERFLOW;
    }
    pos += (size_t)wrote;

    for (i = 0; i < sbi->node_count; i++) {
        struct myafs_host_node *node = sbi->nodes[i];
        if (!node || !node->path || !node->linked) {
            continue;
        }

        if (node->is_dir) {
            wrote = snprintf(buf + pos, cap + 1u - pos, "NODE|D|%s|\n", node->path);
            if (wrote < 0 || (size_t)wrote >= cap + 1u - pos) {
                vfree(buf);
                return -EOVERFLOW;
            }
            pos += (size_t)wrote;
        } else {
            size_t j;
            wrote = snprintf(buf + pos, cap + 1u - pos, "NODE|F|%s|", node->path);
            if (wrote < 0 || (size_t)wrote >= cap + 1u - pos) {
                vfree(buf);
                return -EOVERFLOW;
            }
            pos += (size_t)wrote;
            for (j = 0; j < node->size; j++) {
                u8 b = node->data ? node->data[j] : 0u;
                if (pos + 2u >= cap + 1u) {
                    vfree(buf);
                    return -EOVERFLOW;
                }
                buf[pos++] = g_hex[(b >> 4) & 0x0f];
                buf[pos++] = g_hex[b & 0x0f];
            }
            if (pos + 1u >= cap + 1u) {
                vfree(buf);
                return -EOVERFLOW;
            }
            buf[pos++] = '\n';
        }
    }

    if (pos > cap) {
        vfree(buf);
        return -EOVERFLOW;
    }
    buf[pos] = '\0';

    *out_buf = buf;
    *out_size = pos;
    return 0;
}

static int myafs_host_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int myafs_host_decode_hex(const char *hex, u8 **out_data, size_t *out_size)
{
    size_t len;
    size_t i;
    size_t size;
    u8 *buf;

    if (!hex || !out_data || !out_size) {
        return -EINVAL;
    }

    len = strlen(hex);
    if ((len % 2u) != 0u) {
        return -EINVAL;
    }

    size = len / 2u;
    if (size == 0u) {
        *out_data = NULL;
        *out_size = 0u;
        return 0;
    }

    buf = kmalloc(size, GFP_KERNEL);
    if (!buf) {
        return -ENOMEM;
    }

    for (i = 0; i < len; i += 2u) {
        int hi = myafs_host_hex_nibble(hex[i]);
        int lo = myafs_host_hex_nibble(hex[i + 1u]);
        if (hi < 0 || lo < 0) {
            kfree(buf);
            return -EINVAL;
        }
        buf[i / 2u] = (u8)((hi << 4) | lo);
    }

    *out_data = buf;
    *out_size = size;
    return 0;
}

static int myafs_host_apply_leaf(struct myafs_host_node *leaf, bool is_dir, u8 *data, size_t size)
{
    if (!leaf) {
        return -EINVAL;
    }

    if (is_dir) {
        if (!leaf->is_dir && leaf->child_count > 0) {
            return -EINVAL;
        }
        leaf->is_dir = true;
        kvfree(leaf->data);
        leaf->data = NULL;
        leaf->size = 0u;
        leaf->linked = true;
        return 0;
    }

    if (leaf->is_dir && leaf->child_count > 0) {
        return -EINVAL;
    }

    leaf->is_dir = false;
    kvfree(leaf->data);
    leaf->data = data;
    leaf->size = size;
    leaf->linked = true;
    return 0;
}

static int myafs_host_add_path_node(struct myafs_host_sb_info *sbi, const char *path, bool is_dir, u8 *data, size_t size)
{
    char *mutable_path;
    char *cursor;
    char *parts[MYAFS_HOST_MAX_PARTS];
    u32 part_count = 0u;
    struct myafs_host_node *cur;
    u32 i;

    if (!sbi || !path || path[0] != '/') {
        return -EINVAL;
    }

    if (myafs_host_ensure_root(sbi) != 0) {
        return -ENOMEM;
    }

    if (strcmp(path, "/") == 0) {
        sbi->root->linked = true;
        return myafs_host_apply_leaf(sbi->root, true, NULL, 0u);
    }

    mutable_path = kstrdup(path, GFP_KERNEL);
    if (!mutable_path) {
        return -ENOMEM;
    }

    cursor = mutable_path;
    while (cursor && part_count < MYAFS_HOST_MAX_PARTS) {
        char *part = strsep(&cursor, "/");
        if (!part || !part[0]) {
            continue;
        }
        parts[part_count++] = part;
    }

    if (part_count == 0u) {
        kfree(mutable_path);
        return -EINVAL;
    }

    cur = sbi->root;
    for (i = 0; i < part_count; i++) {
        bool last = (i + 1u == part_count);
        struct myafs_host_node *child = myafs_host_find_child(cur, parts[i], strlen(parts[i]));

        if (!child) {
            char *child_path = myafs_host_build_child_path(cur->path, parts[i]);
            bool child_is_dir = last ? is_dir : true;
            if (!child_path) {
                kfree(mutable_path);
                return -ENOMEM;
            }

            child = myafs_host_alloc_node(sbi, parts[i], child_path, child_is_dir);
            kfree(child_path);
            if (!child) {
                kfree(mutable_path);
                return -ENOMEM;
            }
            child->parent = cur;
            child->linked = true;
            if (myafs_host_add_child(cur, child) != 0 || myafs_host_add_node_ref(sbi, child) != 0) {
                kfree(child->name);
                kfree(child->path);
                kfree(child);
                kfree(mutable_path);
                return -ENOMEM;
            }
        }

        if (last) {
            int rc = myafs_host_apply_leaf(child, is_dir, data, size);
            if (rc != 0) {
                kfree(mutable_path);
                return rc;
            }
            data = NULL;
        } else if (!child->is_dir) {
            kfree(mutable_path);
            return -EINVAL;
        }

        cur = child;
    }

    kfree(mutable_path);
    return 0;
}

static int myafs_host_parse_image(struct myafs_host_sb_info *sbi, char *buffer)
{
    char *cursor = buffer;
    char *line;
    bool saw_magic = false;

    while ((line = strsep(&cursor, "\n")) != NULL) {
        size_t len;

        if (!line) {
            continue;
        }

        len = strlen(line);
        if (len > 0u && line[len - 1u] == '\r') {
            line[len - 1u] = '\0';
            len--;
        }
        if (len == 0u) {
            continue;
        }

        if (!saw_magic) {
            if (strcmp(line, "MYAFS1") != 0) {
                return -EINVAL;
            }
            saw_magic = true;
            continue;
        }

        if (strncmp(line, "LABEL|", 6u) == 0) {
            kfree(sbi->label);
            sbi->label = kstrdup(line + 6u, GFP_KERNEL);
            if (!sbi->label) {
                return -ENOMEM;
            }
            continue;
        }

        if (strncmp(line, "NODE|", 5u) == 0) {
            char *rest = line + 5u;
            char *type = strsep(&rest, "|");
            char *path = strsep(&rest, "|");
            char *payload = rest ? rest : "";
            bool is_dir;
            u8 *data = NULL;
            size_t size = 0u;
            int rc;

            if (!type || !path) {
                return -EINVAL;
            }
            if (strcmp(type, "D") == 0) {
                is_dir = true;
            } else if (strcmp(type, "F") == 0) {
                is_dir = false;
            } else {
                return -EINVAL;
            }

            if (!is_dir) {
                rc = myafs_host_decode_hex(payload, &data, &size);
                if (rc != 0) {
                    return rc;
                }
            }

            rc = myafs_host_add_path_node(sbi, path, is_dir, data, size);
            if (rc != 0) {
                kfree(data);
                return rc;
            }
            continue;
        }

        return -EINVAL;
    }

    return saw_magic ? 0 : -EINVAL;
}

static int myafs_host_load_image(struct myafs_host_sb_info *sbi, const char *image_path)
{
    struct file *filp;
    u8 head[8];
    loff_t pos = 0;
    loff_t size;
    ssize_t read_bytes;
    char *buffer;
    int rc;

    filp = filp_open(image_path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        return PTR_ERR(filp);
    }

    read_bytes = kernel_read(filp, head, sizeof(head), &pos);
    if (read_bytes < 0) {
        filp_close(filp, NULL);
        return (int)read_bytes;
    }

    if (read_bytes >= 6 && memcmp(head, "MYAFS1", 6) == 0) {
        pos = 0;
        size = i_size_read(file_inode(filp));
        if (size <= 0 || size > MYAFS_HOST_IMAGE_MAX) {
            filp_close(filp, NULL);
            return -EINVAL;
        }

        buffer = vmalloc((size_t)size + 1u);
        if (!buffer) {
            filp_close(filp, NULL);
            return -ENOMEM;
        }

        read_bytes = kernel_read(filp, buffer, (size_t)size, &pos);
        filp_close(filp, NULL);
        if (read_bytes < 0) {
            vfree(buffer);
            return (int)read_bytes;
        }

        buffer[read_bytes] = '\0';
        rc = myafs_host_parse_image(sbi, buffer);
        vfree(buffer);
        if (rc == 0) {
            sbi->format = MYAFS_HOST_FMT_LEGACY;
        }
        return rc;
    }

    {
        struct myafs_host_superblock_v2 sb2;
        char label_copy[33];
        loff_t sb_pos = (loff_t)MYAFS_HOST_V2_SB_BLOCK * (loff_t)MYAFS_HOST_V2_BLOCK_SIZE;
        u64 area_bytes_u64;
        size_t payload_bytes;
        loff_t payload_pos;

        read_bytes = kernel_read(filp, &sb2, sizeof(sb2), &sb_pos);
        if (read_bytes < (ssize_t)sizeof(sb2)) {
            filp_close(filp, NULL);
            return -EINVAL;
        }
        if (memcmp(sb2.magic, g_myafs_host_v2_sb_magic, sizeof(g_myafs_host_v2_sb_magic)) != 0) {
            filp_close(filp, NULL);
            return -EINVAL;
        }
        if (sb2.version_major != 2u) {
            filp_close(filp, NULL);
            return -EINVAL;
        }
        if (sb2.block_size == 0u || sb2.recovery_log_start <= sb2.data_start) {
            filp_close(filp, NULL);
            return -EINVAL;
        }

        memset(label_copy, 0, sizeof(label_copy));
        memcpy(label_copy, sb2.label, sizeof(sb2.label));
        label_copy[sizeof(sb2.label)] = '\0';
        kfree(sbi->label);
        sbi->label = kstrdup(label_copy[0] ? label_copy : "myafs", GFP_KERNEL);
        if (!sbi->label) {
            filp_close(filp, NULL);
            return -ENOMEM;
        }

        rc = myafs_host_ensure_root(sbi);
        if (rc != 0) {
            filp_close(filp, NULL);
            return rc;
        }

        sbi->format = MYAFS_HOST_FMT_V2;
        sbi->v2_block_size = sb2.block_size;
        sbi->v2_data_start = sb2.data_start;
        sbi->v2_recovery_log_start = sb2.recovery_log_start;

        area_bytes_u64 = (sb2.recovery_log_start - sb2.data_start) * (u64)sb2.block_size;
        payload_bytes = (size_t)min_t(u64, area_bytes_u64, (u64)MYAFS_HOST_V2_DATA_MAX);
        if (payload_bytes == 0u) {
            filp_close(filp, NULL);
            return 0;
        }

        buffer = vmalloc(payload_bytes + 1u);
        if (!buffer) {
            filp_close(filp, NULL);
            return -ENOMEM;
        }

        payload_pos = (loff_t)sb2.data_start * (loff_t)sb2.block_size;
        read_bytes = kernel_read(filp, buffer, payload_bytes, &payload_pos);
        filp_close(filp, NULL);
        if (read_bytes < 0) {
            vfree(buffer);
            return (int)read_bytes;
        }

        payload_bytes = strnlen(buffer, (size_t)read_bytes);
        buffer[payload_bytes] = '\0';
        if (payload_bytes >= 6u && memcmp(buffer, "MYAFS1", 6u) == 0) {
            rc = myafs_host_parse_image(sbi, buffer);
            vfree(buffer);
            return rc;
        }

        vfree(buffer);
        return 0;
    }

    return -EINVAL;
}

static int myafs_host_save_image_locked(struct myafs_host_sb_info *sbi)
{
    struct file *filp;
    char *payload = NULL;
    size_t payload_size = 0u;
    loff_t pos;
    ssize_t wrote;
    int rc;

    if (!sbi || !sbi->image_path) {
        return -EINVAL;
    }

    rc = myafs_host_serialize_locked(sbi, &payload, &payload_size);
    if (rc != 0) {
        return rc;
    }

    if (sbi->format == MYAFS_HOST_FMT_LEGACY) {
        filp = filp_open(sbi->image_path, O_WRONLY | O_TRUNC | O_CREAT, 0644);
        if (IS_ERR(filp)) {
            vfree(payload);
            return PTR_ERR(filp);
        }

        pos = 0;
        wrote = kernel_write(filp, payload, payload_size, &pos);
        if (wrote < 0) {
            rc = (int)wrote;
        } else if ((size_t)wrote != payload_size) {
            rc = -EIO;
        } else {
            rc = 0;
        }
        if (rc == 0) {
            (void)vfs_fsync(filp, 0);
        }
        filp_close(filp, NULL);
        vfree(payload);
        return rc;
    }

    if (sbi->format == MYAFS_HOST_FMT_V2) {
        u64 data_area_bytes_u64;
        size_t data_area_bytes;
        const char zero = '\0';

        if (sbi->v2_block_size == 0u || sbi->v2_recovery_log_start <= sbi->v2_data_start) {
            vfree(payload);
            return -EINVAL;
        }

        data_area_bytes_u64 = (sbi->v2_recovery_log_start - sbi->v2_data_start) * (u64)sbi->v2_block_size;
        data_area_bytes = (size_t)min_t(u64, data_area_bytes_u64, (u64)MYAFS_HOST_V2_DATA_MAX);
        if (payload_size + 1u > data_area_bytes) {
            vfree(payload);
            return -ENOSPC;
        }

        filp = filp_open(sbi->image_path, O_WRONLY, 0);
        if (IS_ERR(filp)) {
            vfree(payload);
            return PTR_ERR(filp);
        }

        pos = (loff_t)sbi->v2_data_start * (loff_t)sbi->v2_block_size;
        wrote = kernel_write(filp, payload, payload_size, &pos);
        if (wrote < 0) {
            rc = (int)wrote;
        } else if ((size_t)wrote != payload_size) {
            rc = -EIO;
        } else {
            wrote = kernel_write(filp, &zero, 1u, &pos);
            rc = (wrote < 0) ? (int)wrote : (wrote == 1 ? 0 : -EIO);
        }

        if (rc == 0) {
            (void)vfs_fsync(filp, 0);
        }
        filp_close(filp, NULL);
        vfree(payload);
        return rc;
    }

    vfree(payload);
    return -EINVAL;
}

static void myafs_host_free_node(struct myafs_host_node *node)
{
    if (!node) {
        return;
    }
    kfree(node->name);
    kfree(node->path);
    kvfree(node->data);
    kfree(node->children);
    kfree(node);
}

static void myafs_host_free_sbi(struct myafs_host_sb_info *sbi)
{
    u32 i;

    if (!sbi) {
        return;
    }

    for (i = 0; i < sbi->node_count; i++) {
        myafs_host_free_node(sbi->nodes[i]);
    }
    kfree(sbi->nodes);
    kfree(sbi->label);
    kfree(sbi->image_path);
    kfree(sbi);
}

static int myafs_host_extract_image_option(void *raw_data, char *out_path, size_t out_size)
{
    char *opts;
    char *scan;
    char *token;

    if (!out_path || out_size == 0u) {
        return -EINVAL;
    }
    out_path[0] = '\0';

    if (!raw_data) {
        return -EINVAL;
    }

    opts = kstrdup((const char *)raw_data, GFP_KERNEL);
    if (!opts) {
        return -ENOMEM;
    }

    scan = opts;
    while ((token = strsep(&scan, ",")) != NULL) {
        if (!token[0]) {
            continue;
        }
        if (strncmp(token, "image=", 6u) == 0) {
            strscpy(out_path, token + 6u, out_size);
            break;
        }
        if (!out_path[0] && token[0] == '/') {
            strscpy(out_path, token, out_size);
        }
    }

    kfree(opts);
    return out_path[0] ? 0 : -EINVAL;
}

static struct inode *myafs_host_get_inode(struct super_block *sb, struct myafs_host_node *node)
{
    struct inode *inode;

    inode = new_inode(sb);
    if (!inode) {
        return NULL;
    }

    inode->i_ino = node->ino;
    inode->i_private = node;
    inode->i_uid = GLOBAL_ROOT_UID;
    inode->i_gid = GLOBAL_ROOT_GID;
    simple_inode_init_ts(inode);

    if (node->is_dir) {
        inode->i_mode = S_IFDIR | 0755;
        inode->i_op = &myafs_host_dir_inode_ops;
        inode->i_fop = &myafs_host_dir_file_ops;
        inode->i_size = 0;
        set_nlink(inode, 2);
    } else {
        inode->i_mode = S_IFREG | 0644;
        inode->i_fop = &myafs_host_file_ops;
        inode->i_size = node->size;
        set_nlink(inode, 1);
    }

    return inode;
}

static ssize_t myafs_host_file_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
    struct myafs_host_node *node = file_inode(file)->i_private;
    struct myafs_host_sb_info *sbi = myafs_host_sbi(file_inode(file)->i_sb);
    ssize_t rc;

    if (!node || node->is_dir) {
        return -EISDIR;
    }

    mutex_lock(&sbi->lock);
    rc = simple_read_from_buffer(buf, len, ppos, node->data, node->size);
    mutex_unlock(&sbi->lock);
    return rc;
}

static ssize_t myafs_host_file_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct myafs_host_node *node = inode->i_private;
    struct myafs_host_sb_info *sbi = myafs_host_sbi(inode->i_sb);
    loff_t orig_pos;
    loff_t write_end;
    loff_t tmp_pos;
    size_t new_size;
    size_t committed_size;
    u8 *new_data = NULL;
    u8 *old_data;
    size_t old_size;
    ssize_t wrote;
    int rc;

    if (!node || node->is_dir) {
        return -EISDIR;
    }
    if (sb_rdonly(inode->i_sb)) {
        return -EROFS;
    }
    if (!ppos || *ppos < 0) {
        return -EINVAL;
    }
    if (len == 0u) {
        return 0;
    }

    orig_pos = *ppos;
    write_end = orig_pos + (loff_t)len;
    if (write_end < orig_pos || (u64)write_end > (u64)SIZE_MAX) {
        return -EFBIG;
    }

    mutex_lock(&sbi->lock);

    new_size = node->size;
    if ((size_t)write_end > new_size) {
        new_size = (size_t)write_end;
    }

    new_data = kvmalloc(new_size, GFP_KERNEL);
    if (!new_data) {
        rc = -ENOMEM;
        goto out_unlock;
    }
    memset(new_data, 0, new_size);
    if (node->data && node->size > 0u) {
        memcpy(new_data, node->data, node->size);
    }

    tmp_pos = orig_pos;
    wrote = simple_write_to_buffer(new_data, new_size, &tmp_pos, buf, len);
    if (wrote < 0) {
        rc = (int)wrote;
        goto out_unlock;
    }

    committed_size = node->size;
    if ((size_t)tmp_pos > committed_size) {
        committed_size = (size_t)tmp_pos;
    }

    old_data = node->data;
    old_size = node->size;
    node->data = new_data;
    node->size = committed_size;
    inode->i_size = committed_size;

    rc = myafs_host_save_image_locked(sbi);
    if (rc != 0) {
        node->data = old_data;
        node->size = old_size;
        inode->i_size = old_size;
        goto out_unlock;
    }

    *ppos = tmp_pos;
    mark_inode_dirty(inode);
    kvfree(old_data);
    mutex_unlock(&sbi->lock);
    return wrote;

out_unlock:
    kvfree(new_data);
    mutex_unlock(&sbi->lock);
    return rc;
}

static int myafs_host_iterate(struct file *file, struct dir_context *ctx)
{
    struct myafs_host_node *dir = file_inode(file)->i_private;
    struct myafs_host_sb_info *sbi = myafs_host_sbi(file_inode(file)->i_sb);
    u32 i;
    int rc = 0;

    if (!dir || !dir->is_dir) {
        return -ENOTDIR;
    }

    mutex_lock(&sbi->lock);
    if (!dir_emit_dots(file, ctx)) {
        goto out;
    }

    for (i = (u32)(ctx->pos - 2); i < dir->child_count; i++) {
        struct myafs_host_node *child = dir->children[i];
        unsigned type = child->is_dir ? DT_DIR : DT_REG;

        if (!dir_emit(ctx, child->name, strlen(child->name), child->ino, type)) {
            goto out;
        }
        ctx->pos++;
    }

out:
    mutex_unlock(&sbi->lock);
    return rc;
}

static struct dentry *myafs_host_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    struct myafs_host_node *parent = dir->i_private;
    struct myafs_host_sb_info *sbi = myafs_host_sbi(dir->i_sb);
    struct inode *inode = NULL;
    struct myafs_host_node *child;

    (void)flags;

    if (!parent || !parent->is_dir) {
        return ERR_PTR(-ENOTDIR);
    }

    mutex_lock(&sbi->lock);
    child = myafs_host_find_child(parent, dentry->d_name.name, dentry->d_name.len);
    if (child) {
        inode = myafs_host_get_inode(dir->i_sb, child);
        if (!inode) {
            mutex_unlock(&sbi->lock);
            return ERR_PTR(-ENOMEM);
        }
    }
    mutex_unlock(&sbi->lock);

    d_add(dentry, inode);
    return NULL;
}

static void myafs_host_discard_new_node_locked(struct myafs_host_sb_info *sbi, struct myafs_host_node *node)
{
    if (!sbi || !node) {
        return;
    }
    if (node->parent) {
        (void)myafs_host_remove_child(node->parent, node);
    }
    node->parent = NULL;
    node->linked = false;
    myafs_host_remove_node_ref(sbi, node);
    myafs_host_free_node(node);
}

static int myafs_host_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct myafs_host_sb_info *sbi = myafs_host_sbi(dir->i_sb);
    struct myafs_host_node *parent = dir->i_private;
    struct myafs_host_node *node = NULL;
    struct inode *inode = NULL;
    int rc;

    (void)idmap;
    (void)mode;
    (void)excl;

    if (!parent || !parent->is_dir) {
        return -ENOTDIR;
    }
    if (sb_rdonly(dir->i_sb)) {
        return -EROFS;
    }

    mutex_lock(&sbi->lock);
    rc = myafs_host_create_child_locked(sbi, parent, dentry->d_name.name, dentry->d_name.len, false, &node);
    if (rc != 0) {
        goto out_unlock;
    }

    inode = myafs_host_get_inode(dir->i_sb, node);
    if (!inode) {
        myafs_host_discard_new_node_locked(sbi, node);
        rc = -ENOMEM;
        goto out_unlock;
    }

    rc = myafs_host_save_image_locked(sbi);
    if (rc != 0) {
        iput(inode);
        myafs_host_discard_new_node_locked(sbi, node);
        goto out_unlock;
    }

    d_instantiate(dentry, inode);
    mark_inode_dirty(dir);

out_unlock:
    mutex_unlock(&sbi->lock);
    return rc;
}

static struct dentry *myafs_host_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode)
{
    struct myafs_host_sb_info *sbi = myafs_host_sbi(dir->i_sb);
    struct myafs_host_node *parent = dir->i_private;
    struct myafs_host_node *node = NULL;
    struct inode *inode = NULL;
    int rc;

    (void)idmap;
    (void)mode;

    if (!parent || !parent->is_dir) {
        return ERR_PTR(-ENOTDIR);
    }
    if (sb_rdonly(dir->i_sb)) {
        return ERR_PTR(-EROFS);
    }

    mutex_lock(&sbi->lock);
    rc = myafs_host_create_child_locked(sbi, parent, dentry->d_name.name, dentry->d_name.len, true, &node);
    if (rc != 0) {
        goto out_unlock;
    }

    inode = myafs_host_get_inode(dir->i_sb, node);
    if (!inode) {
        myafs_host_discard_new_node_locked(sbi, node);
        rc = -ENOMEM;
        goto out_unlock;
    }

    rc = myafs_host_save_image_locked(sbi);
    if (rc != 0) {
        iput(inode);
        myafs_host_discard_new_node_locked(sbi, node);
        goto out_unlock;
    }

    d_instantiate(dentry, inode);
    inc_nlink(dir);
    mark_inode_dirty(dir);

out_unlock:
    mutex_unlock(&sbi->lock);
    if (rc != 0) {
        return ERR_PTR(rc);
    }
    return NULL;
}

static int myafs_host_unlink(struct inode *dir, struct dentry *dentry)
{
    struct myafs_host_sb_info *sbi = myafs_host_sbi(dir->i_sb);
    struct myafs_host_node *parent = dir->i_private;
    struct inode *inode = d_inode(dentry);
    struct myafs_host_node *node;
    int rc;

    if (!inode) {
        return -ENOENT;
    }
    if (!parent || !parent->is_dir) {
        return -ENOTDIR;
    }
    if (sb_rdonly(dir->i_sb)) {
        return -EROFS;
    }

    node = inode->i_private;
    if (!node) {
        return -ENOENT;
    }
    if (node->is_dir) {
        return -EISDIR;
    }

    mutex_lock(&sbi->lock);
    if (!node->linked || node->parent != parent) {
        rc = -ENOENT;
        goto out_unlock;
    }

    rc = myafs_host_remove_child(parent, node);
    if (rc != 0) {
        goto out_unlock;
    }
    node->parent = NULL;
    node->linked = false;

    rc = myafs_host_save_image_locked(sbi);
    if (rc != 0) {
        node->parent = parent;
        node->linked = true;
        (void)myafs_host_add_child(parent, node);
        goto out_unlock;
    }

    clear_nlink(inode);
    mark_inode_dirty(inode);
    mark_inode_dirty(dir);

out_unlock:
    mutex_unlock(&sbi->lock);
    return rc;
}

static int myafs_host_rmdir(struct inode *dir, struct dentry *dentry)
{
    struct myafs_host_sb_info *sbi = myafs_host_sbi(dir->i_sb);
    struct myafs_host_node *parent = dir->i_private;
    struct inode *inode = d_inode(dentry);
    struct myafs_host_node *node;
    int rc;

    if (!inode) {
        return -ENOENT;
    }
    if (!parent || !parent->is_dir) {
        return -ENOTDIR;
    }
    if (sb_rdonly(dir->i_sb)) {
        return -EROFS;
    }

    node = inode->i_private;
    if (!node || !node->is_dir) {
        return -ENOTDIR;
    }

    mutex_lock(&sbi->lock);
    if (!node->linked || node->parent != parent) {
        rc = -ENOENT;
        goto out_unlock;
    }
    if (node->child_count != 0u) {
        rc = -ENOTEMPTY;
        goto out_unlock;
    }

    rc = myafs_host_remove_child(parent, node);
    if (rc != 0) {
        goto out_unlock;
    }
    node->parent = NULL;
    node->linked = false;

    rc = myafs_host_save_image_locked(sbi);
    if (rc != 0) {
        node->parent = parent;
        node->linked = true;
        (void)myafs_host_add_child(parent, node);
        goto out_unlock;
    }

    clear_nlink(inode);
    drop_nlink(dir);
    mark_inode_dirty(inode);
    mark_inode_dirty(dir);

out_unlock:
    mutex_unlock(&sbi->lock);
    return rc;
}

static void myafs_host_put_super(struct super_block *sb)
{
    struct myafs_host_sb_info *sbi = myafs_host_sbi(sb);

    myafs_host_free_sbi(sbi);
    sb->s_fs_info = NULL;
}

static int myafs_host_fill_super(struct super_block *sb, struct fs_context *fc)
{
    struct myafs_host_mount_opts *opts = (struct myafs_host_mount_opts *)fc->fs_private;
    struct myafs_host_sb_info *sbi;
    struct inode *root_inode;
    const char *image_path;
    int rc;

    sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
    if (!sbi) {
        return -ENOMEM;
    }
    sbi->next_ino = 1u;
    mutex_init(&sbi->lock);
    sb->s_fs_info = sbi;

    image_path = NULL;
    if (opts && opts->image_path[0]) {
        image_path = opts->image_path;
    } else if (fc->source && fc->source[0] && strcmp(fc->source, "none") != 0) {
        image_path = fc->source;
    }

    if (!image_path) {
        pr_err("myafs: mount requires source path or -o image=/path/to/image.mya\n");
        rc = -EINVAL;
        goto fail;
    }

    sbi->image_path = kstrdup(image_path, GFP_KERNEL);
    if (!sbi->image_path) {
        rc = -ENOMEM;
        goto fail;
    }

    rc = myafs_host_load_image(sbi, image_path);
    if (rc != 0) {
        pr_err("myafs: failed to load image '%s' (%d)\n", image_path, rc);
        goto fail;
    }

    if (!sbi->root) {
        rc = -EINVAL;
        goto fail;
    }

    sb->s_magic = MYAFS_HOST_MAGIC;
    sb->s_op = &myafs_host_super_ops;
    sb->s_time_gran = 1;
    sb->s_blocksize = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_maxbytes = MAX_LFS_FILESIZE;

    root_inode = myafs_host_get_inode(sb, sbi->root);
    if (!root_inode) {
        rc = -ENOMEM;
        goto fail;
    }

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        rc = -ENOMEM;
        goto fail;
    }

    return 0;

fail:
    myafs_host_free_sbi(sbi);
    sb->s_fs_info = NULL;
    return rc;
}

static void myafs_host_fc_free(struct fs_context *fc)
{
    kfree(fc->fs_private);
    fc->fs_private = NULL;
}

static int myafs_host_fc_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
    struct myafs_host_mount_opts *opts = (struct myafs_host_mount_opts *)fc->fs_private;

    if (!opts || !param || !param->key) {
        return -EINVAL;
    }

    if (strcmp(param->key, "image") == 0) {
        if (param->type != fs_value_is_string || !param->string || !param->string[0]) {
            return -EINVAL;
        }
        strscpy(opts->image_path, param->string, sizeof(opts->image_path));
        return 0;
    }

    return -ENOPARAM;
}

static int myafs_host_fc_parse_monolithic(struct fs_context *fc, void *data)
{
    struct myafs_host_mount_opts *opts = (struct myafs_host_mount_opts *)fc->fs_private;
    char *raw = (char *)data;

    if (!opts) {
        return -EINVAL;
    }
    if (!raw || raw[0] == '\0') {
        return 0;
    }

    /* Accept generic/unknown mount options, but capture image=... when present. */
    (void)myafs_host_extract_image_option(raw, opts->image_path, sizeof(opts->image_path));
    return 0;
}

static int myafs_host_get_tree(struct fs_context *fc)
{
    return get_tree_nodev(fc, myafs_host_fill_super);
}

static int myafs_host_init_fs_context(struct fs_context *fc)
{
    struct myafs_host_mount_opts *opts;

    opts = kzalloc(sizeof(*opts), GFP_KERNEL);
    if (!opts) {
        return -ENOMEM;
    }

    fc->ops = &myafs_host_context_ops;
    fc->fs_private = opts;
    return 0;
}

static const struct super_operations myafs_host_super_ops = {
    .statfs = simple_statfs,
    .put_super = myafs_host_put_super,
};

static const struct inode_operations myafs_host_dir_inode_ops = {
    .lookup = myafs_host_lookup,
    .create = myafs_host_create,
    .mkdir = myafs_host_mkdir,
    .unlink = myafs_host_unlink,
    .rmdir = myafs_host_rmdir,
};

static const struct file_operations myafs_host_dir_file_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = myafs_host_iterate,
    .llseek = default_llseek,
};

static const struct file_operations myafs_host_file_ops = {
    .owner = THIS_MODULE,
    .read = myafs_host_file_read,
    .write = myafs_host_file_write,
    .llseek = default_llseek,
};

static const struct fs_context_operations myafs_host_context_ops = {
    .free = myafs_host_fc_free,
    .parse_param = myafs_host_fc_parse_param,
    .parse_monolithic = myafs_host_fc_parse_monolithic,
    .get_tree = myafs_host_get_tree,
};

static struct file_system_type myafs_host_fs_type = {
    .owner = THIS_MODULE,
    .name = MYAFS_HOST_NAME,
    .init_fs_context = myafs_host_init_fs_context,
    .kill_sb = kill_anon_super,
    .fs_flags = 0,
};

static int __init myafs_host_init(void)
{
    int rc = register_filesystem(&myafs_host_fs_type);
    if (rc == 0) {
        pr_info("myafs: host filesystem module loaded\n");
    }
    return rc;
}

static void __exit myafs_host_exit(void)
{
    unregister_filesystem(&myafs_host_fs_type);
    pr_info("myafs: host filesystem module unloaded\n");
}

module_init(myafs_host_init);
module_exit(myafs_host_exit);

MODULE_AUTHOR("MyaOS");
MODULE_DESCRIPTION("MyaFS host kernel filesystem module (read-write)");
MODULE_LICENSE("GPL");
