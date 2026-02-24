# AnubhavOS — System Architecture

A from-scratch, educational x86 32-bit hobby operating system written in C and
NASM assembly. This document gives a complete bird's-eye view of the system:
component layout, memory maps, privilege rings, and the flow of control from
power-on to an interactive shell prompt.

---

## Table of Contents

1. [High-Level Component Map](#1-high-level-component-map)
2. [Physical Memory Layout](#2-physical-memory-layout)
3. [Virtual Address Space](#3-virtual-address-space)
4. [Privilege Ring Model](#4-privilege-ring-model)
5. [Boot Sequence (end-to-end)](#5-boot-sequence-end-to-end)
6. [Kernel Subsystem Overview](#6-kernel-subsystem-overview)
7. [Source Tree Reference](#7-source-tree-reference)
8. [Key Invariants and Constraints](#8-key-invariants-and-constraints)

---

## 1. High-Level Component Map

```
┌──────────────────────────────────────────────────────────────────────┐
│                         USERSPACE  (Ring 3)                           │
│                                                                        │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │  shell.bin — interactive REPL, linked at 0x600000              │  │
│  │    uses: syscall_wrappers.h  string.c  (NO libc)               │  │
│  └──────────────────────────────┬─────────────────────────────────┘  │
│                                  │  int 0x80 (syscalls)               │
└──────────────────────────────────┼─────────────────────────────────────┘
                                   │
═══════════════════════════════════╪═══════════════════════════════════════
                   Ring 0 / Ring 3 boundary (hardware enforced)
═══════════════════════════════════╪═══════════════════════════════════════
                                   │
┌──────────────────────────────────┼──────────────────────────────────────┐
│                    KERNEL  (Ring 0)                                      │
│                                  ▼                                       │
│  ┌────────────┐    ┌───────────────────────────────────────────────┐   │
│  │  boot.asm  │    │  kernel/kernel.c  — kmain() entry + kprintf   │   │
│  │  (Multiboot│    └───────────────────────────────────────────────┘   │
│  │   header)  │                                                         │
│  └────────────┘    ┌──────────────┐  ┌────────────┐  ┌─────────────┐  │
│                    │  arch/       │  │  mm/        │  │  drivers/   │  │
│                    │  gdt.c/.asm  │  │  pmm.c      │  │  vga.c      │  │
│                    │  tss.c       │  │  vmm.c      │  │  timer.c    │  │
│                    │  idt.c/.asm  │  │  heap.c     │  │  keyboard.c │  │
│                    │  isr.c       │  └────────────┘  └─────────────┘  │
│                    │  irq.c       │                                      │
│                    └──────────────┘  ┌────────────┐  ┌─────────────┐  │
│                                      │  proc/      │  │  fs/        │  │
│                    ┌──────────────┐  │  process.c  │  │  vfs.c      │  │
│                    │  syscall/    │  │  scheduler.c│  │  ramfs.c    │  │
│                    │  syscall.c   │  │  ctx_sw.asm │  └─────────────┘  │
│                    └──────────────┘  │  exec.c     │                   │
│                                      └────────────┘                    │
└─────────────────────────────────────────────────────────────────────────┘
                                   │
                    ┌──────────────▼──────────────────┐
                    │  HARDWARE / FIRMWARE              │
                    │  BIOS  GRUB  PIC  PIT  PS/2 KBD  │
                    │  VGA text buffer at 0xB8000       │
                    │  Physical DRAM                    │
                    └──────────────────────────────────┘
```

---

## 2. Physical Memory Layout

After the kernel boots, physical memory is divided as follows:

```
Physical address
0x00000000 ─── page 0 ─────────────────────────────────── [RESERVED]
               (null-pointer protection — PMM keeps this used)

0x00000500 ─── BIOS data area, interrupt vector table ─── [RESERVED]

0x00007C00 ─── GRUB MBR / stage 1 ──────────────────────  (not our concern)

0x00100000 (1 MB mark) ─────────────────────────────────── kernel_start
           ├── .text      — kernel code
           ├── .rodata    — string literals, const tables
           ├── .data      — initialised globals
           └── .bss       — 16KB boot stack + PMM bitmap + zeroed globals
                            └── kernel_end  ◄─ linker exports this symbol

kernel_end ──── GRUB module: initramfs.img ──────────────── [GRUB module]
                (mod_start … mod_end, loaded contiguously after kernel)
                *** heap must start AFTER this ***

page-aligned ── Kernel heap ─────────────────────────────── 2 MB
                kmalloc / kfree memory pool

heap_end ────── Free physical pages ──────────────────────── PMM-managed
                Used for page tables, process kernel stacks, user pages

0x00B8000 ────── VGA text buffer ───────────────────────── 4000 bytes (80×25×2)
                (inside the first 8 MB identity map)

0x00C00000 ──── available RAM (managed by PMM) ──────────── up to 32 MB
                (we run with -m 32M in QEMU)
```

> **Key insight**: The heap must be placed *after* the GRUB module
> (`initramfs.img`).  GRUB places the module right after the kernel image
> in physical memory.  If `heap_init()` starts at `kernel_end`, it would
> zero the module before `initramfs_load()` can parse it.  `kmain()` scans
> `mbi->mods_addr` and places the heap after the highest module end address.

---

## 3. Virtual Address Space

AnubhavOS uses a **flat 32-bit virtual address space** with x86 two-level
paging.  The kernel is identity-mapped: virtual address == physical address for
all kernel pages.

```
Virtual address
0x00000000 ─── [kernel and userspace share the same 4GB space] ─────────
               │
               │  0x00000000 – 0x007FFFFF  (first 8 MB, identity-mapped)
               │  → kernel code, data, VGA buffer, heap all live here
               │
0x00100000 ─── kernel_start  (identity-mapped)
               │  kernel code + data + stack
               │
heap_base  ─── kernel heap (identity-mapped)
               │  kmalloc pool
               │
0x00600000 ─── USER_LOAD_ADDR  ────────────────────────────────────────
               │  shell.bin flat binary copied here (identity-mapped)
               │  Above the heap end (~0x513000), no conflict
               │
               │  ... user code / data ...
               │
               │  0xC0000000 – 0xFFFFFFFF  (unmapped — access causes page fault)
               │
0xFFFFFFFF ─── end of 32-bit address space
```

**Per-process page directory**:

Every user process gets its own `page_directory_t` allocated from the PMM.  The
kernel mappings (directory entries 0 and 1 — covering the first 8 MB) are
copied from `kernel_dir` into the new directory, so the kernel is visible from
user space during system calls.  User-specific mappings (at 0x400000 and
0xBFFFC000–0xBFFFF000) are added by `exec()`.

---

## 4. Privilege Ring Model

```
   ┌────────────────────────────────────────────────┐
   │               x86 Ring Model                    │
   │                                                  │
   │   Ring 0 (kernel)   Ring 3 (user)               │
   │   ┌────────────┐    ┌────────────┐              │
   │   │ kernel.elf │    │ shell.bin  │              │
   │   │            │◄───│ int 0x80   │              │
   │   │            │───►│ iret       │              │
   │   └────────────┘    └────────────┘              │
   │                                                  │
   │   GDT selectors:                                 │
   │     0x08  kernel code  (DPL=0)                   │
   │     0x10  kernel data  (DPL=0)                   │
   │     0x1B  user code    (DPL=3)  = 0x18 | RPL3   │
   │     0x23  user data    (DPL=3)  = 0x20 | RPL3   │
   │     0x28  TSS          (DPL=0)                   │
   └────────────────────────────────────────────────┘
```

**Ring 0 → Ring 3 transition (first run of a user process)**:

```
  scheduler_tick() calls context_switch(idle, shell_pcb)
       ↓
  context_switch.asm rets to proc_iret_trampoline
       ↓
  proc_iret_trampoline executes IRET
       ↓ IRET pops: eip=0x400000, cs=0x1B, eflags=0x202, esp=0xC0000000, ss=0x23
  CPU switches to Ring 3, jumps to 0x400000 (_start in shell.bin)
```

**Ring 3 → Ring 0 transition (system call)**:

```
  shell calls: int $0x80   (EAX=syscall number, EBX/ECX/EDX=args)
       ↓
  CPU hardware: saves Ring-3 eip/cs/eflags/esp/ss on kernel stack (from TSS.esp0)
       ↓
  IDT gate 0x80 → isr128 stub → isr_common_stub
       ↓
  isr_handler() detects int_no==0x80 → syscall_handler(regs)
       ↓
  returns via IRET → back to Ring 3
```

**Why `tss.esp0` must be updated on every context switch**:

The TSS holds a single kernel stack pointer (`esp0`).  When a Ring-3 interrupt
fires, the CPU switches to Ring 0 and loads `ESP` from `tss.esp0`.  If the
wrong process's kernel stack is there, the interrupt handler corrupts it.  The
scheduler calls `tss_set_kernel_stack(next->kernel_stack_top)` *before* every
`context_switch()`.

---

## 5. Boot Sequence (end-to-end)

```
Power on
  │
  ▼
BIOS POST
  │  loads GRUB from disk
  ▼
GRUB stage 1/2
  │  reads grub.cfg:
  │    multiboot /boot/kernel.elf
  │    module    /boot/initramfs.img
  │  loads kernel.elf into RAM at 0x100000
  │  loads initramfs.img immediately after kernel
  │  sets EAX = 0x2BADB002  (Multiboot magic)
  │  sets EBX = physical addr of multiboot_info_t
  │  jumps to boot_start (linker entry point)
  ▼
boot_start  (boot/boot.asm)
  │  mov esp, stack_top    ← set up 16KB kernel stack
  │  push ebx              ← arg1: multiboot_info_t *
  │  push eax              ← arg0: magic
  │  call kmain
  ▼
kmain()  (kernel/kernel.c)
  │
  ├── Stage 2: vga_init() + kprintf banner
  │
  ├── Stage 3: gdt_init()  → lgdt + far jmp to reload CS
  │            tss_init()  → write TSS descriptor into GDT[5], ltr
  │            idt_init()  → set 256 IDT gates, irq_init() remaps PIC, sti
  │
  ├── Stage 4: pmm_init()  → parse Multiboot mmap, build bitmap
  │            vmm_init()  → identity-map 8 MB, enable paging (CR0 bit 31)
  │            heap_init() → free-list heap after kernel+module
  │
  ├── Stage 5: process_init()
  │            scheduler_init()  → create idle process (PID 0)
  │            timer_init(100)   → program PIT at 100 Hz
  │            irq_register(0, scheduler_tick)
  │
  ├── Stage 6: syscall_init()   → announcement (IDT gate set in idt_init)
  │
  ├── Stage 7: vfs_init() + ramfs_init()  → mount ramfs
  │            VFS self-test: create hello.txt, write, read back
  │
  ├── Stage 8: keyboard_init()  → register IRQ1 handler
  │            initramfs_load() → unpack GRUB module into ramfs
  │            exec("shell.bin")→ load flat binary, create Ring-3 PCB, add to scheduler
  │
  └── idle loop: for(;;) { sti; hlt; }
         ↑
         │  timer IRQ fires (~10ms later)
         │
         ▼
  scheduler_tick()
     │  finds shell PID ready
     │  tss_set_kernel_stack(shell->kernel_stack_top)
     │  context_switch(idle, shell)
     │       ↓ rets to proc_iret_trampoline
     │  IRET → Ring 3 at 0x400000
     │
     ▼
  shell _start()
     │  prints ASCII banner
     │  REPL: print_prompt → readline → dispatch command
     │  sys_write(1, ...) → kernel vga_putchar
     │  sys_read(0, ...)  → keyboard_getchar (blocks until key)
```

---

## 6. Kernel Subsystem Overview

| Subsystem | Files | Responsibility |
|-----------|-------|---------------|
| **Boot** | `boot/boot.asm`, `boot/grub.cfg`, `linker.ld` | Multiboot header, stack setup, jump to C |
| **VGA driver** | `drivers/vga.c` | 80×25 text buffer at 0xB8000, scrolling, cursor |
| **GDT** | `arch/gdt.c`, `arch/gdt.asm` | 6-entry GDT, far-jump to reload CS |
| **TSS** | `arch/tss.c` | Single global TSS, `esp0` updated each context switch |
| **IDT / ISR / IRQ** | `arch/idt.c`, `arch/idt.asm`, `arch/isr.c`, `arch/irq.c` | 256-gate IDT, exception handlers, PIC remap, IRQ dispatch |
| **PMM** | `mm/pmm.c` | 32-bit bitmap allocator for 4KB physical page frames |
| **VMM** | `mm/vmm.c` | x86 two-level paging, identity-map first 8MB, user dirs |
| **Heap** | `mm/heap.c` | First-fit free-list, `kmalloc`/`kfree`/`kzalloc` |
| **Timer** | `drivers/timer.c` | PIT channel 0 at 100 Hz, tick counter |
| **Keyboard** | `drivers/keyboard.c` | IRQ1, scancode→ASCII, ring buffer, blocking `getchar` |
| **Process** | `proc/process.c` | PCB allocation, initial kernel/user stack frames |
| **Context switch** | `proc/context_switch.asm` | Save/restore callee-saves, CR3 switch, `kthread_entry`, `proc_iret_trampoline` |
| **Scheduler** | `proc/scheduler.c` | Round-robin circular linked list, preempted by PIT |
| **Syscall** | `syscall/syscall.c` | `int 0x80` dispatcher, 10 syscalls |
| **VFS** | `fs/vfs.c` | Single-mount vtable dispatch |
| **ramfs** | `fs/ramfs.c` | Flat in-RAM filesystem, initramfs unpacker |
| **exec** | `proc/exec.c` | Load flat binary from ramfs into user address space |
| **Shell** | `userspace/shell/shell.c` | Ring-3 REPL, 8 commands |
| **mkramfs** | `tools/mkramfs.c` | Host-side tool: packs a directory into RAMF image |

---

## 7. Source Tree Reference

```
anubhav-os/
├── boot/
│   ├── boot.asm          Multiboot header + entry, sets up stack, calls kmain
│   └── grub.cfg          GRUB menu: loads kernel.elf + module initramfs.img
│
├── include/
│   ├── types.h           uint8/16/32/64_t, NULL, PACKED, NORETURN (no stdlib)
│   └── multiboot.h       multiboot_info_t, multiboot_mmap_entry_t, multiboot_mod_t
│
├── kernel/
│   ├── kernel.c          kmain(), kprintf(), debug_print(), khalt()
│   ├── kernel.h          (forward decls shared across subsystems)
│   │
│   ├── arch/
│   │   ├── io.h           outb/inb/outw/inw/io_wait — inline port I/O
│   │   ├── gdt.h/c        GDT with 6 entries (null, kcode, kdata, ucode, udata, TSS)
│   │   ├── gdt.asm        gdt_flush: lgdt + far jmp to reload CS + reload DS/ES/FS/GS/SS
│   │   ├── tss.h/c        Single TSS; tss_set_kernel_stack() called each ctx switch
│   │   ├── idt.h/c        256-entry IDT; idt_set_gate(); idt_flush
│   │   ├── idt.asm        ISR/IRQ stubs (macro-generated); isr_common_stub; irq_common_stub
│   │   ├── isr.c          Exception names, register dump, page-fault CR2 print
│   │   └── irq.c          PIC remap (IRQ→INT 32-47), EOI-before-handler, irq_register
│   │
│   ├── mm/
│   │   ├── pmm.h/c        Bitmap allocator; pmm_init parses Multiboot mmap
│   │   ├── vmm.h/c        Two-level paging; vmm_init; vmm_map_page; vmm_create_user_directory
│   │   └── heap.h/c       Free-list heap; block_hdr_t with magic canary; split+coalesce
│   │
│   ├── drivers/
│   │   ├── vga.h/c        80×25 text buffer; vga_putchar handles \n \r \t \b; scrolling
│   │   ├── timer.h/c      PIT channel 0; timer_init(hz); timer_tick(); timer_get_seconds()
│   │   └── keyboard.h/c   IRQ1; scan set 1 → ASCII; shift; 256-char ring buffer
│   │
│   ├── proc/
│   │   ├── process.h/c    process_t PCB; process_create (kernel); process_create_user (Ring 3)
│   │   ├── context_switch.asm  Save/restore ebx/esi/edi/ebp; CR3 switch; kthread_entry; proc_iret_trampoline
│   │   ├── scheduler.h/c  Circular singly-linked run queue; round-robin; tss update
│   │   └── exec.h/c       Open ramfs file; vmm_create_user_directory; map binary + stack; scheduler_add
│   │
│   ├── fs/
│   │   ├── vfs.h/c        fs_ops_t vtable; single mounted_fs; vfs_open/read/write/close/readdir
│   │   └── ramfs.h/c      64 flat file entries × 32 KB; initramfs_load parses RAMF magic
│   │
│   └── syscall/
│       ├── syscall_table.h  SYS_EXIT=1 … SYS_PS=10
│       ├── syscall.h        syscall_init / syscall_handler prototypes
│       └── syscall.c        Switch on EAX; sys_write/read/open/close/exit/getpid/readdir/uptime/meminfo/ps
│
├── userspace/
│   ├── lib/
│   │   ├── syscall_wrappers.h  Inline asm int $0x80 wrappers; meminfo_t; ps_entry_t
│   │   ├── string.h/c          strlen/strcmp/strncmp/strcpy/memcpy/memset/itoa/utoa
│   │   └── (crt0.asm)          Minimal C runtime start (links before shell.o)
│   └── shell/
│       └── shell.c             _start(); readline; cmd_help/ls/cat/echo/clear/uptime/meminfo/ps
│
├── tools/
│   └── mkramfs.c          Host tool (cc, not cross-compiled); RAMF format writer
│
├── linker.ld              Kernel: ENTRY(boot_start), . = 0x100000; exports kernel_start/end
├── linker_user.ld         Userspace: ENTRY(_entry), . = 0x600000
└── Makefile               Full build: kernel.elf + shell.bin + initramfs.img + ISO
```

---

## 8. Key Invariants and Constraints

These are the non-negotiable facts the entire system relies on.  Violate any
one and the system will either triple-fault, corrupt memory, or deadlock.

| # | Invariant | Why it matters |
|---|-----------|---------------|
| 1 | `tss.esp0` is updated to `next->kernel_stack_top` **before** every `context_switch()` | Ring-3 interrupts use the TSS to find the kernel stack.  Wrong value → stack corruption. |
| 2 | `pic_send_eoi(irq)` is called **before** the IRQ handler, not after | If `scheduler_tick` does a context switch, the handler never returns.  EOI must re-arm the PIC first, or all future timer ticks are lost. |
| 3 | New kernel threads start via `kthread_entry` (calls `sti` before entry function) | `context_switch` rets into the new thread still inside IRQ context with IF=0.  Without `sti`, the thread spins with interrupts disabled forever. |
| 4 | Heap starts **after** the GRUB module (`initramfs.img`) | GRUB loads the module right after `kernel_end`.  `heap_init()` zeroes its region; if placed too early it destroys the initramfs before parsing it. |
| 5 | The kernel identity-maps the first 8 MB | VGA buffer at 0xB8000, kernel code/data at 0x100000, heap, and PIT/PIC ports all rely on virtual == physical for the first 8 MB. |
| 6 | User page directories **copy** kernel directory entries 0 and 1 | syscall / interrupt handlers run in kernel space.  The kernel pages must be accessible from the user page directory or every interrupt from Ring 3 causes a page fault. |
| 7 | `sys_exit` marks the process ZOMBIE but does **not** call `scheduler_remove()` | `scheduler_remove` sets `p->next = NULL`.  The immediately following `scheduler_tick(NULL)` dereferences `current->next` — calling remove first causes a NULL pointer dereference. |
| 8 | The Multiboot header is in `.multiboot` (first in the ELF) and within the first 8 KB of the image | GRUB scans the first 8 KB of the image for the magic 0x1BADB002.  If the header appears later, GRUB will not recognise the kernel. |
| 9 | ISR stubs push a dummy error code (0) for exceptions that don't have one | The `registers_t` struct has a fixed layout: `int_no` then `err_code` then the CPU frame.  Without the dummy push, all field offsets shift by 4 bytes and the C handler reads garbage. |
| 10 | PIC masks are explicitly set to 0x00/0x00 after remapping | GRUB (or the BIOS) can leave IRQ0 masked.  Restoring the pre-remap IMR values silently blocks the timer. |
