/*
 * kernel/fs/ramfs.c — RAM-based filesystem implementation
 *
 * Storage: a flat array of ramfs_file_t entries.  Each entry holds:
 *   - name[64]: null-terminated filename
 *   - data[RAMFS_MAX_FILE_SIZE]: raw file contents
 *   - size: current number of valid bytes in data
 *   - used: 1 if this slot is in use, 0 if free
 *
 * initramfs format (produced by tools/mkramfs):
 *   Header:
 *     uint32_t magic      (0x52414D46 = "RAMF")
 *     uint32_t file_count
 *   Per file:
 *     char     name[64]
 *     uint32_t offset     (from start of image)
 *     uint32_t size
 *   Followed by raw file data at the specified offsets.
 */

#include "ramfs.h"
#include "../kernel.h"
#include "../mm/heap.h"

#define RAMFS_MAX_FILES 64
#define RAMFS_MAX_FILE_SIZE (32 * 1024) /* 32 KB per file max */
#define INITRAMFS_MAGIC 0x52414D46u     /* "RAMF" */

typedef struct {
  char name[VFS_MAX_FILENAME];
  uint8_t data[RAMFS_MAX_FILE_SIZE];
  uint32_t size;
  uint8_t used;
} ramfs_file_t;

static ramfs_file_t files[RAMFS_MAX_FILES];

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static int str_eq(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b)
      return 0;
    a++;
    b++;
  }
  return (*a == *b);
}

static void mem_copy(void *dst, const void *src, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < n; i++)
    d[i] = s[i];
}

static int find_file(const char *name) {
  for (int i = 0; i < RAMFS_MAX_FILES; i++) {
    if (files[i].used && str_eq(files[i].name, name))
      return i;
  }
  return -1;
}

static int find_free_slot(void) {
  for (int i = 0; i < RAMFS_MAX_FILES; i++) {
    if (!files[i].used)
      return i;
  }
  return -1;
}

/* -------------------------------------------------------------------------
 * ramfs operations (implement fs_ops_t)
 * ---------------------------------------------------------------------- */

static int ramfs_create(const char *name) {
  if (find_file(name) >= 0)
    return -1; /* Already exists */
  int idx = find_free_slot();
  if (idx < 0)
    return -1;

  /* Copy name */
  int i;
  for (i = 0; i < VFS_MAX_FILENAME - 1 && name[i]; i++)
    files[idx].name[i] = name[i];
  files[idx].name[i] = '\0';
  files[idx].size = 0;
  files[idx].used = 1;
  return idx;
}

static int ramfs_open(const char *path) {
  /* Skip leading '/' */
  if (*path == '/')
    path++;
  return find_file(path);
}

static int32_t ramfs_read(int file_idx, void *buf, uint32_t offset,
                          uint32_t len) {
  if (file_idx < 0 || file_idx >= RAMFS_MAX_FILES)
    return -1;
  if (!files[file_idx].used)
    return -1;

  ramfs_file_t *f = &files[file_idx];
  if (offset >= f->size)
    return 0; /* EOF */

  uint32_t available = f->size - offset;
  if (len > available)
    len = available;
  mem_copy(buf, f->data + offset, len);
  return (int32_t)len;
}

static int32_t ramfs_write(int file_idx, const void *buf, uint32_t offset,
                           uint32_t len) {
  if (file_idx < 0 || file_idx >= RAMFS_MAX_FILES)
    return -1;
  if (!files[file_idx].used)
    return -1;

  ramfs_file_t *f = &files[file_idx];
  if (offset + len > RAMFS_MAX_FILE_SIZE)
    len = RAMFS_MAX_FILE_SIZE - offset;

  mem_copy(f->data + offset, buf, len);
  if (offset + len > f->size)
    f->size = offset + len;
  return (int32_t)len;
}

static int ramfs_close(int file_idx) {
  (void)file_idx;
  return 0; /* Nothing to do for ramfs */
}

static int ramfs_readdir(dirent_t *entries, int max) {
  int count = 0;
  for (int i = 0; i < RAMFS_MAX_FILES && count < max; i++) {
    if (files[i].used) {
      /* Copy name */
      int j;
      for (j = 0; j < VFS_MAX_FILENAME - 1 && files[i].name[j]; j++)
        entries[count].name[j] = files[i].name[j];
      entries[count].name[j] = '\0';
      entries[count].size = files[i].size;
      count++;
    }
  }
  return count;
}

/* -------------------------------------------------------------------------
 * fs_ops_t interface
 * ---------------------------------------------------------------------- */

static fs_ops_t ramfs_ops = {
    .create = ramfs_create,
    .open = ramfs_open,
    .read = ramfs_read,
    .write = ramfs_write,
    .close = ramfs_close,
    .readdir = ramfs_readdir,
};

fs_ops_t *ramfs_get_ops(void) { return &ramfs_ops; }

/* -------------------------------------------------------------------------
 * ramfs_init — zero all file entries, mount into VFS
 * ---------------------------------------------------------------------- */
void ramfs_init(void) {
  for (int i = 0; i < RAMFS_MAX_FILES; i++) {
    files[i].used = 0;
    files[i].size = 0;
    files[i].name[0] = '\0';
  }
  vfs_mount(&ramfs_ops);
  kprintf("[RAMFS] In-memory filesystem mounted\n");
}

/* -------------------------------------------------------------------------
 * initramfs_load — unpack a GRUB Multiboot module into ramfs
 *
 * Format:
 *   [4B magic][4B count]
 *   For each file: [64B name][4B offset][4B size]
 *   Raw data follows at the specified offsets.
 * ---------------------------------------------------------------------- */
void initramfs_load(uint32_t mod_start, uint32_t mod_size) {
  (void)mod_size;
  uint32_t *header = (uint32_t *)mod_start;

  if (header[0] != INITRAMFS_MAGIC) {
    kprintf("[RAMFS] WARNING: bad initramfs magic 0x%x (expected 0x%x)\n",
            header[0], INITRAMFS_MAGIC);
    return;
  }

  uint32_t file_count = header[1];
  kprintf("[RAMFS] Loading initramfs: %u files\n", file_count);

  /* Entry table starts at offset 8 */
  uint8_t *entry_ptr = (uint8_t *)(mod_start + 8);

  for (uint32_t i = 0; i < file_count; i++) {
    char *name = (char *)entry_ptr;
    uint32_t offset = *(uint32_t *)(entry_ptr + 64);
    uint32_t fsize = *(uint32_t *)(entry_ptr + 68);
    entry_ptr += 72; /* 64 name + 4 offset + 4 size */

    int idx = ramfs_create(name);
    if (idx >= 0 && fsize > 0) {
      uint8_t *src = (uint8_t *)(mod_start + offset);
      uint32_t copy_len = fsize;
      if (copy_len > RAMFS_MAX_FILE_SIZE)
        copy_len = RAMFS_MAX_FILE_SIZE;
      mem_copy(files[idx].data, src, copy_len);
      files[idx].size = copy_len;
    }
    kprintf("  [RAMFS] Loaded: %s (%u bytes)\n", name, fsize);
  }
}
