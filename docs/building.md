# AnubhavOS — Building & Running

Complete guide to building, running, and debugging AnubhavOS.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Building](#2-building)
3. [Running in QEMU](#3-running-in-qemu)
4. [Build System Details](#4-build-system-details)
5. [Debugging with GDB](#5-debugging-with-gdb)
6. [Project Structure](#6-project-structure)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. Prerequisites

### Required Tools

| Tool | Purpose | Install (macOS) |
|------|---------|----------------|
| `i686-elf-gcc` | Cross-compiler for 32-bit x86 | `brew install i686-elf-gcc` |
| `i686-elf-ld` | Cross-linker | Included with i686-elf-gcc |
| `nasm` | Assembler for x86 assembly | `brew install nasm` |
| `i686-elf-grub-mkrescue` | ISO image creator | `brew install i686-elf-grub` |
| `qemu-system-i386` | x86 emulator | `brew install qemu` |
| `xorriso` | ISO 9660 utility (used by grub-mkrescue) | `brew install xorriso` |

### Verify Installation

```bash
i686-elf-gcc --version     # Should show i686-elf-gcc 13.x or similar
nasm --version              # Should show NASM 2.x
qemu-system-i386 --version  # Should show QEMU 8.x or similar
```

---

## 2. Building

### Full Build

```bash
cd ~/development/anubhav-os
make clean && make
```

### Expected Output

```
[CLEAN] Done
i686-elf-gcc ... -c kernel/proc/process.c -o kernel/proc/process.o
i686-elf-gcc ... -c kernel/proc/exec.c -o kernel/proc/exec.o
... (18 kernel .c files compiled)
nasm -f elf32 boot/boot.asm -o boot/boot.asm.o
nasm -f elf32 kernel/proc/context_switch.asm -o kernel/proc/context_switch.asm.o
nasm -f elf32 kernel/arch/gdt.asm -o kernel/arch/gdt.asm.o
nasm -f elf32 kernel/arch/idt.asm -o kernel/arch/idt.asm.o
[LD] Linked build/kernel.elf
[HOSTCC] Built build/mkramfs
nasm -f elf32 userspace/lib/crt0.asm -o userspace/lib/crt0.asm.o
i686-elf-gcc ... -c userspace/shell/shell.c -o userspace/shell/shell.o
i686-elf-gcc ... -c userspace/lib/string.c -o userspace/lib/string.o
[LD] Linked build/shell.elf
[OBJCOPY] Created build/shell.bin
[INITRAMFS] Created build/initramfs.img
[ISO] Created build/anubhav-os.iso
```

### Build Artifacts

| File | Size | Description |
|------|------|-------------|
| `build/kernel.elf` | ~60 KB | Kernel with debug symbols |
| `build/shell.elf` | ~8 KB | Shell ELF (before stripping) |
| `build/shell.bin` | ~5 KB | Shell flat binary |
| `build/initramfs.img` | ~5 KB | RAM filesystem image |
| `build/mkramfs` | ~20 KB | Host tool for packing ramfs |
| `build/anubhav-os.iso` | ~5 MB | Bootable ISO image |

---

## 3. Running in QEMU

### Standard Run

```bash
make run
```

This runs:
```bash
qemu-system-i386 \
    -cdrom build/anubhav-os.iso \
    -m 32M \
    -debugcon stdio \
    -no-reboot \
    -no-shutdown
```

| Flag | Purpose |
|------|---------|
| `-cdrom` | Boot from ISO image |
| `-m 32M` | 32 MB of RAM |
| `-debugcon stdio` | Serial port output → host terminal |
| `-no-reboot` | Don't reboot on triple fault (show error instead) |
| `-no-shutdown` | Keep window open after shutdown |

### What You'll See

**QEMU window** (VGA output):
```
AnubhavOS booting...
Multiboot magic OK (0x2badb002)
[GDT/TSS/IDT] Loaded. Interrupts enabled.
[PMM] Total: 32 MB  Free: 31 MB  Used by kernel: 1248 KB
... more boot messages ...

  ___                _                ___  ___
 / _ \              | |               |  \/  |
/ /_\ \_ __  _   _| |__   __ ___   _| .  . |
...
Welcome to AnubhavOS! Type 'help' for commands.

anubhav-os:/ $
```

**Host terminal** (serial debug log):
```
[serial] AnubhavOS booting...
[serial] Multiboot OK
[serial] GDT loaded
[serial] TSS loaded
[serial] IDT+IRQ loaded
[serial] PMM ready
[serial] Paging enabled
[serial] Heap ready
[serial] Scheduler running
[serial] Syscall init
[serial] VFS test OK
[serial] Keyboard ready
```

---

## 4. Build System Details

### Makefile Targets

| Target | Command | Description |
|--------|---------|-------------|
| `make` | (default) | Build everything |
| `make run` | Build + launch QEMU |
| `make debug` | Build + launch QEMU with GDB server |
| `make clean` | Remove all build artifacts |

### Compiler Flags

```makefile
CFLAGS = -std=c99           # C99 standard
         -ffreestanding      # No standard library
         -fno-builtin        # Don't substitute builtin functions
         -fno-stack-protector # No stack canaries (no libc to support them)
         -O2                 # Optimisation level 2
         -Wall -Wextra       # All warnings
         -Iinclude           # Header search path
```

### Build Pipeline

```
1. Kernel C files   → i686-elf-gcc → .o files
2. Kernel ASM files → nasm -f elf32 → .asm.o files
3. All .o files     → i686-elf-ld -T linker.ld → build/kernel.elf

4. mkramfs.c        → cc (host compiler) → build/mkramfs

5. Userspace C/ASM  → cross-compile → .o files
6. Link             → i686-elf-ld -T linker_user.ld → build/shell.elf
7. Strip            → i686-elf-objcopy -O binary → build/shell.bin

8. Pack initramfs   → build/mkramfs → build/initramfs.img

9. Create ISO       → grub-mkrescue → build/anubhav-os.iso
```

### Linker Scripts

**Kernel** (`linker.ld`):
```ld
ENTRY(boot_start)     /* boot.asm entry point */
SECTIONS {
    . = 0x100000;     /* load at 1 MB (standard for Multiboot) */
    .multiboot : { *(.multiboot) }    /* GRUB header (must be in first 8KB) */
    .text      : { *(.text) }
    .rodata    : { *(.rodata) }
    .data      : { *(.data) }
    .bss       : { *(.bss) *(COMMON) }
}
/* Exports: kernel_start, kernel_end, stack_top */
```

**Userspace** (`linker_user.ld`):
```ld
ENTRY(_entry)         /* crt0.asm entry point */
SECTIONS {
    . = 0x600000;     /* load at 6 MB (above kernel heap) */
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) *(COMMON) }
}
```

---

## 5. Debugging with GDB

### Start Debug Session

```bash
# Terminal 1: Start QEMU with GDB server (paused)
make debug
# QEMU starts but halts, waiting for GDB connection

# Terminal 2: Attach GDB
i686-elf-gdb build/kernel.elf \
    -ex 'target remote localhost:1234'
```

### Useful GDB Commands

```gdb
# Breakpoints
break kmain                   # Break at kernel entry
break syscall_handler         # Break on any syscall
break keyboard_irq_handler    # Break on keypress
break exec                    # Break when shell is loaded

# Execution
continue                      # Resume execution
stepi                         # Single-step one instruction
next                          # Step over function call
finish                        # Run until function returns

# Inspection
info registers                # Show all CPU registers
print/x $eax                  # Print EAX in hex
x/10x 0x600000               # Examine 10 hex words at address
x/s 0xb8000                  # Show string at VGA buffer
info break                    # List breakpoints

# Memory
x/10i $eip                   # Disassemble 10 instructions at EIP
print/x *(uint32_t*)0x600000 # Read 4 bytes from address
```

### Example Debug Session

```gdb
(gdb) break kmain
(gdb) continue
Breakpoint 1, kmain (magic=0x2badb002, mbi=0x10000) at kernel/kernel.c:160

(gdb) break syscall_handler
(gdb) continue
Breakpoint 2, syscall_handler (regs=0x...) at kernel/syscall/syscall.c:209
(gdb) print/x regs->eax
$1 = 0x2                      # SYS_WRITE
(gdb) print/x regs->ebx
$2 = 0x1                      # fd = 1 (stdout)
(gdb) print/d regs->edx
$3 = 5                        # len = 5 bytes
(gdb) x/s regs->ecx
0x600123: "hello"             # buffer contents
```

---

## 6. Project Structure

```
anubhav-os/
├── boot/
│   ├── boot.asm            # Multiboot header, stack setup, call kmain
│   └── grub.cfg            # GRUB menu configuration
│
├── include/
│   ├── types.h             # uint8/16/32_t, NULL, PACKED (no stdlib)
│   └── multiboot.h         # Multiboot info structures
│
├── kernel/
│   ├── kernel.c            # kmain(), kprintf(), debug_print()
│   ├── kernel.h            # Shared kernel declarations
│   │
│   ├── arch/               # CPU architecture (x86)
│   │   ├── io.h            # inb/outb port I/O
│   │   ├── gdt.c + gdt.asm # Global Descriptor Table
│   │   ├── tss.c           # Task State Segment
│   │   ├── idt.c + idt.asm # Interrupt Descriptor Table + stubs
│   │   ├── isr.c           # Exception handler
│   │   └── irq.c           # PIC + IRQ dispatch
│   │
│   ├── mm/                 # Memory management
│   │   ├── pmm.c           # Physical page bitmap allocator
│   │   ├── vmm.c           # x86 two-level paging
│   │   └── heap.c          # kmalloc/kfree free-list
│   │
│   ├── drivers/            # Hardware drivers
│   │   ├── vga.c           # 80×25 text mode display
│   │   ├── timer.c         # PIT (Programmable Interval Timer)
│   │   └── keyboard.c      # PS/2 keyboard
│   │
│   ├── proc/               # Process management
│   │   ├── process.c       # PCB allocation
│   │   ├── scheduler.c     # Round-robin scheduler
│   │   ├── context_switch.asm # Register save/restore
│   │   └── exec.c          # Binary loader
│   │
│   ├── fs/                 # Filesystem
│   │   ├── vfs.c           # Virtual filesystem layer
│   │   └── ramfs.c         # In-memory filesystem
│   │
│   └── syscall/            # System calls
│       ├── syscall_table.h # Syscall number definitions
│       └── syscall.c       # int 0x80 dispatcher
│
├── userspace/
│   ├── lib/
│   │   ├── crt0.asm        # C runtime start (_entry → _start)
│   │   ├── syscall_wrappers.h # Inline asm int $0x80 wrappers
│   │   └── string.c/h      # String functions (no libc)
│   └── shell/
│       └── shell.c         # Interactive shell (8 commands)
│
├── tools/
│   └── mkramfs.c           # Host tool: pack directory → RAMF image
│
├── linker.ld               # Kernel linker script (0x100000)
├── linker_user.ld          # Userspace linker script (0x600000)
├── Makefile                # Build system
│
└── docs/                   # Documentation (you are here!)
    ├── architecture.md     # System architecture overview
    ├── boot.md             # Boot sequence details
    ├── gdt-tss-idt.md      # GDT, TSS, IDT internals
    ├── memory-management.md # PMM, VMM, heap
    ├── drivers.md          # VGA, timer, keyboard
    ├── filesystem.md       # VFS, ramfs, initramfs
    ├── syscalls.md         # System call reference
    ├── shell.md            # Shell guide
    ├── building.md         # Building & running (this file)
    └── testing.md          # Test plan
```

---

## 7. Troubleshooting

### Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `i686-elf-gcc: command not found` | Cross-compiler not installed | `brew install i686-elf-gcc` |
| `nasm: command not found` | Assembler not installed | `brew install nasm` |
| `grub-mkrescue: command not found` | GRUB tools not installed | `brew install i686-elf-grub` |
| `Circular dependency dropped` | Makefile ASM rule quirk | Harmless warning, ignore |
| `undefined reference to ...` | Missing source file | Check Makefile C_SRCS/ASM_SRCS |
| `LOAD segment with RWX permissions` | Linker warning | Harmless for hobby OS |

### Runtime Errors

| Symptom | Possible Cause | Debug Steps |
|---------|---------------|-------------|
| QEMU window closes immediately | Triple fault during boot | Use `make debug`, set breakpoint at `kmain` |
| Red `KERNEL EXCEPTION` text | CPU exception triggered | Note exception number and EIP in dump |
| `bad initramfs magic 0x0` | Heap overwrites GRUB module | Check `kernel.c` heap placement code |
| No shell prompt | Shell binary not loaded | Check serial log for `[EXEC]` messages |
| Keyboard doesn't respond | IRQ1 not registered | Verify `keyboard_init()` is called |
| `ls` shows no files | Initramfs not loaded | Check `grub.cfg` has `module /boot/initramfs.img` |
| Characters garbled/doubled | Scheduler interfering with keyboard | Ensure `scheduler_tick` not registered as timer handler |
| Screen frozen after boot | Infinite loop with interrupts disabled | Check for missing `sti` after `cli` points |
