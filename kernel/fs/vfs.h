#ifndef VFS_H
#define VFS_H

#include <myaos/syscall.h>
#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_MOUNTS 16u

typedef struct {
    int (*list)(void* ctx, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count);
    int (*read_file)(void* ctx, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size);
    int (*write_file)(void* ctx, const char* path, const uint8_t* data, uint32_t size);
    int (*mkdir)(void* ctx, const char* path);
    int (*touch)(void* ctx, const char* path);
    int (*remove)(void* ctx, const char* path);
    int (*is_dir)(void* ctx, const char* path);
    int (*sync)(void* ctx);
} vfs_ops_t;

void vfs_init(void);
int vfs_mount(
    const char* mount_path,
    const char* fs_name,
    const char* source,
    uint32_t disk_id,
    uint8_t read_only,
    void* ctx,
    const vfs_ops_t* ops
);
int vfs_list(const char* cwd, const char* path, myaos_dirent_t* out, size_t max_entries, size_t* out_count);
int vfs_read_file(const char* cwd, const char* path, uint8_t* out_buf, uint32_t out_buf_size, uint32_t* out_size);
int vfs_write_file(const char* cwd, const char* path, const uint8_t* data, uint32_t size);
int vfs_mkdir(const char* cwd, const char* path);
int vfs_touch(const char* cwd, const char* path);
int vfs_remove(const char* cwd, const char* path);
int vfs_is_dir(const char* cwd, const char* path);
int vfs_can_exec(const char* cwd, const char* path);
int vfs_chmod(const char* cwd, const char* path, uint16_t mode);
int vfs_chown(const char* cwd, const char* path, uint32_t owner_uid);
int vfs_resolve_cwd(const char* cwd, const char* path, char* out_abs_path, size_t out_size);
int vfs_sync_all(void);
int vfs_list_mounts(myaos_mount_info_t* out, uint32_t max_entries, uint32_t* out_count);

#endif
