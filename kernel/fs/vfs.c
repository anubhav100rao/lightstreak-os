/*
 * kernel/fs/vfs.c — Virtual Filesystem Switch
 *
 * Dispatches all file operations to the mounted filesystem driver.
 * Currently only one filesystem can be mounted at a time (ramfs).
 */

#include "vfs.h"
#include "../kernel.h"

static fs_ops_t *mounted_fs = NULL;

void vfs_mount(fs_ops_t *ops) { mounted_fs = ops; }

int vfs_create(const char *name) {
  if (!mounted_fs || !mounted_fs->create)
    return -1;
  return mounted_fs->create(name);
}

int vfs_open(const char *path) {
  if (!mounted_fs || !mounted_fs->open)
    return -1;
  return mounted_fs->open(path);
}

int32_t vfs_read(int file_idx, void *buf, uint32_t offset, uint32_t len) {
  if (!mounted_fs || !mounted_fs->read)
    return -1;
  return mounted_fs->read(file_idx, buf, offset, len);
}

int32_t vfs_write(int file_idx, const void *buf, uint32_t offset,
                  uint32_t len) {
  if (!mounted_fs || !mounted_fs->write)
    return -1;
  return mounted_fs->write(file_idx, buf, offset, len);
}

int vfs_close(int file_idx) {
  if (!mounted_fs || !mounted_fs->close)
    return -1;
  return mounted_fs->close(file_idx);
}

int vfs_readdir(dirent_t *entries, int max) {
  if (!mounted_fs || !mounted_fs->readdir)
    return -1;
  return mounted_fs->readdir(entries, max);
}

void vfs_init(void) {
  mounted_fs = NULL;
  kprintf("[VFS] Virtual filesystem initialised\n");
}
