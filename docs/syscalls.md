# AnubhavOS — System Call Reference

AnubhavOS provides 10 system calls accessed via `int $0x80`. Arguments are passed
in x86 registers following the Linux-like convention.

---

## Calling Convention

| Register | Purpose |
|----------|---------|
| `EAX`    | Syscall number (1–10) |
| `EBX`    | Argument 1 |
| `ECX`    | Argument 2 |
| `EDX`    | Argument 3 |
| `EAX`    | Return value (after `iret`) |

**How it works under the hood:**

```
Userspace                         Kernel
─────────                         ──────
mov eax, 2        ─────────►   isr128 (idt.asm)
mov ebx, 1                         │
mov ecx, buf                       ▼
mov edx, len                   isr_common_stub
int $0x80         ─────────►       │ pusha
                                   │ set kernel segment 0x10
                                   ▼
                               isr_handler(registers_t *regs)
                                   │ if (regs->int_no == 0x80)
                                   ▼
                               syscall_handler(regs)
                                   │ switch (regs->eax)
                                   │ case SYS_WRITE: ...
                                   │ regs->eax = return_value
                                   ▼
                               popa + iret ─────────► EAX = return_value
```

---

## Syscall Table

Defined in `kernel/syscall/syscall_table.h`:

```c
#define SYS_EXIT     1    // Terminate process
#define SYS_WRITE    2    // Write to file descriptor
#define SYS_READ     3    // Read from file descriptor
#define SYS_OPEN     4    // Open a file
#define SYS_CLOSE    5    // Close a file descriptor
#define SYS_GETPID   6    // Get current process ID
#define SYS_READDIR  7    // List directory entries
#define SYS_UPTIME   8    // Get seconds since boot
#define SYS_MEMINFO  9    // Get memory statistics
#define SYS_PS      10    // List running processes
```

---

## Syscall Details

### 1. `SYS_EXIT` — Terminate the current process

```
EAX = 1 (SYS_EXIT)
EBX = exit code (int)
Returns: Does not return
```

**Kernel implementation** (`kernel/syscall/syscall.c`):

```c
static void sys_exit(int32_t code) {
    process_t *cur = scheduler_current();
    if (cur) {
        cur->state = PROC_ZOMBIE;
        cur->exit_code = code;
    }
    scheduler_tick((registers_t *)0);  // force context switch
    khalt();                           // should never reach here
}
```

**Userspace wrapper** (`userspace/lib/syscall_wrappers.h`):

```c
static inline void sys_exit(int code) {
    __asm__ volatile("int $0x80" :: "a"(SYS_EXIT), "b"(code));
}
```

> **Design note**: The process is marked as ZOMBIE but NOT removed from the
> scheduler's circular list. `scheduler_remove()` sets `p->next = NULL`, which
> would cause a NULL dereference in the scheduler's round-robin loop. The
> scheduler simply skips ZOMBIE processes.

---

### 2. `SYS_WRITE` — Write bytes to a file descriptor

```
EAX = 2 (SYS_WRITE)
EBX = file descriptor (uint32_t)
ECX = pointer to buffer (const char *)
EDX = number of bytes to write (uint32_t)
Returns: EAX = number of bytes written, or -1 on error
```

**Kernel implementation**:

```c
static int32_t sys_write(uint32_t fd, const char *buf, uint32_t len) {
    if (fd == 1 || fd == 2) {
        // stdout / stderr → write directly to VGA text buffer
        for (uint32_t i = 0; i < len; i++) {
            vga_putchar(buf[i]);
        }
        return (int32_t)len;
    }
    // Regular file fd → VFS write
    process_t *cur = scheduler_current();
    if (!cur || fd >= MAX_FDS || !cur->fd_table[fd].in_use) return -1;
    int file_idx = cur->fd_table[fd].file_idx;
    int32_t written = vfs_write(file_idx, buf, cur->fd_table[fd].offset, len);
    if (written > 0) cur->fd_table[fd].offset += (uint32_t)written;
    return written;
}
```

**Userspace wrapper**:

```c
static inline int sys_write(int fd, const void *buf, size_t len) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}
```

**Example usage in shell**:

```c
// Print a string to stdout
static void print(const char *s) {
    sys_write(1, s, strlen(s));  // fd=1 is stdout → goes to VGA
}
```

---

### 3. `SYS_READ` — Read bytes from a file descriptor

```
EAX = 3 (SYS_READ)
EBX = file descriptor (uint32_t)
ECX = pointer to buffer (char *)
EDX = number of bytes to read (uint32_t)
Returns: EAX = number of bytes read, or -1 on error
```

**Kernel implementation**:

```c
static int32_t sys_read(uint32_t fd, char *buf, uint32_t len) {
    if (fd == 0) {
        // stdin → keyboard (blocks until data available)
        for (uint32_t i = 0; i < len; i++) {
            buf[i] = keyboard_getchar();  // blocks with sti;hlt
        }
        return (int32_t)len;
    }
    // Regular file fd → VFS read
    process_t *cur = scheduler_current();
    int file_idx = cur->fd_table[fd].file_idx;
    int32_t bytes = vfs_read(file_idx, buf, cur->fd_table[fd].offset, len);
    if (bytes > 0) cur->fd_table[fd].offset += (uint32_t)bytes;
    return bytes;
}
```

**Example — reading a single keypress**:

