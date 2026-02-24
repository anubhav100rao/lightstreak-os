#ifndef FS_RAMFS_H
#define FS_RAMFS_H

/*
 * kernel/fs/ramfs.h — RAM-based filesystem
 *
 * Simple flat filesystem stored entirely in RAM.
 * Files are fixed-size entries with name, data buffer, and size.
 * No subdirectories — all files live in a single flat namespace.
 */

#include "vfs.h"

/* Initialise ramfs and register with VFS */
void ramfs_init(void);

/* Load an initramfs image (packed by tools/mkramfs) into ramfs.
 * Called from kmain() after GRUB loads the Multiboot module. */
void initramfs_load(uint32_t mod_start, uint32_t mod_size);

/* Get the ramfs ops struct (for direct testing before VFS is wired) */
fs_ops_t *ramfs_get_ops(void);

#endif /* FS_RAMFS_H */
