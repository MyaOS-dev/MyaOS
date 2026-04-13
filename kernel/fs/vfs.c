#include "vfs.h"
#include "scheduler.h"

typedef struct {
    char mount_path[MYAOS_PATH_MAX];
    char fs_name[MYAOS_NAME_MAX];
    char source[MYAOS_NAME_MAX];
    uint32_t disk_id;
    uint8_t read_only;
    void* ctx;
    const vfs_ops_t* ops;
} vfs_mount_t;

#define VFS_MAX_ACL 256u
#define VFS_MAX_CACHE 32u
#define VFS_CACHE_DATA_MAX 4096u
#define VFS_PERM_R 4u
#define VFS_PERM_W 2u
#define VFS_PERM_X 1u

typedef struct {
    uint8_t used;
    uint8_t is_dir;
    uint16_t mode;
    uint32_t owner_uid;
    char path[MYAOS_PATH_MAX];
} vfs_acl_t;

typedef struct {
    uint8_t used;
    uint8_t reserved0[3];
    uint32_t size;
    uint32_t tick;
    char path[MYAOS_PATH_MAX];
    uint8_t data[VFS_CACHE_DATA_MAX];
} vfs_cache_t;

static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];
static uint32_t g_mount_count;
static vfs_acl_t g_acl[VFS_MAX_ACL];
static vfs_cache_t g_cache[VFS_MAX_CACHE];
static uint32_t g_cache_tick;

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

