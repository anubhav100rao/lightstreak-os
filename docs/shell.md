# AnubhavOS — Shell Guide

The AnubhavOS shell is a minimal interactive REPL (Read-Eval-Print Loop) that
runs as the first userspace program. It demonstrates syscall usage, keyboard
input handling, and VGA output — all without any C standard library.

**Source**: `userspace/shell/shell.c`

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [How the Shell Starts](#2-how-the-shell-starts)
3. [Command Reference](#3-command-reference)
4. [readline — Input Handling](#4-readline--input-handling)
5. [How Commands Are Dispatched](#5-how-commands-are-dispatched)
6. [Adding a New Command](#6-adding-a-new-command)
7. [Limitations](#7-limitations)

---

## 1. Architecture Overview

```
┌────────────────────────────────────────────────────────────┐
│  shell.c (Userspace)                                       │
│                                                            │
│  _start()     ← entry point (called by crt0.asm _entry)   │
│    │                                                       │
│    ├── print ASCII banner                                  │
│    └── loop:                                               │
│          print_prompt()   → sys_write(1, "anubhav-os...$") │
│          readline()       → sys_read(0, &c, 1)  (blocking) │
│          dispatch command → cmd_help / cmd_ls / ...        │
│                                                            │
│  Dependencies:                                             │
│    syscall_wrappers.h  — inline int $0x80 wrappers         │
│    string.h/c          — strlen, strcmp, memset, utoa       │
│                                                            │
│  NO libc, NO kernel headers, NO malloc                     │
└────────────────────────────────────────────────────────────┘
           │ int $0x80
           ▼
┌────────────────────────────────────────────────────────────┐
│  Kernel (Ring 0)                                           │
│    syscall_handler() dispatches to:                        │
│      sys_write(fd=1) → vga_putchar()   (screen output)    │
│      sys_read(fd=0)  → keyboard_getchar() (keyboard input)│
│      sys_open/read/close → VFS/ramfs   (file I/O)         │
└────────────────────────────────────────────────────────────┘
```

---

## 2. How the Shell Starts

### Step 1: Binary Loading

During boot, `kmain()` calls `exec("shell.bin")`, which:

1. Opens `shell.bin` from the ramfs (loaded from `initramfs.img`)
2. Reads the flat binary into a temp buffer via `kmalloc()`
3. Copies it to physical address `0x600000` (identity-mapped)
4. The entry point is `_entry` in `crt0.asm`, which calls `_start`

### Step 2: Entry Point

```asm
; userspace/lib/crt0.asm
global _entry
extern _start

_entry:
    call _start       ; call the C entry point
    ; if _start returns, spin forever
    jmp $
```

### Step 3: _start() in shell.c

```c
void _start(void) {
    // Print ASCII art banner
    print("  ___                _                ___  ___\n");
    // ... more banner lines ...
    print("Welcome to AnubhavOS! Type 'help' for commands.\n\n");

    char cmd[CMD_BUF_SIZE];     // 256-byte command buffer

    for (;;) {
        print_prompt();                 // "anubhav-os:/ $ "
        int len = readline(cmd, CMD_BUF_SIZE);
        if (len == 0) continue;         // empty enter
        // dispatch command...
    }
}
```

---

## 3. Command Reference

### `help` — Show available commands

```
anubhav-os:/ $ help
AnubhavOS Shell Commands:
  help     — show this help
  ls       — list files in ramfs
  cat FILE — display file contents
  echo ... — print arguments
  clear    — clear the screen
  uptime   — seconds since boot
  meminfo  — physical memory usage
  ps       — list running processes
```

**Implementation**: Simply prints hardcoded help text via `sys_write`.

---

### `ls` — List files in ramfs

```
anubhav-os:/ $ ls
  hello.txt  (22 bytes)
  shell.bin  (4722 bytes)
```

**Implementation**:

```c
static void cmd_ls(void) {
    user_dirent_t entries[32];
    int count = sys_readdir(entries, 32);   // syscall 7
    for (int i = 0; i < count; i++) {
        print("  ");
        print(entries[i].name);
        print("  (");
        print_num(entries[i].size);
        print(" bytes)\n");
    }
}
```

**Syscall flow**: `sys_readdir` → `int $0x80` → kernel `vfs_readdir()` → ramfs
iterates all non-empty file entries and fills the `dirent_t` array.

---

### `cat <filename>` — Display file contents

```
anubhav-os:/ $ cat hello.txt
Hello from AnubhavOS!
```

**Implementation**:

```c
static void cmd_cat(const char *filename) {
    int fd = sys_open(filename);         // open file → fd 3+
    if (fd < 0) {
        print("cat: file not found: ");
        print(filename);
        return;
    }
    char buf[512];
    int n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        sys_write(1, buf, (size_t)n);    // write to stdout
    }
    sys_close(fd);
}
```

**Syscall flow**: `sys_open` → kernel allocates fd 3, maps to ramfs file index.
Then `sys_read(fd=3)` reads from ramfs buffer. `sys_close` frees the fd slot.

---

### `echo <args>` — Print text

```
anubhav-os:/ $ echo Hello World
Hello World
```

**Implementation** (the simplest command):

```c
static void cmd_echo(const char *args) {
    if (args && *args) {
        print(args);        // print everything after "echo "
    }
    print("\n");
}
```

The shell command parser passes `cmd + 5` (skipping `"echo "`) to `cmd_echo`.
Bare `echo` with no arguments just prints a newline.

---

### `uptime` — Seconds since boot

```
anubhav-os:/ $ uptime
Uptime: 42 seconds
```

**Syscall flow**: `sys_uptime()` → kernel divides PIT tick counter by frequency
(100 Hz) → returns seconds as `uint32_t`.

---

### `meminfo` — Physical memory statistics

```
anubhav-os:/ $ meminfo
Physical Memory:
  Total: 32768 KB
  Free:  31520 KB
  Used:  1248 KB
```

**Syscall flow**: `sys_meminfo(&info)` → kernel queries PMM bitmap for page
counts → fills `meminfo_t` struct → userspace multiplies by 4 for KB.

---

### `ps` — Process list

```
anubhav-os:/ $ ps
PID  STATE
  0   READY
  2   RUNNING
```

Shows all processes in the scheduler's circular linked list.

**Process states**: `0=READY`, `1=RUNNING`, `2=BLOCKED`, `3=ZOMBIE`.

---

### `clear` — Clear the screen

Writes 80×25 spaces to fill the VGA text buffer, then sends `\r` to reset the
cursor position.

---

### `exit` — Terminate the shell

```
anubhav-os:/ $ exit
Goodbye!
```

Calls `sys_exit(0)` which marks the process as ZOMBIE and halts.

---

## 4. readline — Input Handling

The `readline` function provides line editing with echo and backspace support:

```c
static int readline(char *buf, int max) {
    int pos = 0;
    while (pos < max - 1) {
        char c;
        int n = sys_read(0, &c, 1);    // block until keypress
        if (n <= 0) continue;

        if (c == '\n' || c == '\r') {   // Enter → submit line
            buf[pos] = '\0';
            print("\n");
            return pos;
        }
        if (c == '\b' || c == 127) {    // Backspace → erase char
            if (pos > 0) {
                pos--;
                sys_write(1, "\b \b", 3);  // erase on screen
            }
            continue;
        }
        buf[pos++] = c;
        sys_write(1, &c, 1);           // echo character
    }
    buf[pos] = '\0';
    return pos;
}
```

**Key features**:

- **Blocking read**: `sys_read(0, &c, 1)` blocks until a key is pressed.
  Inside the kernel, `keyboard_getchar()` does `sti; hlt` in a loop, waking
  only when keyboard IRQ1 fires and adds a character to the ring buffer.

- **Echo**: Each character is immediately written back to stdout so the user
  sees what they type.

- **Backspace**: Moves the cursor back, writes a space (to erase), then moves
  back again. The VGA driver handles `\b` as cursor-left.

- **Buffer limit**: Maximum 255 characters per line (`CMD_BUF_SIZE - 1`).

---

## 5. How Commands Are Dispatched

```c
// In _start()
if (strcmp(cmd, "help") == 0) {
    cmd_help();
} else if (strcmp(cmd, "ls") == 0) {
    cmd_ls();
} else if (strncmp(cmd, "cat ", 4) == 0) {
    cmd_cat(cmd + 4);               // pass filename after "cat "
} else if (strncmp(cmd, "echo ", 5) == 0) {
    cmd_echo(cmd + 5);              // pass text after "echo "
} else if (strcmp(cmd, "echo") == 0) {
    cmd_echo("");                   // bare echo → just newline
} else if (strcmp(cmd, "clear") == 0) {
    cmd_clear();
} else if (strcmp(cmd, "uptime") == 0) {
    cmd_uptime();
} else if (strcmp(cmd, "meminfo") == 0) {
    cmd_meminfo();
} else if (strcmp(cmd, "ps") == 0) {
    cmd_ps();
} else if (strcmp(cmd, "exit") == 0) {
    print("Goodbye!\n");
    sys_exit(0);
} else {
    print("Unknown command: ");
    print(cmd);
    print("\nType 'help' for a list of commands.\n");
}
```

Commands with arguments (like `cat` and `echo`) use `strncmp` to match the
prefix and pass the remainder of the string to the handler.

---

## 6. Adding a New Command

Here's a step-by-step guide to add a new `whoami` command:

### Step 1: Write the handler function

```c
// In userspace/shell/shell.c, before _start()
static void cmd_whoami(void) {
    uint32_t pid = sys_getpid();
    print("You are process ");
    print_num(pid);
    print("\n");
}
```

### Step 2: Add to the dispatcher

```c
// In _start(), add before the "else" fallback:
} else if (strcmp(cmd, "whoami") == 0) {
    cmd_whoami();
} else {
```

### Step 3: Update help text

```c
static void cmd_help(void) {
    // ... existing entries ...
    print("  whoami   — show current process ID\n");
}
```

### Step 4: Build and test

```bash
make clean && make && make run
```

---

## 7. Limitations

| Limitation | Reason |
|-----------|--------|
| No command history (arrow keys) | Arrow keys generate multi-byte escape sequences; the keyboard driver only handles single-byte scancodes |
| No tab completion | Would require directory listing + prefix matching logic |
| No piping or redirection | No pipe syscall or fd duplication implemented |
| No background processes | Single foreground process model |
| No line editing (left/right arrow) | Would need cursor position tracking separate from buffer position |
| 255 character line limit | Fixed buffer size |
| No environment variables | `echo $PATH` just prints `$PATH` literally |
