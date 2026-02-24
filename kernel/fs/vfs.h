#ifndef FS_VFS_H
#define FS_VFS_H

/*
 * kernel/fs/vfs.h — Virtual Filesystem Switch
 *
 * Provides a uniform interface for filesystem operations.
 * The VFS dispatches open/read/write/close/readdir to whichever
 * concrete filesystem is mounted (currently only ramfs).
 */

#include "../../include/types.h"

#define VFS_MAX_FILENAME 64
#define VFS_MAX_FILES    64

/* Directory entry returned by readdir */
typedef struct {
    char     name[VFS_MAX_FILENAME];
    uint32_t size;
} dirent_t;

/* Filesystem operations — every FS driver implements these */
typedef struct {
    int      (*create)(const char *name);
    int      (*open)(const char *path);
    int32_t  (*read)(int file_idx, void *buf, uint32_t offset, uint32_t len);
    int32_t  (*write)(int file_idx, const void *buf, uint32_t offset, uint32_t len);
    int      (*close)(int file_idx);
    int      (*readdir)(dirent_t *entries, int max);
} fs_ops_t;

/* Mount a filesystem implementation */
void vfs_mount(fs_ops_t *ops);

/* VFS public API */
int      vfs_create(const char *name);
int      vfs_open(const char *path);
int32_t  vfs_read(int file_idx, void *buf, uint32_t offset, uint32_t len);
int32_t  vfs_write(int file_idx, const void *buf, uint32_t offset, uint32_t len);
int      vfs_close(int file_idx);
int      vfs_readdir(dirent_t *entries, int max);

/* Initialise VFS */
void vfs_init(void);

#endif /* FS_VFS_H */