static int path_has_suffix(const char* path, const char* suffix) {
    size_t path_len = str_len(path);
    size_t suffix_len = str_len(suffix);

    if (suffix_len == 0 || path_len < suffix_len) {
        return 0;
    }
    for (size_t i = 0; i < suffix_len; i++) {
        if (path[path_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

static uint16_t default_mode_for_path(const char* abs_path, uint8_t is_dir) {
    if (is_dir) {
        return 0755u;
    }
    if (path_has_suffix(abs_path, ".elf")) {
        return 0755u;
    }
    return 0644u;
}

static vfs_acl_t* acl_find(const char* abs_path) {
    for (uint32_t i = 0; i < VFS_MAX_ACL; i++) {
        if (g_acl[i].used && str_eq(g_acl[i].path, abs_path)) {
            return &g_acl[i];
        }
    }
    return NULL;
}

static vfs_acl_t* acl_ensure(const char* abs_path, uint8_t is_dir, uint8_t owner_is_current) {
    uint32_t uid = owner_is_current ? scheduler_current_uid() : 0u;
    vfs_acl_t* existing = acl_find(abs_path);

    if (existing) {
        return existing;
    }

    for (uint32_t i = 0; i < VFS_MAX_ACL; i++) {
        if (g_acl[i].used) {
            continue;
        }
        g_acl[i].used = 1u;
        g_acl[i].is_dir = is_dir;
        g_acl[i].mode = default_mode_for_path(abs_path, is_dir);
        g_acl[i].owner_uid = uid;
        str_copy(g_acl[i].path, abs_path, sizeof(g_acl[i].path));
        return &g_acl[i];
    }
    return NULL;
}

static uint8_t acl_has_perm(const vfs_acl_t* acl, uint8_t required) {
    uint32_t uid = scheduler_current_uid();
    uint16_t bits;

    if (!acl) {
        return 0u;
    }
    if (uid == 0u) {
        return 1u;
    }

    bits = (uid == acl->owner_uid) ? ((acl->mode >> 6) & 0x7u) : (acl->mode & 0x7u);
    return ((bits & required) == required) ? 1u : 0u;
}

static int acl_check(const char* abs_path, uint8_t required, uint8_t is_dir_hint, uint8_t owner_is_current_on_create) {
    vfs_acl_t* acl = acl_find(abs_path);
    vfs_acl_t fallback;

    if (!acl) {
        fallback.used = 1u;
        fallback.is_dir = is_dir_hint;
        fallback.mode = default_mode_for_path(abs_path, is_dir_hint);
        fallback.owner_uid = owner_is_current_on_create ? scheduler_current_uid() : 0u;
        fallback.path[0] = '\0';
        acl = &fallback;
    }
    return acl_has_perm(acl, required) ? 0 : -1;
}

static void acl_remove(const char* abs_path) {
    vfs_acl_t* acl = acl_find(abs_path);
    if (!acl) {
        return;
    }
    acl->used = 0u;
    acl->is_dir = 0u;
    acl->mode = 0u;
    acl->owner_uid = 0u;
    acl->path[0] = '\0';
}

static vfs_cache_t* cache_find(const char* abs_path) {
    for (uint32_t i = 0; i < VFS_MAX_CACHE; i++) {
        if (g_cache[i].used && str_eq(g_cache[i].path, abs_path)) {
            return &g_cache[i];
        }
    }
    return NULL;
}

static vfs_cache_t* cache_pick_slot(void) {
    vfs_cache_t* best = NULL;

    for (uint32_t i = 0; i < VFS_MAX_CACHE; i++) {
        if (!g_cache[i].used) {
            return &g_cache[i];
        }
        if (!best || g_cache[i].tick < best->tick) {
            best = &g_cache[i];
        }
    }
    return best;
}

static void cache_invalidate(const char* abs_path) {
    vfs_cache_t* cache = cache_find(abs_path);
    if (!cache) {
        return;
    }
    cache->used = 0u;
    cache->size = 0u;
    cache->tick = 0u;
    cache->path[0] = '\0';
}

static int cache_read(const char* abs_path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    vfs_cache_t* cache = cache_find(abs_path);
    if (!cache || !out_size || (cache->size != 0u && !out_buf)) {
        return -1;
    }
    if (cache->size > out_buf_size) {
        return -1;
    }
    for (uint32_t i = 0; i < cache->size; i++) {
        out_buf[i] = cache->data[i];
    }
    *out_size = cache->size;
    cache->tick = ++g_cache_tick;
    return 0;
}

static void cache_store(const char* abs_path, const uint8_t* data, uint32_t size) {
    vfs_cache_t* cache;

    if (!abs_path || (size != 0u && !data) || size > VFS_CACHE_DATA_MAX) {
        return;
    }
    cache = cache_find(abs_path);
    if (!cache) {
        cache = cache_pick_slot();
    }
    if (!cache) {
        return;
    }

    cache->used = 1u;
    cache->size = size;
    cache->tick = ++g_cache_tick;
    str_copy(cache->path, abs_path, sizeof(cache->path));
    for (uint32_t i = 0; i < size; i++) {
        cache->data[i] = data[i];
    }
}

static int append_path_component(char* out, size_t out_size, const char* component) {
    size_t len = str_len(out);
    size_t comp_len = str_len(component);

    if (len + comp_len + 2 > out_size) {
        return -1;
    }

    if (len > 1) {
        out[len++] = '/';
    }

    for (size_t i = 0; i < comp_len; i++) {
        out[len++] = component[i];
    }
    out[len] = '\0';
    return 0;
}

static int copy_prefix(char* dst, const char* src, size_t start, size_t end, size_t dst_size) {
    size_t pos = 0;

    if (dst_size == 0 || end < start) {
        return -1;
    }

    for (size_t i = start; i < end && pos + 1 < dst_size; i++) {
        dst[pos++] = src[i];
    }
    dst[pos] = '\0';
    return 0;
}

static void strip_mount_marker_suffix(char* part) {
    size_t len;

    if (!part || !part[0]) {
        return;
    }

    len = str_len(part);
    if (len > 1u && part[len - 1u] == '@') {
        part[len - 1u] = '\0';
    }
}

static int normalize_absolute_path(const char* path, char* out_abs_path, size_t out_size) {
    char parts[16][MYAOS_NAME_MAX];
    uint32_t depth = 0;
    size_t i = 0;

    if (!path || path[0] != '/' || out_size < 2) {
        return -1;
    }

    while (path[i]) {
        char part[MYAOS_NAME_MAX];
        size_t start;
        size_t end;

        while (path[i] == '/') {
            i++;
        }
        if (!path[i]) {
            break;
        }

        start = i;
        while (path[i] && path[i] != '/') {
            i++;
        }
        end = i;

        if (copy_prefix(part, path, start, end, sizeof(part)) != 0) {
            return -1;
        }
        strip_mount_marker_suffix(part);

        if (str_eq(part, ".")) {
            continue;
        }
        if (str_eq(part, "..")) {
            if (depth > 0) {
                depth--;
            }
            continue;
        }
        if (depth >= 16) {
            return -1;
        }

        str_copy(parts[depth], part, sizeof(parts[depth]));
        depth++;
    }

    out_abs_path[0] = '/';
    out_abs_path[1] = '\0';
    for (uint32_t part_idx = 0; part_idx < depth; part_idx++) {
        if (append_path_component(out_abs_path, out_size, parts[part_idx]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int path_has_prefix(const char* path, const char* prefix) {
    size_t prefix_len = str_len(prefix);

    if (prefix_len == 1 && prefix[0] == '/') {
        return path[0] == '/';
    }
    for (size_t i = 0; i < prefix_len; i++) {
        if (path[i] != prefix[i]) {
            return 0;
        }
    }
    return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

static const vfs_mount_t* resolve_mount(const char* abs_path) {
    const vfs_mount_t* best = NULL;
    size_t best_len = 0;

    for (uint32_t i = 0; i < g_mount_count; i++) {
        size_t mount_len = str_len(g_mounts[i].mount_path);
        if (!path_has_prefix(abs_path, g_mounts[i].mount_path)) {
            continue;
        }
        if (!best || mount_len > best_len) {
            best = &g_mounts[i];
            best_len = mount_len;
        }
    }

    return best;
}

static int make_relative_path(const vfs_mount_t* mount, const char* abs_path, char* out_path, size_t out_size) {
    size_t mount_len;

    if (!mount || !out_path || out_size == 0) {
        return -1;
    }

    mount_len = str_len(mount->mount_path);
    if (mount_len == 1 && mount->mount_path[0] == '/') {
        str_copy(out_path, abs_path, out_size);
        return 0;
    }

    if (!path_has_prefix(abs_path, mount->mount_path)) {
        return -1;
    }

    if (abs_path[mount_len] == '\0') {
        str_copy(out_path, "/", out_size);
        return 0;
    }

    str_copy(out_path, abs_path + mount_len, out_size);
    return 0;
}

static int list_mount_children(const char* abs_path, myaos_dirent_t* out, size_t max_entries, size_t* io_count) {
    size_t count = *io_count;
    size_t base_len = str_len(abs_path);

    for (uint32_t i = 0; i < g_mount_count; i++) {
        const char* mount_path = g_mounts[i].mount_path;
        const char* child_start = NULL;
        char child_name[MYAOS_NAME_MAX];
        uint8_t duplicate = 0;

        if (str_eq(abs_path, mount_path)) {
            continue;
        }

        if (base_len == 1 && abs_path[0] == '/') {
            if (mount_path[0] != '/' || mount_path[1] == '\0') {
                continue;
            }
            child_start = mount_path + 1;
        } else if (path_has_prefix(mount_path, abs_path) && mount_path[base_len] == '/') {
            child_start = mount_path + base_len + 1;
        }

        if (!child_start || child_start[0] == '\0') {
            continue;
        }

        if (copy_prefix(child_name, child_start, 0, str_len(child_start), sizeof(child_name)) != 0) {
            continue;
        }
        for (size_t j = 0; child_name[j]; j++) {
            if (child_name[j] == '/') {
                child_name[j] = '\0';
                break;
            }
        }

        for (size_t j = 0; j < count; j++) {
            if (str_eq_ci(out[j].name, child_name)) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate || count >= max_entries) {
            continue;
        }

        str_copy(out[count].name, child_name, sizeof(out[count].name));
        out[count].type = MYAOS_NODE_MOUNT;
        out[count].size = 0;
        count++;
    }

    *io_count = count;
    return 0;
}

void vfs_init(void) {
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        g_mounts[i].mount_path[0] = '\0';
        g_mounts[i].fs_name[0] = '\0';
        g_mounts[i].source[0] = '\0';
        g_mounts[i].disk_id = MYAOS_INVALID_DISK_ID;
        g_mounts[i].read_only = 0;
        g_mounts[i].ctx = NULL;
        g_mounts[i].ops = NULL;
    }
    for (uint32_t i = 0; i < VFS_MAX_ACL; i++) {
        g_acl[i].used = 0u;
        g_acl[i].is_dir = 0u;
        g_acl[i].mode = 0u;
        g_acl[i].owner_uid = 0u;
        g_acl[i].path[0] = '\0';
    }
    for (uint32_t i = 0; i < VFS_MAX_CACHE; i++) {
        g_cache[i].used = 0u;
        g_cache[i].size = 0u;
        g_cache[i].tick = 0u;
        g_cache[i].path[0] = '\0';
    }
    g_cache_tick = 0u;
    g_mount_count = 0;
}

int vfs_mount(
    const char* mount_path,
    const char* fs_name,
    const char* source,
    uint32_t disk_id,
    uint8_t read_only,
    void* ctx,
    const vfs_ops_t* ops
) {
    char abs_path[MYAOS_PATH_MAX];

    if (!mount_path || !fs_name || !ops || g_mount_count >= VFS_MAX_MOUNTS) {
        return -1;
    }
    if (normalize_absolute_path(mount_path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < g_mount_count; i++) {
        if (str_eq(abs_path, g_mounts[i].mount_path)) {
            return -1;
        }
    }

    str_copy(g_mounts[g_mount_count].mount_path, abs_path, sizeof(g_mounts[g_mount_count].mount_path));
    str_copy(g_mounts[g_mount_count].fs_name, fs_name, sizeof(g_mounts[g_mount_count].fs_name));
    str_copy(g_mounts[g_mount_count].source, source ? source : "", sizeof(g_mounts[g_mount_count].source));
    g_mounts[g_mount_count].disk_id = disk_id;
    g_mounts[g_mount_count].read_only = read_only;
    g_mounts[g_mount_count].ctx = ctx;
    g_mounts[g_mount_count].ops = ops;
    g_mount_count++;
    return 0;
}

int vfs_resolve_cwd(const char* cwd, const char* path, char* out_abs_path, size_t out_size) {
    char tmp[MYAOS_PATH_MAX];

    if (!path || !out_abs_path || out_size == 0) {
        return -1;
    }

    if (path[0] == '/') {
        return normalize_absolute_path(path, out_abs_path, out_size);
    }

    str_copy(tmp, (cwd && cwd[0]) ? cwd : "/", sizeof(tmp));
    if (!str_eq(tmp, "/")) {
        if (append_path_component(tmp, sizeof(tmp), path) != 0) {
            return -1;
        }
    } else {
        if (tmp[1] != '\0') {
            return -1;
        }
        if (append_path_component(tmp, sizeof(tmp), path) != 0) {
            return -1;
        }
    }

    return normalize_absolute_path(tmp, out_abs_path, out_size);
}

int vfs_list(const char* cwd, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count) {
    char abs_path[MYAOS_PATH_MAX];
    char rel_path[MYAOS_PATH_MAX];
    const vfs_mount_t* mount;
    size_t count = 0;

    if (!out_count || !out) {
        return -1;
    }
    if (vfs_resolve_cwd(cwd, path ? path : ".", abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    if (acl_check(abs_path, VFS_PERM_R, 1u, 0u) != 0) {
        return -1;
    }

    mount = resolve_mount(abs_path);
    if (!mount || !mount->ops || !mount->ops->list) {
        return -1;
    }
    if (make_relative_path(mount, abs_path, rel_path, sizeof(rel_path)) != 0) {
        return -1;
    }
    if (mount->ops->list(mount->ctx, rel_path, out, max_entries, &count) != 0) {
        return -1;
    }

    (void)list_mount_children(abs_path, out, max_entries, &count);
    *out_count = count;
    return 0;
}

int vfs_read_file(const char* cwd, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size) {
    char abs_path[MYAOS_PATH_MAX];
    char rel_path[MYAOS_PATH_MAX];
    const vfs_mount_t* mount;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    if (acl_check(abs_path, VFS_PERM_R, 0u, 0u) != 0) {
        return -1;
    }
    if (cache_read(abs_path, out_buf, out_buf_size, out_size) == 0) {
        return 0;
    }

    mount = resolve_mount(abs_path);
    if (!mount || !mount->ops || !mount->ops->read_file) {
        return -1;
    }
    if (make_relative_path(mount, abs_path, rel_path, sizeof(rel_path)) != 0) {
        return -1;
    }
    if (mount->ops->read_file(mount->ctx, rel_path, out_buf, out_buf_size, out_size) != 0) {
        return -1;
    }
    if (out_size && *out_size <= VFS_CACHE_DATA_MAX) {
        cache_store(abs_path, out_buf, *out_size);
    }
    return 0;
}

int vfs_write_file(const char* cwd, const char* path, const uint8_t* data, uint32_t size) {
    char abs_path[MYAOS_PATH_MAX];
    char rel_path[MYAOS_PATH_MAX];
    const vfs_mount_t* mount;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    (void)acl_ensure(abs_path, 0u, 1u);
    if (acl_check(abs_path, VFS_PERM_W, 0u, 1u) != 0) {
        return -1;
    }
    mount = resolve_mount(abs_path);
    if (!mount || !mount->ops || !mount->ops->write_file || mount->read_only) {
        return -1;
    }
    if (make_relative_path(mount, abs_path, rel_path, sizeof(rel_path)) != 0) {
        return -1;
    }
    if (mount->ops->write_file(mount->ctx, rel_path, data, size) != 0) {
        return -1;
    }
    if (mount->ops->sync) {
        (void)mount->ops->sync(mount->ctx);
    }
    cache_invalidate(abs_path);
    if (size <= VFS_CACHE_DATA_MAX) {
        cache_store(abs_path, data, size);
    }
    return 0;
}

int vfs_mkdir(const char* cwd, const char* path) {
    char abs_path[MYAOS_PATH_MAX];
    char rel_path[MYAOS_PATH_MAX];
    const vfs_mount_t* mount;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    (void)acl_ensure(abs_path, 1u, 1u);
    if (acl_check(abs_path, VFS_PERM_W, 1u, 1u) != 0) {
        return -1;
    }
    mount = resolve_mount(abs_path);
    if (!mount || !mount->ops || !mount->ops->mkdir || mount->read_only) {
        return -1;
    }
    if (make_relative_path(mount, abs_path, rel_path, sizeof(rel_path)) != 0) {
        return -1;
    }
    if (mount->ops->mkdir(mount->ctx, rel_path) != 0) {
        return -1;
    }
    if (mount->ops->sync) {
        (void)mount->ops->sync(mount->ctx);
    }
    return 0;
}

int vfs_touch(const char* cwd, const char* path) {
    char abs_path[MYAOS_PATH_MAX];
    char rel_path[MYAOS_PATH_MAX];
    const vfs_mount_t* mount;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    (void)acl_ensure(abs_path, 0u, 1u);
    if (acl_check(abs_path, VFS_PERM_W, 0u, 1u) != 0) {
        return -1;
    }
    mount = resolve_mount(abs_path);
    if (!mount || !mount->ops || !mount->ops->touch || mount->read_only) {
        return -1;
    }
    if (make_relative_path(mount, abs_path, rel_path, sizeof(rel_path)) != 0) {
        return -1;
    }
    if (mount->ops->touch(mount->ctx, rel_path) != 0) {
        return -1;
    }
    if (mount->ops->sync) {
        (void)mount->ops->sync(mount->ctx);
    }
    cache_invalidate(abs_path);
    return 0;
}

int vfs_remove(const char* cwd, const char* path) {
    char abs_path[MYAOS_PATH_MAX];
    char rel_path[MYAOS_PATH_MAX];
    const vfs_mount_t* mount;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    if (acl_check(abs_path, VFS_PERM_W, 0u, 0u) != 0) {
        return -1;
    }
    if (str_eq(abs_path, "/")) {
        return -1;
    }

    for (uint32_t i = 0; i < g_mount_count; i++) {
        if (str_eq(abs_path, g_mounts[i].mount_path)) {
            return -1;
        }
    }

    mount = resolve_mount(abs_path);
    if (!mount || !mount->ops || !mount->ops->remove || mount->read_only) {
        return -1;
    }
    if (make_relative_path(mount, abs_path, rel_path, sizeof(rel_path)) != 0) {
        return -1;
    }
    if (mount->ops->remove(mount->ctx, rel_path) != 0) {
        return -1;
    }
    if (mount->ops->sync) {
        (void)mount->ops->sync(mount->ctx);
    }
    cache_invalidate(abs_path);
    acl_remove(abs_path);
    return 0;
}

int vfs_is_dir(const char* cwd, const char* path) {
    char abs_path[MYAOS_PATH_MAX];
    char rel_path[MYAOS_PATH_MAX];
    const vfs_mount_t* mount;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    if (acl_check(abs_path, VFS_PERM_R, 1u, 0u) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < g_mount_count; i++) {
        if (str_eq(abs_path, g_mounts[i].mount_path)) {
            return 1;
        }
    }

    mount = resolve_mount(abs_path);
    if (!mount || !mount->ops || !mount->ops->is_dir) {
        return -1;
    }
    if (make_relative_path(mount, abs_path, rel_path, sizeof(rel_path)) != 0) {
        return -1;
    }
    return mount->ops->is_dir(mount->ctx, rel_path);
}

int vfs_can_exec(const char* cwd, const char* path) {
    char abs_path[MYAOS_PATH_MAX];
    vfs_acl_t* acl;
    uint8_t hdr[4];
    uint32_t got = 0u;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }

    acl = acl_find(abs_path);
    if (acl) {
        return acl_check(abs_path, VFS_PERM_X, 0u, 0u);
    }

    if (vfs_is_dir("/", abs_path) > 0) {
        return -1;
    }
    if (path_has_suffix(abs_path, ".elf")) {
        return 0;
    }

    if (vfs_read_file("/", abs_path, hdr, sizeof(hdr), &got) == 0 &&
        got >= sizeof(hdr) &&
        hdr[0] == 0x7Fu && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F') {
        return 0;
    }

    return acl_check(abs_path, VFS_PERM_X, 0u, 0u);
}

int vfs_chmod(const char* cwd, const char* path, uint16_t mode) {
    char abs_path[MYAOS_PATH_MAX];
    vfs_acl_t* acl;
    uint32_t uid = scheduler_current_uid();
    int is_dir_rc;

    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }

    acl = acl_find(abs_path);
    if (!acl) {
        is_dir_rc = vfs_is_dir("/", abs_path);
        if (is_dir_rc < 0) {
            return -1;
        }
        acl = acl_ensure(abs_path, is_dir_rc > 0 ? 1u : 0u, 0u);
        if (!acl) {
            return -1;
        }
    }

    if (uid != 0u && uid != acl->owner_uid) {
        return -1;
    }

    acl->mode = (uint16_t)(mode & 0777u);
    return 0;
}

int vfs_chown(const char* cwd, const char* path, uint32_t owner_uid) {
    char abs_path[MYAOS_PATH_MAX];
    vfs_acl_t* acl;
    int is_dir_rc;

    if (scheduler_current_uid() != 0u) {
        return -1;
    }
    if (vfs_resolve_cwd(cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }

    acl = acl_find(abs_path);
    if (!acl) {
        is_dir_rc = vfs_is_dir("/", abs_path);
        if (is_dir_rc < 0) {
            return -1;
        }
        acl = acl_ensure(abs_path, is_dir_rc > 0 ? 1u : 0u, 0u);
        if (!acl) {
            return -1;
        }
    }

    acl->owner_uid = owner_uid;
    return 0;
}

int vfs_sync_all(void) {
    for (uint32_t i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].ops && g_mounts[i].ops->sync) {
            int rc = g_mounts[i].ops->sync(g_mounts[i].ctx);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}

int vfs_list_mounts(myaos_mount_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t count;

    if (!out_count || (max_entries != 0 && !out)) {
        return -1;
    }

    count = g_mount_count;
    if (count > max_entries) {
        count = max_entries;
    }

    for (uint32_t i = 0; i < count; i++) {
        str_copy(out[i].path, g_mounts[i].mount_path, sizeof(out[i].path));
        str_copy(out[i].fs_name, g_mounts[i].fs_name, sizeof(out[i].fs_name));
        str_copy(out[i].source, g_mounts[i].source, sizeof(out[i].source));
        out[i].disk_id = g_mounts[i].disk_id;
        out[i].read_only = g_mounts[i].read_only;
    }
    *out_count = count;
    return 0;
}
