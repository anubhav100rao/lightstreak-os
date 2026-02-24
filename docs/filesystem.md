# AnubhavOS — Filesystem

AnubhavOS uses a two-layer filesystem architecture: a Virtual Filesystem (VFS)
abstraction layer and a RAM-based filesystem (ramfs) implementation.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Virtual Filesystem (VFS)](#2-virtual-filesystem-vfs)
3. [RAM Filesystem (ramfs)](#3-ram-filesystem-ramfs)
4. [Initramfs Format & Loading](#4-initramfs-format--loading)
5. [File Descriptors & Process I/O](#5-file-descriptors--process-io)
6. [mkramfs — Build Tool](#6-mkramfs--build-tool)
7. [Adding a New File to the OS](#7-adding-a-new-file-to-the-os)

---

## 1. Architecture Overview

```
┌────────────────────────────────────────────────┐
│  Userspace (shell.c)                           │
│    sys_open("hello.txt")  →  fd = 3            │
│    sys_read(fd, buf, 512) →  "Hello from..."   │
│    sys_close(fd)                               │
└─────────────┬──────────────────────────────────┘
              │ int $0x80
              ▼
┌────────────────────────────────────────────────┐
│  Syscall Layer (syscall.c)                     │
│    sys_open()  → vfs_open()                    │
│    sys_read()  → vfs_read()                    │
│    sys_write() → vfs_write()                   │
│    sys_close() → vfs_close()                   │
└─────────────┬──────────────────────────────────┘
              │ vtable dispatch
              ▼
┌────────────────────────────────────────────────┐
│  VFS Layer (vfs.c)                             │
│    mounted_fs.ops->open(name)                  │
│    mounted_fs.ops->read(idx, buf, off, len)    │
└─────────────┬──────────────────────────────────┘
              │ function pointers
              ▼
┌────────────────────────────────────────────────┐
│  ramfs (ramfs.c)                               │
│    64 file slots × 32 KB max each              │
│    Data stored in flat arrays in kernel memory  │
└────────────────────────────────────────────────┘
```

---

## 2. Virtual Filesystem (VFS)

**Source**: `kernel/fs/vfs.c`, `kernel/fs/vfs.h`

### 2.1 Design

The VFS provides a uniform API for file operations, regardless of the
underlying filesystem. It uses a **vtable** pattern (function pointers):

```c
typedef struct {
    int     (*create)(const char *name);
    int     (*open)(const char *name);
    int32_t (*read)(int idx, void *buf, uint32_t offset, uint32_t len);
    int32_t (*write)(int idx, const void *buf, uint32_t offset, uint32_t len);
    void    (*close)(int idx);
    int     (*readdir)(dirent_t *entries, int max);
} fs_ops_t;
```

A single `mounted_fs` global holds the current filesystem:

```c
typedef struct {
    const char *name;       // "ramfs"
    fs_ops_t    ops;        // function pointers to ramfs functions
} mounted_fs_t;

static mounted_fs_t mounted_fs;
```

### 2.2 API Functions

```c
// Mount a filesystem (set the vtable)
void vfs_mount(const char *name, fs_ops_t ops);

// File operations (dispatch to mounted_fs.ops)
int     vfs_create(const char *name);
int     vfs_open(const char *name);
int32_t vfs_read(int idx, void *buf, uint32_t offset, uint32_t len);
int32_t vfs_write(int idx, const void *buf, uint32_t offset, uint32_t len);
void    vfs_close(int idx);
int     vfs_readdir(dirent_t *entries, int max);
```

### 2.3 dirent_t — Directory Entry

```c
typedef struct {
    char     name[64];    // null-terminated filename
    uint32_t size;        // file size in bytes
} dirent_t;
```

This struct is shared between kernel and userspace. The shell's `cmd_ls`
allocates an array of these and passes it via `sys_readdir`.

---

## 3. RAM Filesystem (ramfs)

**Source**: `kernel/fs/ramfs.c`, `kernel/fs/ramfs.h`

### 3.1 Data Structure

The ramfs stores files in flat arrays allocated in the kernel's `.bss` segment:

```c
#define RAMFS_MAX_FILES    64
#define RAMFS_MAX_FILESIZE 32768  // 32 KB per file

typedef struct {
    char     name[64];           // filename
    uint8_t  data[RAMFS_MAX_FILESIZE]; // file contents
    uint32_t size;               // current file size
    uint8_t  used;               // 1 = slot occupied, 0 = empty
} ramfs_file_t;

static ramfs_file_t files[RAMFS_MAX_FILES];  // ~2 MB total
```

### 3.2 Operations

```c
// Create a new empty file
int ramfs_create(const char *name) {
    // Find an empty slot, set name, size=0, used=1
    // Returns file index, or -1 if full
}

// Open a file by name
int ramfs_open(const char *name) {
    // Linear search through files[] for matching name
    // Returns file index, or -1 if not found
}

// Read from a file at offset
int32_t ramfs_read(int idx, void *buf, uint32_t offset, uint32_t len) {
    // Copy min(len, size-offset) bytes from files[idx].data + offset
    // Returns bytes read, 0 at EOF
}

// Write to a file at offset
int32_t ramfs_write(int idx, const void *buf, uint32_t offset, uint32_t len) {
    // Copy bytes into files[idx].data, update size
    // Returns bytes written, -1 if would exceed 32KB
}

// List all files
int ramfs_readdir(dirent_t *entries, int max) {
    // Iterate files[], copy name+size for each used slot
    // Returns count
}
```

### 3.3 Mounting

During boot, `ramfs_init()` registers itself with the VFS:

```c
void ramfs_init(void) {
    // Zero all file slots
    for (int i = 0; i < RAMFS_MAX_FILES; i++) files[i].used = 0;

    // Register with VFS
    fs_ops_t ops = {
        .create  = ramfs_create,
        .open    = ramfs_open,
        .read    = ramfs_read,
        .write   = ramfs_write,
        .close   = ramfs_close,
        .readdir = ramfs_readdir,
    };
    vfs_mount("ramfs", ops);
}
```

---

## 4. Initramfs Format & Loading

### 4.1 RAMF Binary Format

The initramfs image (`build/initramfs.img`) uses a custom binary format called
RAMF (RAM Filesystem):

```
Offset  Size    Field
──────  ────    ─────
0       4       Magic: 'R','A','M','F' (0x464D4152)
4       4       Number of files (uint32_t, little-endian)

For each file:
+0      64      Filename (null-padded)
+64     4       File size in bytes (uint32_t, little-endian)
+68     N       File data (N = file size)
```

### 4.2 Loading Process

During boot, GRUB loads `initramfs.img` as a Multiboot module into physical
memory right after the kernel. `kmain()` retrieves the module location:

```c
if (mbi->flags & (1u << 3) && mbi->mods_count > 0) {
    multiboot_mod_t *mod = (multiboot_mod_t *)mbi->mods_addr;
    uint32_t mod_start = mod->mod_start;
    uint32_t mod_size  = mod->mod_end - mod->mod_start;
    initramfs_load(mod_start, mod_size);
}
```

`initramfs_load` parses the RAMF header and unpacks each file into the ramfs:

```c
void initramfs_load(uint32_t start_addr, uint32_t size) {
    uint8_t *data = (uint8_t *)start_addr;

    // Verify magic
    if (data[0] != 'R' || data[1] != 'A' ||
        data[2] != 'M' || data[3] != 'F') {
        kprintf("[RAMFS] WARNING: bad initramfs magic\n");
        return;
    }

    uint32_t num_files = *(uint32_t *)(data + 4);
    uint32_t offset = 8;

    for (uint32_t i = 0; i < num_files; i++) {
        char *name = (char *)(data + offset);       // 64 bytes
        uint32_t fsize = *(uint32_t *)(data + offset + 64);
        uint8_t *fdata = data + offset + 68;

        // Create file in ramfs and write data
        ramfs_create(name);
        int idx = ramfs_open(name);
        if (idx >= 0) {
            ramfs_write(idx, fdata, 0, fsize);
        }
        offset += 68 + fsize;
    }
}
```

> **Important**: The heap must be placed AFTER the GRUB module in physical
> memory. See `kernel.c` for the `safe_end` calculation that prevents
> `heap_init()` from overwriting the module data.

### 4.3 GRUB Configuration

```cfg
# boot/grub.cfg
menuentry "AnubhavOS" {
    multiboot /boot/kernel.elf
    module    /boot/initramfs.img
    boot
}
```

GRUB loads both files into RAM and sets up the Multiboot info structure.

---

## 5. File Descriptors & Process I/O

Each process has a file descriptor table with `MAX_FDS = 16` slots:

```c
typedef struct {
    int      file_idx;   // ramfs file index (-1 for stdio)
    uint32_t offset;     // current read/write position
    uint8_t  in_use;     // 1 = open, 0 = closed
} fd_entry_t;

// In process_t:
fd_entry_t fd_table[MAX_FDS];
```

**Reserved file descriptors**:

| fd | Purpose | Backing |
|----|---------|---------|
| 0  | stdin   | `keyboard_getchar()` (blocking) |
| 1  | stdout  | `vga_putchar()` (VGA text buffer) |
| 2  | stderr  | `vga_putchar()` (same as stdout) |
| 3+ | files   | ramfs via VFS |

When `sys_open` is called, it finds the first unused fd slot ≥ 3 and maps it
to the ramfs file index. `sys_read` and `sys_write` check the fd number: 0/1/2
go to keyboard/VGA directly; 3+ go through the VFS.

---

## 6. mkramfs — Build Tool

**Source**: `tools/mkramfs.c`

`mkramfs` is a **host-side** tool (compiled with the system compiler, not the
cross-compiler) that packs a directory into a RAMF image:

```bash
# Usage:
build/mkramfs <input_directory> <output_file>

# Example (done automatically by make):
build/mkramfs build/initramfs_root build/initramfs.img
```

The Makefile copies `build/shell.bin` and any other files into
`build/initramfs_root/`, then runs mkramfs to create the image.

---

## 7. Adding a New File to the OS

### Method 1: Add a static file to the initramfs

1. Create the file in `build/initramfs_root/`:
   ```bash
   echo "My config data" > build/initramfs_root/config.txt
   ```

2. Rebuild:
   ```bash
   make clean && make
   ```

3. Access in the shell:
   ```
   anubhav-os:/ $ ls
     config.txt  (15 bytes)
     hello.txt   (22 bytes)
     shell.bin   (4722 bytes)
   anubhav-os:/ $ cat config.txt
   My config data
   ```

### Method 2: Create a file programmatically in the kernel

```c
// In kernel.c, during initialisation:
vfs_create("status.txt");
int idx = vfs_open("status.txt");
if (idx >= 0) {
    const char *msg = "System OK\n";
    vfs_write(idx, msg, 0, strlen(msg));
    vfs_close(idx);
}
```

### Limitations

| Limitation | Value |
|-----------|-------|
| Max files | 64 |
| Max file size | 32 KB |
| Max filename length | 63 characters |
| Directories | Not supported (flat namespace) |
| Persistence | RAM only — files lost on reboot |
| File permissions | None (all files readable/writable) |