```c
char c;
int n = sys_read(0, &c, 1);  // fd=0 → blocks until key pressed
if (n > 0) {
    sys_write(1, &c, 1);     // echo back to screen
}
```

**Example — reading a file**:

```c
int fd = sys_open("hello.txt");
char buf[512];
int n;
while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
    sys_write(1, buf, (size_t)n);  // print file contents
}
sys_close(fd);
```

---

### 4. `SYS_OPEN` — Open a file

```
EAX = 4 (SYS_OPEN)
EBX = pointer to filename string (const char *)
Returns: EAX = file descriptor (≥ 3), or -1 if not found
```

File descriptors 0, 1, 2 are reserved for stdin, stdout, stderr. Opened files
start at fd 3. Maximum 16 open file descriptors per process (`MAX_FDS`).

**Example**:

```c
int fd = sys_open("hello.txt");
if (fd < 0) {
    print("File not found!\n");
} else {
    // read/write using fd...
    sys_close(fd);
}
```

---

### 5. `SYS_CLOSE` — Close a file descriptor

```
EAX = 5 (SYS_CLOSE)
EBX = file descriptor (uint32_t)
Returns: EAX = 0 on success, -1 on error
```

Cannot close fd 0, 1, or 2 (stdio). Closing an already-closed fd returns -1.

---

### 6. `SYS_GETPID` — Get process ID

```
EAX = 6 (SYS_GETPID)
Returns: EAX = current process PID (uint32_t)
```

**Example**:

```c
uint32_t pid = sys_getpid();
print("My PID is: ");
print_num(pid);
print("\n");
```

---

### 7. `SYS_READDIR` — List directory entries

```
EAX = 7 (SYS_READDIR)
EBX = pointer to array of dirent_t (dirent_t *)
ECX = max entries to return (int)
Returns: EAX = number of entries, or -1 on error
```

**dirent_t structure** (must match between kernel and userspace):

```c
typedef struct {
    char     name[64];   // null-terminated filename
    uint32_t size;       // file size in bytes
} dirent_t;
```

**Example — listing files** (`cmd_ls` in shell):

```c
static void cmd_ls(void) {
    user_dirent_t entries[32];
    int count = sys_readdir(entries, 32);
    for (int i = 0; i < count; i++) {
        print("  ");
        print(entries[i].name);
        print("  (");
        print_num(entries[i].size);
        print(" bytes)\n");
    }
}
```

---

### 8. `SYS_UPTIME` — Seconds since boot

```
EAX = 8 (SYS_UPTIME)
Returns: EAX = seconds since boot (uint32_t)
```

The PIT timer ticks at 100 Hz. The kernel divides the tick counter by the
frequency to compute seconds.

---

### 9. `SYS_MEMINFO` — Physical memory statistics

```
EAX = 9 (SYS_MEMINFO)
EBX = pointer to meminfo_t struct
Returns: EAX = 0 on success, -1 on error
```

**meminfo_t structure**:

```c
typedef struct {
    uint32_t total_pages;   // total 4KB pages in system
    uint32_t free_pages;    // unallocated pages
    uint32_t used_pages;    // allocated pages
} meminfo_t;
```

**Example**:

```c
meminfo_t info;
sys_meminfo(&info);
// Total memory = info.total_pages * 4 KB
// Free  memory = info.free_pages  * 4 KB
```

---

### 10. `SYS_PS` — List running processes

```
EAX = 10 (SYS_PS)
EBX = pointer to array of ps_entry_t
ECX = max entries (int)
Returns: EAX = number of processes, or -1 on error
```

**ps_entry_t structure**:

```c
typedef struct {
    uint32_t pid;
    uint32_t state;   // 0=READY, 1=RUNNING, 2=BLOCKED, 3=ZOMBIE
} ps_entry_t;
```

---

## Writing Your Own Syscall Wrapper

To add a new syscall wrapper for userspace, follow this pattern:

```c
// In userspace/lib/syscall_wrappers.h

// For a syscall that takes 2 args and returns int:
static inline int sys_my_new_call(int arg1, int arg2) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)             // output: EAX → ret
        : "a"(SYS_MY_CALL),    // input: syscall number → EAX
          "b"(arg1),            // input: arg1 → EBX
          "c"(arg2)             // input: arg2 → ECX
        : "memory");
    return ret;
}
```

### Inline Assembly Syntax Guide

```c
__asm__ volatile("int $0x80"
    : "=a"(ret)                    // OUTPUTS  (section 1)
    : "a"(2), "b"(fd), "c"(buf)   // INPUTS   (section 2)
    : "memory"                     // CLOBBERS (section 3)
);
```

| Section | Separator | Purpose |
|---------|-----------|---------|
| Assembly | (none)  | The instruction to execute |
| Outputs  | `:`     | What the asm produces — `"=a"` means write-only EAX |
| Inputs   | `:`     | What goes in — `"a"(val)` loads `val` into EAX |
| Clobbers | `:`     | Registers/memory the asm might change |

If a section is empty, leave it blank: `:: "a"(1)` means "no outputs, one input".

**Register constraint letters**:

| Letter | Register | Letter | Register |
|--------|----------|--------|----------|
| `a`    | EAX      | `S`    | ESI      |
| `b`    | EBX      | `D`    | EDI      |
| `c`    | ECX      | `r`    | any GPR  |
| `d`    | EDX      |        |          |
