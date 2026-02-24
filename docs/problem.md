# AnubhavOS — Lightweight Hobby Kernel: Project Blueprint

> A from-scratch, educational x86 hobby operating system built in C and Assembly,
> designed to teach every fundamental layer of how a modern OS works.
> Target platform: macOS (Apple Silicon or Intel) with QEMU emulation.

---

## Table of Contents

1. [Project Vision](#1-project-vision)
2. [High-Level Architecture](#2-high-level-architecture)
3. [Tech Stack & Tools](#3-tech-stack--tools)
4. [Repository Structure](#4-repository-structure)
5. [Development Environment Setup](#5-development-environment-setup)
6. [Build System](#6-build-system)
7. [Stage-by-Stage Implementation Plan](#7-stage-by-stage-implementation-plan)
   - [Stage 1 — Bootloader](#stage-1--bootloader)
   - [Stage 2 — Kernel Entry & VGA Output](#stage-2--kernel-entry--vga-output)
   - [Stage 3 — GDT & IDT (Interrupts)](#stage-3--gdt--idt-interrupts)
   - [Stage 4 — Memory Management](#stage-4--memory-management)
   - [Stage 5 — Multitasking & Scheduler](#stage-5--multitasking--scheduler)
   - [Stage 6 — System Calls](#stage-6--system-calls)
   - [Stage 7 — Filesystem (ramfs)](#stage-7--filesystem-ramfs)
   - [Stage 8 — Userspace & Shell](#stage-8--userspace--shell)
8. [Debugging Strategy](#8-debugging-strategy)
9. [Testing Strategy](#9-testing-strategy)
10. [Key Concepts Glossary](#10-key-concepts-glossary)
11. [Reference Resources](#11-reference-resources)

---

## 1. Project Vision

**AnubhavOS** is a minimal, UNIX-inspired hobby kernel built entirely from scratch. The goal is not to build a production OS, but to deeply understand every abstraction that modern operating systems provide — by implementing each one yourself.

### What it will support at completion:
- Boots from a GRUB-generated ISO image on QEMU
- Protected mode 32-bit x86 kernel written in C
- Physical and virtual memory management with paging
- Preemptive multitasking with a round-robin scheduler
- Keyboard and timer hardware interrupt handling
- A system call interface (similar to Linux syscalls)
- A simple in-memory filesystem (ramfs/initrd)
- A minimal userspace shell that can run basic commands

### What it will NOT do (intentionally out of scope):
- Network stack
- Graphics / GUI
- USB support
- SMP (multi-core)
- 64-bit long mode (we stay in 32-bit protected mode for simplicity)

---

## 2. High-Level Architecture

```
+---------------------------------------------------------------+
|                        USER SPACE                             |
|   ┌─────────────────────────────────────────────────────┐    |
|   │  Shell (sh)  │  User Programs  │  C Standard Lib    │    |
|   └──────────────────────┬──────────────────────────────┘    |
|                          │  System Call Interface (int 0x80)  |
+--------------------------|------------------------------------+
|                     KERNEL SPACE                              |
|   ┌───────────────────────────────────────────────────────┐  |
|   │                   System Call Handler                  │  |
|   ├─────────────┬──────────────┬──────────────────────────┤  |
|   │  VFS Layer  │  Scheduler   │  Memory Manager           │  |
|   ├─────────────┴──────────────┴──────────────────────────┤  |
|   │                   Interrupt Manager (IDT)              │  |
|   ├────────────────────────────────────────────────────────  |
|   │  Drivers: Timer (PIT) │ Keyboard (PS/2) │ VGA Text    │  |
|   └───────────────────────────────────────────────────────┘  |
|                                                               |
|   ┌───────────────────────────────────────────────────────┐  |
|   │          Hardware Abstraction (GDT, IDT, CR registers) │  |
|   └───────────────────────────────────────────────────────┘  |
+---------------------------------------------------------------+
|                      HARDWARE (emulated by QEMU)              |
|        x86 CPU  │  RAM  │  Keyboard  │  VGA  │  Disk         |
+---------------------------------------------------------------+
```

---

## 3. Tech Stack & Tools

### Core Languages

| Language | Purpose |
|---|---|
| **x86 Assembly (NASM syntax)** | Bootloader, interrupt stubs, context switch low-level code |
| **C (C99)** | Everything else — kernel, drivers, memory manager, scheduler |

### Toolchain

| Tool | Purpose | Install |
|---|---|---|
| `i686-elf-gcc` | Cross-compiler — compiles C to 32-bit ELF for bare metal | Built from source (see setup) |
| `i686-elf-ld` | Cross-linker — links ELF objects with our linker script | Comes with cross-compiler |
| `nasm` | Assembler for `.asm` files | `brew install nasm` |
| `make` | Build system | Pre-installed on macOS (or `brew install make`) |
| `qemu-system-i386` | x86 emulator to run our OS | `brew install qemu` |
| `gdb` or `lldb` | Debugger, connects to QEMU | `brew install gdb` or use lldb |
| `grub-mkrescue` / `xorriso` | Creates bootable ISO with GRUB | `brew install xorriso` + Docker for grub tools |
| `mtools` | Required by grub-mkrescue | `brew install mtools` |

### Why these choices?

- **NASM over GAS** — NASM uses Intel syntax which is much more readable for learning. AT&T syntax (GAS) is harder to follow when you're learning.
- **32-bit protected mode over 64-bit** — Long mode (64-bit) has more setup ceremony. 32-bit protected mode gives you all the core OS concepts with less boilerplate.
- **GRUB as bootloader** — Writing a full bootloader from scratch is a project in itself. We use GRUB to handle the BIOS/UEFI complexity and focus on the kernel. (We still write a minimal boot stub to understand what GRUB does.)
- **QEMU over real hardware** — Instant boot, easy reset, GDB integration, no hardware risk.

---

## 4. Repository Structure

```
anubhav-os/
│
├── boot/
│   ├── boot.asm              # Multiboot header + kernel entry point
│   └── grub.cfg              # GRUB bootloader config
│
├── kernel/
│   ├── kernel.c              # kmain() — first C function called
│   ├── kernel.h
│   │
│   ├── arch/                 # Architecture-specific (x86)
│   │   ├── gdt.c             # Global Descriptor Table + TSS descriptor
│   │   ├── gdt.h
│   │   ├── gdt.asm           # GDT flush (lgdt instruction)
│   │   ├── tss.c             # Task State Segment setup + esp0 update
│   │   ├── tss.h
│   │   ├── idt.c             # Interrupt Descriptor Table
│   │   ├── idt.h
│   │   ├── idt.asm           # ISR/IRQ stubs in assembly
│   │   ├── isr.c             # Interrupt service routines (exceptions)
│   │   └── irq.c             # Hardware interrupt handlers
│   │
│   ├── mm/                   # Memory Management
│   │   ├── pmm.c             # Physical Memory Manager (bitmap allocator)
│   │   ├── pmm.h
│   │   ├── vmm.c             # Virtual Memory Manager (paging)
│   │   ├── vmm.h
│   │   ├── heap.c            # Kernel heap (kmalloc / kfree)
│   │   └── heap.h
│   │
│   ├── proc/                 # Process Management
│   │   ├── process.c         # Process creation, PCB structure
│   │   ├── process.h
│   │   ├── exec.c            # Loads flat binary from ramfs into new address space
│   │   ├── exec.h
│   │   ├── scheduler.c       # Round-robin scheduler (updates TSS esp0 on switch)
│   │   ├── scheduler.h
│   │   └── context_switch.asm # Low-level register save/restore
│   │
│   ├── fs/                   # Filesystem
│   │   ├── vfs.c             # Virtual Filesystem Switch abstraction
│   │   ├── vfs.h
│   │   ├── ramfs.c           # RAM-based filesystem implementation
│   │   └── ramfs.h
│   │
│   ├── drivers/              # Hardware Drivers
│   │   ├── vga.c             # VGA text mode (80x25)
│   │   ├── vga.h
│   │   ├── keyboard.c        # PS/2 keyboard driver
│   │   ├── keyboard.h
│   │   ├── timer.c           # PIT (Programmable Interval Timer)
│   │   └── timer.h
│   │
│   └── syscall/              # System Calls
│       ├── syscall.c         # Syscall dispatcher
│       ├── syscall.h
│       └── syscall_table.h   # Syscall number definitions
│
├── userspace/
│   ├── lib/
│   │   ├── syscall_wrappers.c  # Thin wrappers over int 0x80
│   │   └── string.c            # Basic string functions (no stdlib)
│   └── shell/
│       └── shell.c             # Minimal interactive shell
│
├── tools/
│   └── mkramfs.c             # Host-side tool: packs a directory into initramfs.img
│
├── include/                  # Shared headers
│   ├── types.h               # uint8_t, uint32_t etc. (no stdlib)
│   ├── stdint.h
│   └── stddef.h
│
├── linker.ld                 # Linker script — kernel memory layout (loads at 0x100000)
├── linker_user.ld            # Linker script — userspace programs (loads at 0x400000)
├── Makefile                  # Build system
└── README.md
```

---

## 5. Development Environment Setup

### Step 1 — Install Homebrew Dependencies

```bash
brew install nasm qemu xorriso mtools gmp mpfr libmpc
```

### Step 2 — Build the i686-elf Cross-Compiler

This is a one-time setup. It builds GCC and binutils to target `i686-elf` (bare-metal x86 32-bit).

```bash
# Create a working directory
mkdir -p ~/cross-compiler && cd ~/cross-compiler

# Set environment variables
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

# Download sources (check for latest stable versions)
# binutils ~2.41, gcc ~13.x
wget https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.gz
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz

tar xf binutils-2.41.tar.gz
tar xf gcc-13.2.0.tar.gz

# Build binutils
mkdir build-binutils && cd build-binutils
../binutils-2.41/configure --target=$TARGET --prefix=$PREFIX \
    --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
make install
cd ..

# Build GCC (C only, no C++)
mkdir build-gcc && cd build-gcc
../gcc-13.2.0/configure --target=$TARGET --prefix=$PREFIX \
    --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc
make -j$(nproc) all-target-libgcc
make install-gcc
make install-target-libgcc
```

Add to your `~/.zshrc`:
```bash
export PATH="$HOME/opt/cross/bin:$PATH"
```

### Step 3 — Handle GRUB on macOS (Docker workaround)

`grub-mkrescue` is not available natively on macOS. Use Docker:

```bash
# Pull a Linux image with grub tools
docker pull ubuntu:22.04

# Create a helper script: make-iso.sh
# (We'll invoke this from the Makefile automatically)
```

### Step 4 — Verify Setup

```bash
i686-elf-gcc --version    # Should print GCC version
nasm --version            # Should print NASM version
qemu-system-i386 --version # Should print QEMU version
```

---

## 6. Build System

### Makefile Overview

```makefile
# Makefile

CC      = i686-elf-gcc
AS      = nasm
LD      = i686-elf-ld

CFLAGS  = -std=c99 -ffreestanding -O2 -Wall -Wextra -Iinclude
ASFLAGS = -f elf32
LDFLAGS = -T linker.ld -nostdlib

# Source files
C_SOURCES  := $(shell find kernel -name '*.c') $(shell find userspace -name '*.c')
ASM_SOURCES:= boot/boot.asm $(shell find kernel -name '*.asm')

OBJECTS    := $(C_SOURCES:.c=.o) $(ASM_SOURCES:.asm=.o)
KERNEL     := build/kernel.elf
ISO        := build/anubhav-os.iso

.PHONY: all clean run debug iso

all: $(ISO)

$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

iso: $(KERNEL)
	mkdir -p build/iso/boot/grub
	cp $(KERNEL) build/iso/boot/kernel.elf
	cp boot/grub.cfg build/iso/boot/grub/grub.cfg
	docker run --rm -v $(PWD)/build:/build ubuntu:22.04 \
	    grub-mkrescue -o /build/anubhav-os.iso /build/iso

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 32M

debug: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 32M -s -S &
	gdb $(KERNEL) -ex "target remote localhost:1234"

clean:
	find . -name '*.o' -delete
	rm -rf build/
```

### Linker Script (`linker.ld`)

```ld
ENTRY(boot_start)

SECTIONS {
    . = 0x100000;       /* Load kernel at 1MB mark (standard) */

    .text : {
        *(.multiboot)   /* Multiboot header must come first */
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(COMMON)
        *(.bss)
    }
}
```

---

## 7. Stage-by-Stage Implementation Plan

---

### Stage 1 — Bootloader

**Goal:** Get the CPU to jump into your code. See "Hello" printed on screen from bare metal.

**Concepts learned:**
- How a PC boots (BIOS → MBR → Bootloader → Kernel)
- The Multiboot specification (how GRUB hands off to your kernel)
- x86 real mode vs protected mode
- Memory map at boot time
- Sections in assembly code

**Key files:** `boot/boot.asm`, `boot/grub.cfg`

**What to implement:**
- A Multiboot-compliant header in assembly so GRUB can identify and load your kernel
- A minimal boot stub that sets up the stack and calls your `kmain()` C function
- GRUB config that points to your kernel ELF

**Multiboot Modules — loading the initramfs alongside the kernel:**

GRUB supports passing extra files to the kernel at boot time called **Multiboot modules**. We use this to load a small **initramfs image** — a flat binary blob containing the shell executable and any initial files — directly into RAM before the kernel even starts. GRUB places the module at a known physical address and records its location in the Multiboot info struct (`mbi->mods_addr`). Your kernel reads that address in `kmain()` and hands it to the ramfs layer to unpack. This means by the time the shell needs to run, its binary is already sitting in memory — no disk driver required.

```c
// In kmain(), after memory manager is up:
multiboot_module_t *mod = (multiboot_module_t *)mbi->mods_addr;
initramfs_load(mod->mod_start, mod->mod_end - mod->mod_start);
// ramfs is now populated with files from the initramfs image
```

The initramfs image itself is a simple custom format: a header listing filenames and offsets, followed by raw file data. You'll write a tiny host-side tool (`tools/mkramfs.c`) that takes a directory on your Mac and packs it into this binary format at build time.

**boot/boot.asm sketch:**
```nasm
; Multiboot constants
MBOOT_MAGIC    equ 0x1BADB002
MBOOT_ALIGN    equ 1 << 0
MBOOT_MEMINFO  equ 1 << 1
MBOOT_FLAGS    equ MBOOT_ALIGN | MBOOT_MEMINFO
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384          ; 16KB stack
stack_top:

section .text
global boot_start
extern kmain

boot_start:
    mov esp, stack_top  ; Set up stack pointer
    push ebx            ; Pass multiboot info struct to kmain
    push eax            ; Pass multiboot magic number
    call kmain
    hlt                 ; Should never reach here
```

**Milestone:** QEMU boots, GRUB menu appears, your kernel loads without triple-faulting.

---

### Stage 2 — Kernel Entry & VGA Output

**Goal:** Your C `kmain()` runs and prints text to the screen.

**Concepts learned:**
- VGA text mode memory-mapped I/O (0xB8000)
- How characters are rendered in text mode (character + attribute byte)
- Freestanding C (no stdlib, no OS beneath you)
- Basic I/O port access (`inb`/`outb`)

**Key files:** `kernel/kernel.c`, `kernel/drivers/vga.c`

**What to implement:**
- `vga_init()` — clears the screen
- `vga_putchar(char c)` — writes a character at current cursor position
- `vga_puts(char *str)` — writes a string
- `kprintf(char *fmt, ...)` — a minimal printf (handle `%s`, `%d`, `%x` at least)
- Scrolling when the screen is full

**VGA memory layout:**
```c
// VGA text buffer is at physical address 0xB8000
// Each cell is 2 bytes: [character][color attribute]
// 80 columns × 25 rows = 2000 cells

#define VGA_ADDRESS 0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25

typedef enum {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_WHITE = 15,
    VGA_COLOR_GREEN = 2,
    // ...
} vga_color_t;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}
```

**Milestone:** `kmain()` prints "AnubhavOS booting..." in green text on a black screen.

---

### Stage 3 — GDT & IDT (Interrupts)

**Goal:** Set up the CPU's descriptor tables so it can handle exceptions and hardware interrupts.

**Concepts learned:**
- x86 memory segmentation model
- CPU privilege rings (Ring 0 = kernel, Ring 3 = user)
- How the CPU handles exceptions (divide by zero, page fault, etc.)
- Hardware interrupts (IRQs) and the Programmable Interrupt Controller (PIC 8259A)
- Interrupt Service Routines (ISRs)

**Key files:** `kernel/arch/gdt.*`, `kernel/arch/idt.*`, `kernel/arch/isr.c`, `kernel/arch/irq.c`

**GDT — Global Descriptor Table:**
The GDT defines memory segments. In protected mode you need at minimum:
- Segment 0: Null descriptor (required)
- Segment 1: Kernel code segment (ring 0, executable)
- Segment 2: Kernel data segment (ring 0, writable)
- Segment 3: User code segment (ring 3) — for later
- Segment 4: User data segment (ring 3) — for later
- Segment 5: TSS descriptor — explained below

**TSS — Task State Segment:**
The TSS is a special GDT entry that the CPU uses when switching privilege levels. When a Ring 3 (user) program triggers a syscall via `int 0x80`, the CPU needs to know which stack to switch to for kernel mode — it reads this from the TSS. Without a valid TSS, the moment any userspace code triggers an interrupt, the CPU will triple-fault.

You only need one TSS for the entire kernel. Its most important fields are `ss0` (kernel stack segment, always `0x10`) and `esp0` (the kernel stack pointer, updated every time you context-switch to a new process so each process gets its own kernel stack on interrupt entry):

```c
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;      // ← kernel stack pointer — updated on every process switch
    uint32_t ss0;       // ← kernel stack segment, always 0x10
    // ... (many reserved fields the CPU uses internally)
    uint32_t cs, ss, ds, es, fs, gs;
} __attribute__((packed)) tss_entry_t;

void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;   // called during every context switch
}
```

The TSS must be set up in Stage 3 alongside the GDT, even though you won't need it fully until Stage 5 (multitasking) and Stage 6 (syscalls from userspace). Setting it up early avoids a hard-to-debug triple fault later.

**IDT — Interrupt Descriptor Table:**
The IDT maps interrupt numbers (0–255) to handler functions.
- ISRs 0–31: CPU exceptions (divide by zero, page fault, GPF, etc.)
- IRQs 0–15: Hardware interrupts (remapped to ISRs 32–47 after PIC init)
- ISR 0x80: System call interrupt (for later)

**What to implement:**
- `gdt_init()` — fill GDT entries, load with `lgdt` instruction
- `idt_init()` — fill IDT entries, load with `lidt` instruction
- 32 assembly ISR stubs (auto-generate these with a macro)
- `isr_handler()` — C function that receives exception info and prints it
- `pic_remap()` — remap PIC IRQs from 0–15 to 32–47 (avoid conflicts with CPU exceptions)
- `irq_handler()` — dispatcher for hardware interrupts
- `irq_register(int irq, handler_fn)` — register a handler for a specific IRQ

**Milestone:** Intentionally divide by zero in kmain — your OS should catch it and print "Exception: Divide by Zero" instead of triple-faulting.

---

### Stage 4 — Memory Management

**Goal:** Know exactly which RAM is available, be able to allocate and free physical pages, and set up virtual memory (paging).

**Concepts learned:**
- How the CPU and OS manage memory
- Physical vs virtual addresses — one of the most important concepts in OS dev
- The page table hierarchy (Page Directory → Page Table → Physical Page)
- Why userspace can't access kernel memory
- How `malloc` works under the hood

**Key files:** `kernel/mm/pmm.c`, `kernel/mm/vmm.c`, `kernel/mm/heap.c`

**Sub-stage 4a — Physical Memory Manager (PMM):**

Uses a bitmap to track which 4KB physical pages are free or used.

```c
// Each bit in a uint32_t array represents one 4KB page
// 0 = free, 1 = used
#define PAGE_SIZE 4096

void pmm_init(uint32_t mem_size, multiboot_info_t *mbi);
void *pmm_alloc_page();        // Returns physical address of a free page
void  pmm_free_page(void *p);  // Marks a page as free
```

Parse the Multiboot memory map (passed by GRUB) to know what RAM exists and what's reserved (BIOS, VRAM, etc.).

**Sub-stage 4b — Virtual Memory Manager (VMM) / Paging:**

Set up a page directory and page tables. Map virtual addresses to physical ones.

```c
// x86 two-level paging:
// Virtual address = [10 bits: PD index][10 bits: PT index][12 bits: page offset]

typedef struct { uint32_t entries[1024]; } page_table_t;
typedef struct { uint32_t entries[1024]; } page_directory_t;

void vmm_init();
void vmm_map_page(page_directory_t *pd, uint32_t virt, uint32_t phys, uint32_t flags);
void vmm_unmap_page(page_directory_t *pd, uint32_t virt);
void vmm_switch_directory(page_directory_t *pd);  // loads CR3
```

**Sub-stage 4c — Kernel Heap:**

A simple `kmalloc`/`kfree` for kernel use. Start with a bump allocator, then implement a proper free-list allocator.

```c
void  heap_init(uint32_t start, uint32_t size);
void *kmalloc(size_t size);
void  kfree(void *ptr);
```

**Milestone:** `kmalloc(64)` and `kfree()` work. You can allocate and map pages. Print the memory map from GRUB. Trigger a page fault by accessing an unmapped address — your fault handler should catch it cleanly.

---

### Stage 5 — Multitasking & Scheduler

**Goal:** Run multiple "processes" concurrently. The CPU switches between them using timer interrupts.

**Concepts learned:**
- What a process actually is (a saved CPU state + address space)
- Context switching — saving and restoring registers
- Preemptive multitasking (the timer forces switches, programs don't cooperate)
- The Process Control Block (PCB)
- Round-robin scheduling algorithm

**Key files:** `kernel/proc/process.c`, `kernel/proc/scheduler.c`, `kernel/proc/context_switch.asm`

**Process Control Block:**
```c
typedef struct process {
    uint32_t pid;
    uint32_t esp;           // Saved stack pointer (key for context switch)
    uint32_t eip;           // Saved instruction pointer
    uint32_t *kernel_stack; // Each process has its own kernel stack
    page_directory_t *page_dir; // Own address space
    enum { RUNNING, READY, BLOCKED, ZOMBIE } state;
    struct process *next;   // Linked list for scheduler
} process_t;
```

**Timer (PIT — Programmable Interval Timer):**
```c
// PIT fires IRQ0 at a configurable frequency
// We set it to ~100Hz (every 10ms) — this is our "scheduler tick"
void timer_init(uint32_t frequency);
// On each tick, the timer IRQ calls scheduler_tick()
```

**Context Switch (the heart of multitasking):**
```c
// In assembly — saves current registers to current process PCB,
// loads next process registers, switches page directory, returns into next process
void context_switch(process_t *current, process_t *next);
```

One critical detail: every time you switch to a new process, you must update `tss.esp0` to point to that process's kernel stack. This ensures that if the process triggers a syscall or interrupt while running in Ring 3, the CPU switches to the *correct* kernel stack for that process rather than trampling another process's stack:

```c
void scheduler_tick() {
    process_t *next = pick_next_process();
    tss_set_kernel_stack(next->kernel_stack_top); // ← update TSS before switching
    context_switch(current, next);
}
```

Forgetting this is one of the most common sources of mysterious stack corruption bugs in hobby kernels.

**Round-Robin Scheduler:**
```c
void scheduler_init();
void scheduler_add(process_t *p);
void scheduler_tick();  // Called by timer IRQ — pick next process and switch
process_t *scheduler_current();
```

**Milestone:** Create two kernel threads — one prints "A" and one prints "B" in a loop. You should see them interleaved on screen, proving the scheduler switches between them.

---

### Stage 6 — System Calls

**Goal:** Create a safe, controlled gateway for user programs to request kernel services.

**Concepts learned:**
- Why user code can't call kernel functions directly (privilege rings)
- How `int 0x80` works in Linux (same mechanism you'll implement)
- The ABI (Application Binary Interface) for system calls
- Kernel/user boundary protection

**Key files:** `kernel/syscall/syscall.c`, `kernel/syscall/syscall_table.h`

**Syscall convention (Linux-inspired):**
```
- User program executes: int 0x80
- EAX = syscall number
- EBX, ECX, EDX = arguments
- Return value in EAX
```

**Syscall table:**
```c
// syscall_table.h
#define SYS_EXIT   1
#define SYS_WRITE  2
#define SYS_READ   3
#define SYS_OPEN   4
#define SYS_CLOSE  5
#define SYS_GETPID 6
#define SYS_FORK   7   // stretch goal
```

**Dispatcher:**
```c
// Registered as handler for interrupt 0x80
void syscall_handler(registers_t *regs) {
    uint32_t syscall_num = regs->eax;
    // Validate, dispatch to the right handler, set regs->eax = return value
}
```

**Milestone:** A "userspace" function calls `sys_write(1, "Hello from userspace\n", 21)` via `int 0x80` and it prints on screen. `sys_getpid()` returns the current process's PID.

---

### Stage 7 — Filesystem (ramfs)

**Goal:** Implement a simple in-memory filesystem so processes can open, read, and write "files".

**Concepts learned:**
- What a filesystem actually does (maps names to data)
- The VFS (Virtual Filesystem Switch) abstraction layer — how Linux supports ext4, FAT, NFS etc. through one interface
- Inodes, directory entries, file descriptors
- How `open()`, `read()`, `write()`, `close()` are implemented

**Key files:** `kernel/fs/vfs.c`, `kernel/fs/ramfs.c`

**VFS Interface (abstract):**
```c
// Every filesystem implements these operations
typedef struct {
    int (*open)(const char *path, int flags);
    int (*read)(int fd, void *buf, size_t len);
    int (*write)(int fd, const void *buf, size_t len);
    int (*close)(int fd);
    int (*readdir)(const char *path, dirent_t *entries, int max);
} fs_ops_t;
```

**ramfs — in-memory filesystem:**
- Files stored as simple structs with a name, data buffer, and size
- Directory is a flat array of file entries (no subdirectories for v1)
- Supports: create, open, read, write, close, list

**File descriptor table:**
- Each process has an array of open file descriptors (max 32 or so)
- FD 0 = stdin (keyboard), FD 1 = stdout (VGA), FD 2 = stderr
- Other FDs map to open ramfs files

**Milestone:** From your kernel, create a file "hello.txt", write "Hello World" to it, close it, reopen it, and read it back. Print the contents on screen.

---

### Stage 8 — Userspace & Shell

**Goal:** Run a simple interactive shell as a userspace process. The OS is now functional.

**Concepts learned:**
- How userspace programs are loaded and run
- Writing a program with no standard library (using only your syscalls)
- Process lifecycle — create, run, exit
- Building a REPL (Read-Eval-Print Loop)

**How the shell executable is loaded:**

The shell is compiled as a flat binary (not a full ELF — we strip all headers) and packed into the initramfs image at build time by `tools/mkramfs.c`. Here is the exact journey from source to running process:

```
Build time:
  1. shell.c compiled → shell.elf  (by i686-elf-gcc, linked at user virtual address 0x400000)
  2. shell.elf stripped → shell.bin (objcopy removes ELF headers, leaving raw code+data)
  3. mkramfs packs shell.bin into initramfs.img alongside any other initial files

Boot time:
  4. GRUB loads kernel.elf + initramfs.img as a Multiboot module
  5. kmain() calls initramfs_load() → ramfs now has a file named "shell.bin"
  6. kmain() calls exec("shell.bin") — the kernel's process loader:
       a. Allocates a new page directory (fresh virtual address space)
       b. Reads shell.bin from ramfs into a kernel buffer
       c. Copies it into the new address space starting at 0x400000
       d. Allocates a user-mode stack at 0xBFFFF000 (just below 3GB, Linux convention)
       e. Creates a new PCB with:
             eip = 0x400000   (entry point — start of the flat binary)
             esp = 0xBFFFF000 (user stack)
             cs  = user code segment (Ring 3, GDT entry 3)
             ss  = user data segment (Ring 3, GDT entry 4)
       f. Adds the PCB to the scheduler's run queue
  7. Scheduler picks it up on the next timer tick, context-switches to it
  8. CPU drops to Ring 3, shell starts executing at 0x400000
```

We use a **flat binary** instead of ELF for the shell to keep the loader simple — no ELF header parsing, no dynamic linking, no relocations. The linker script for userspace programs sets the load address to `0x400000` so the binary is already position-correct when copied in. When you want to add a second user program later, you repeat the same process — compile, strip, pack into initramfs, and the kernel loader handles the rest unchanged.

**Key files added:** `tools/mkramfs.c`, `kernel/proc/exec.c`, `userspace/linker_user.ld`

**Syscall wrappers (no libc):**
```c
// Users link against these thin wrappers instead of glibc
static inline void sys_exit(int code) {
    asm volatile("int $0x80" :: "a"(SYS_EXIT), "b"(code));
}

static inline int sys_write(int fd, const void *buf, size_t len) {
    int ret;
    asm volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len));
    return ret;
}
```

**Shell — what it supports:**
```
anubhav-os:/ $ help
anubhav-os:/ $ ls           (list files in ramfs)
anubhav-os:/ $ cat hello.txt
anubhav-os:/ $ echo hello world
anubhav-os:/ $ clear
anubhav-os:/ $ uptime       (seconds since boot using PIT tick count)
anubhav-os:/ $ meminfo      (show physical memory usage)
anubhav-os:/ $ ps           (list running processes)
```

**Milestone:** Boot the OS, see the shell prompt, type `ls`, see files listed, type `cat hello.txt`, see the contents. You have a working OS.

---

## 8. Debugging Strategy

QEMU + GDB is your primary debugging tool. This setup is incredibly powerful — you can set breakpoints inside the kernel, inspect registers, and step through code instruction by instruction.

**Start QEMU in debug mode:**
```bash
qemu-system-i386 -cdrom build/anubhav-os.iso -m 32M -s -S
# -s = open GDB server on localhost:1234
# -S = freeze CPU at startup (wait for GDB to connect)
```

**Connect GDB:**
```bash
gdb build/kernel.elf
(gdb) target remote localhost:1234
(gdb) break kmain
(gdb) continue
(gdb) next
(gdb) print variable_name
(gdb) x/10x 0xB8000   # examine VGA memory
```

**VSCode Integration:** Install the C/C++ extension and create a `launch.json` that connects to QEMU's GDB server. You'll get visual breakpoints and variable watches.

**Serial port logging (very useful):**
```c
// Write debug output to QEMU's serial port — shows up in your terminal
// Port 0xE9 is a QEMU hack: anything written here appears on stdout
void debug_print(const char *str) {
    while (*str) {
        outb(0xE9, *str++);
    }
}
```

Add `-debugcon stdio` to your QEMU command to see this output.

**Common crash types:**
- **Triple fault** → CPU resets immediately. Usually a bad IDT, GDT, or stack. Enable QEMU monitor with `-monitor stdio` and check `info registers`.
- **Page fault** → Accessing unmapped memory. Your page fault handler should print the faulting address (from CR2 register).
- **General Protection Fault** → Segment violation, usually a bad GDT or trying to execute data.

---

## 9. Testing Strategy

Since you have no standard library or test framework, testing requires creativity.

**Kernel unit tests** — Write a `tests/` module that gets compiled into the kernel and runs checks at boot before entering the main kernel loop:

```c
// tests/test_pmm.c
void test_pmm() {
    void *a = pmm_alloc_page();
    void *b = pmm_alloc_page();
    ASSERT(a != NULL);
    ASSERT(b != NULL);
    ASSERT(a != b);
    pmm_free_page(a);
    void *c = pmm_alloc_page();
    ASSERT(c == a);  // Should reuse freed page
    kprintf("[PASS] PMM basic allocation test\n");
}
```

**Visual regression** — QEMU can take screenshots. Write a script that boots the OS and compares the VGA output to expected output.

**QEMU exit codes** — QEMU supports a special ISA debug exit device. You can write to a port to make QEMU exit with a specific code, allowing automated test scripts:

```bash
# In Makefile test target:
qemu-system-i386 -cdrom build/anubhav-os.iso \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -nographic -m 32M
# Exit code 0 = all tests passed
```

---

## 10. Key Concepts Glossary

| Term | Meaning |
|---|---|
| **Bare metal** | Running directly on hardware with no OS underneath |
| **Multiboot** | A spec that lets GRUB load any compliant kernel |
| **Multiboot modules** | Extra files GRUB loads into RAM alongside the kernel (we use this for initramfs) |
| **initramfs** | Initial RAM filesystem image — a binary blob packed with startup files, loaded by GRUB and unpacked into ramfs before the shell runs |
| **Flat binary** | A stripped executable with no ELF headers — raw code+data ready to copy directly into a virtual address and jump to |
| **ELF** | Executable and Linkable Format — standard binary format for Linux/bare-metal |
| **Real mode** | CPU's initial 16-bit mode at boot, limited to 1MB RAM |
| **Protected mode** | 32-bit mode with memory protection, virtual memory, privilege rings |
| **GDT** | Global Descriptor Table — tells the CPU about memory segments |
| **TSS** | Task State Segment — a GDT entry the CPU reads on privilege-level switches to find the kernel stack; `esp0` field must be updated on every context switch |
| **IDT** | Interrupt Descriptor Table — maps interrupt numbers to handler functions |
| **ISR** | Interrupt Service Routine — function that handles an interrupt |
| **IRQ** | Interrupt Request — hardware-generated interrupt (keyboard, timer, etc.) |
| **PIC** | Programmable Interrupt Controller — chip that manages hardware IRQs |
| **PIT** | Programmable Interval Timer — chip that fires periodic IRQs (our scheduler clock) |
| **Paging** | Virtual memory mechanism — maps virtual pages to physical frames |
| **Page fault** | Exception when accessing an unmapped or protected virtual address |
| **CR0, CR2, CR3** | Control registers — CR0 enables paging, CR2 has fault address, CR3 has page dir |
| **PCB** | Process Control Block — kernel data structure representing a process |
| **Context switch** | Saving one process's CPU state and loading another's (also updates TSS esp0) |
| **Ring 0 / Ring 3** | CPU privilege levels — kernel runs in Ring 0, userspace in Ring 3 |
| **syscall** | Mechanism for userspace to request kernel services safely |
| **VFS** | Virtual Filesystem Switch — abstraction layer over different filesystem types |
| **Inode** | Data structure representing a file (metadata + data pointers) |
| **kmalloc** | Kernel-space memory allocator (equivalent of malloc for kernel code) |

---

## 11. Reference Resources

### Essential Reading
- **OSDev Wiki** — https://wiki.osdev.org — The definitive reference. Start with "Bare Bones" tutorial.
- **xv6 Source Code** — https://github.com/mit-pdos/xv6-public — MIT's teaching OS. Read this alongside building yours.
- **Intel x86 Software Developer Manual** — The authoritative CPU reference. Volume 3 (System Programming) is what you need.
- **"Writing a Simple Operating System from Scratch"** by Nick Blundell (free PDF) — excellent linear tutorial

### Tutorials to follow
- https://wiki.osdev.org/Bare_Bones — Your first kernel
- https://wiki.osdev.org/GDT_Tutorial
- https://wiki.osdev.org/IDT
- https://wiki.osdev.org/Paging
- https://wiki.osdev.org/Scheduling_Algorithms

### Video Series
- **Poncho OS Dev** (YouTube) — Very clear series building an OS from scratch step by step
- **Nanobyte** (YouTube) — Excellent bootloader and kernel tutorials

### Cross-Compiler Setup
- https://wiki.osdev.org/GCC_Cross-Compiler — Official OSDev guide for building i686-elf-gcc

### Communities
- **OSDev Discord / Forum** — https://forum.osdev.org — Active community, good for when you get stuck
- **r/osdev** — Reddit community for OS developers

---

## Quick Reference: Boot Sequence Summary

```
Power on
  → BIOS initializes hardware
    → BIOS loads first 512 bytes from disk (MBR) into 0x7C00
      → GRUB loads (from MBR)
        → GRUB reads grub.cfg, presents menu
          → GRUB loads kernel.elf into memory
          → GRUB loads initramfs.img into memory as a Multiboot module
            → GRUB jumps to Multiboot entry point (boot.asm : boot_start)
              → Assembly sets up stack
                → Assembly calls kmain() in C
                  → kmain() initializes:
                      GDT (+ TSS descriptor)
                      → IDT
                      → TSS (esp0 set to kernel stack)
                      → PMM → VMM → Heap
                      → Drivers (VGA, keyboard, timer/PIT)
                      → Scheduler
                      → Syscalls
                      → VFS + ramfs
                      → initramfs_load()  ← unpacks shell.bin into ramfs
                    → kmain() calls exec("shell.bin"):
                        allocates user address space
                        copies flat binary to 0x400000
                        creates PCB (eip=0x400000, Ring 3 stack at 0xBFFFF000)
                        updates TSS esp0 to shell's kernel stack
                        adds PCB to scheduler run queue
                      → Timer tick fires → scheduler picks shell process
                        → context_switch() → CPU drops to Ring 3
                          → Shell runs at 0x400000
                            → You type commands, OS responds
```

---

*Built with curiosity, assembly, and a lot of QEMU reboots. — AnubhavOS*