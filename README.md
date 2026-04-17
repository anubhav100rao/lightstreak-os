# AnubhavOS

A from-scratch, educational **32-bit x86 operating system** written in C and NASM assembly. Boots via GRUB, runs an interactive shell with syscalls, keyboard input, filesystem, and VGA text-mode display — all with **zero external dependencies** (no libc, no standard library).

```
  ___                _                ___  ___
 / _ \              | |               |  \/  |
/ /_\ \_ __  _   _| |__   __ ___   _| .  . |
|  _  | '_ \| | | | '_ \ / _` \ \ / / |\/| |
| | | | | | | |_| | |_) | (_| |\ V /| |  | |
\_| |_/_| |_|\__,_|_.__/ \__,_| \_/ \_|  |_/

Welcome to AnubhavOS! Type 'help' for commands.

anubhav-os:/ $
```

---

## ✨ Features

| Category | What's Implemented |
|----------|-------------------|
| **Boot** | Multiboot-compliant boot via GRUB, 16KB kernel stack, Multiboot info parsing |
| **CPU** | GDT (6 entries), TSS, IDT (256 gates), Ring 0/Ring 3 privilege separation |
| **Memory** | Physical page allocator (bitmap), two-level x86 paging, kernel heap (first-fit free-list) |
| **Interrupts** | PIC remapping (IRQ → INT 32-47), ISR stubs for all 32 CPU exceptions, IRQ handlers |
| **Drivers** | VGA text mode (80×25, colors, scrolling, cursor), PIT timer (100 Hz), PS/2 keyboard (scancode set 1, shift support) |
| **Filesystem** | VFS abstraction layer, in-memory ramfs (64 files × 32KB), custom initramfs format |
| **Processes** | PCB structures, round-robin scheduler, context switching |
| **Syscalls** | 10 system calls via `int $0x80` (read, write, open, close, exit, getpid, readdir, uptime, meminfo, ps) |
| **Shell** | Interactive REPL with 8 commands: `help`, `ls`, `cat`, `echo`, `clear`, `uptime`, `meminfo`, `ps` |
| **Tools** | `mkramfs` — host-side tool to pack directories into initramfs images |

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────────────┐
│  USERSPACE                                       │
│  ┌────────────────────────────────────────────┐  │
│  │  shell.bin — interactive REPL at 0x600000  │  │
│  │  syscall_wrappers.h + string.c (no libc)   │  │
│  └────────────────────┬───────────────────────┘  │
│                       │ int $0x80                 │
├───────────────────────┼──────────────────────────┤
│  KERNEL (Ring 0)      ▼                          │
│  ┌──────┐ ┌────────┐ ┌───────┐ ┌─────────────┐  │
│  │ arch │ │   mm   │ │ proc  │ │   drivers   │  │
│  │ gdt  │ │ pmm    │ │ sched │ │ vga (0xB8000│  │
│  │ idt  │ │ vmm    │ │ exec  │ │ timer (PIT) │  │
│  │ isr  │ │ heap   │ │ ctx_sw│ │ keyboard    │  │
│  │ irq  │ └────────┘ └───────┘ └─────────────┘  │
│  │ tss  │ ┌────────┐ ┌───────┐                   │
│  └──────┘ │   fs   │ │syscall│                   │
│           │ vfs    │ │ 10    │                   │
│           │ ramfs  │ │ calls │                   │
│           └────────┘ └───────┘                   │
├──────────────────────────────────────────────────┤
│  HARDWARE: x86 CPU, PIC, PIT, PS/2, VGA, RAM    │
└──────────────────────────────────────────────────┘
```

---

## 🚀 Quick Start

### Prerequisites

```bash
# macOS (Homebrew)
brew install i686-elf-gcc nasm qemu xorriso i686-elf-grub
```

### Build & Run

```bash
git clone https://github.com/anubhav100rao/lightstreak-os.git
cd lightstreak-os
make clean && make
make run
```

This opens a **QEMU window** with the OS and prints **serial debug output** in your terminal.

### Shell Commands

Once you see the `anubhav-os:/ $` prompt:

```bash
help                 # List available commands
ls                   # List files in ramfs
cat hello.txt        # Display file contents
echo Hello World     # Print text
uptime               # Seconds since boot
meminfo              # Physical memory usage
ps                   # List running processes
clear                # Clear screen
```

---

## 📦 Project Structure

```
anubhav-os/
├── boot/                    # Multiboot entry + GRUB config
│   ├── boot.asm             # Multiboot header, stack setup, call kmain
│   └── grub.cfg             # GRUB menu configuration
│
├── kernel/                  # Kernel source code
│   ├── kernel.c             # kmain() entry point, kprintf
│   ├── arch/                # x86 architecture (GDT, IDT, ISR, IRQ, TSS)
│   ├── mm/                  # Memory management (PMM, VMM, heap)
│   ├── drivers/             # VGA, timer, keyboard
│   ├── proc/                # Processes, scheduler, context switch
│   ├── fs/                  # VFS + ramfs
│   └── syscall/             # System call dispatcher
│
├── userspace/               # Userspace programs
│   ├── lib/                 # crt0.asm, syscall wrappers, string functions
│   └── shell/               # Interactive shell
│
├── tools/                   # Build tools
│   └── mkramfs.c            # Packs directory → initramfs image
│
├── include/                 # Shared headers (types, multiboot)
├── docs/                    # Detailed documentation
├── linker.ld                # Kernel linker script (loads at 0x100000)
├── linker_user.ld           # Userspace linker script (loads at 0x600000)
└── Makefile                 # Build system
```

---

## 🔧 Build System

| Command | Description |
|---------|-------------|
| `make` | Build everything (kernel + shell + initramfs + ISO) |
| `make run` | Build and launch in QEMU |
| `make debug` | Build and launch with GDB server (paused) |
| `make clean` | Remove all build artifacts |

### Build Pipeline

```
Kernel .c/.asm → i686-elf-gcc/nasm → .o files → i686-elf-ld → kernel.elf
Shell  .c/.asm → cross-compile     → .o files → link → shell.elf → objcopy → shell.bin
mkramfs.c      → host cc           → mkramfs  → pack shell.bin → initramfs.img
grub-mkrescue  → kernel.elf + initramfs.img + grub.cfg → anubhav-os.iso
```

---

## 🧠 How It Works

### Boot Sequence

```
BIOS → GRUB → boot.asm (stack setup) → kmain()
  → GDT/TSS/IDT → PMM → Paging → Heap
  → Scheduler → Syscalls → VFS/ramfs
  → Keyboard → Load initramfs → exec("shell.bin")
  → Shell prompt!
```

### System Call Flow

```
Shell: sys_write(1, "hello", 5)
  → EAX=2, EBX=1, ECX=buf, EDX=5
  → int $0x80
  → CPU traps to kernel
  → isr_handler → syscall_handler
  → vga_putchar('h','e','l','l','o')
  → iret back to userspace
```

### Memory Layout

```
0x000000  ┌─────────────────┐
          │ Reserved (BIOS)  │
0x100000  ├─────────────────┤  ← kernel loads here (1 MB)
          │ Kernel code/data │
          ├─────────────────┤
          │ GRUB module      │  ← initramfs.img
          ├─────────────────┤
          │ Kernel heap      │  2 MB
          ├─────────────────┤
0x600000  │ Shell binary     │  ← userspace code
          ├─────────────────┤
          │ Free pages (PMM) │
0xB8000   │ VGA text buffer  │  ← 80×25×2 bytes (memory-mapped)
          └─────────────────┘
```

---

## 📚 Documentation

Detailed documentation lives in the [`docs/`](docs/) directory:

| Document | Contents |
|----------|----------|
| [architecture.md](docs/architecture.md) | System architecture, memory maps, privilege rings, boot sequence |
| [current-project-overview.md](docs/current-project-overview.md) | Best current onboarding doc: scope, architecture, workflows, capabilities, and runtime caveats |
| [boot.md](docs/boot.md) | Detailed boot process from BIOS to kmain |
| [gdt-tss-idt.md](docs/gdt-tss-idt.md) | GDT segments, TSS, IDT gates deep dive |
| [memory-management.md](docs/memory-management.md) | PMM bitmap allocator, VMM paging, heap internals |
| [drivers.md](docs/drivers.md) | VGA, PIT timer, PS/2 keyboard, PIC hardware details |
| [filesystem.md](docs/filesystem.md) | VFS design, ramfs, initramfs format, mkramfs tool |
| [syscalls.md](docs/syscalls.md) | All 10 system calls with code examples and inline asm guide |
| [shell.md](docs/shell.md) | Shell commands, readline, how to add new commands |
| [building.md](docs/building.md) | Prerequisites, build pipeline, GDB debugging guide |
| [testing.md](docs/testing.md) | Step-by-step manual testing plan |

---

## 🔬 Debugging

```bash
# Terminal 1: Start QEMU with GDB server
make debug

# Terminal 2: Attach GDB
i686-elf-gdb build/kernel.elf -ex 'target remote localhost:1234'

# Useful breakpoints
(gdb) break kmain
(gdb) break syscall_handler
(gdb) break keyboard_irq_handler
(gdb) continue
```

---

## 🎓 Educational Purpose

This OS was built to understand how operating systems work at the lowest level:

- **No abstractions** — every byte matters, from GDT entries to VGA attributes
- **No standard library** — `strlen`, `memcpy`, `kprintf` all written from scratch
- **Real hardware interfaces** — PIC, PIT, PS/2 via I/O ports, VGA via memory-mapped I/O
- **x86 privilege model** — Ring 0 kernel, Ring 3 userspace, interrupt gates, TSS

### Key Concepts Demonstrated

1. **Protected Mode** — GDT segments, privilege levels, segment selectors
2. **Interrupts** — IDT, ISR stubs, PIC remapping, IRQ handling
3. **Virtual Memory** — Two-level page tables, identity mapping, CR3 switching
4. **System Calls** — `int $0x80` trap, register-based argument passing, `iret` return
5. **I/O** — Port-mapped I/O (`inb`/`outb`) and memory-mapped I/O (VGA at 0xB8000)
6. **Process Management** — PCBs, context switching, round-robin scheduling
7. **Filesystem** — VFS vtable pattern, block-less RAM filesystem, custom binary format

---

## 📝 License

This project is for educational purposes. Feel free to study, modify, and learn from it.

---

*Built with ❤️ as a learning exercise in operating system development.*
