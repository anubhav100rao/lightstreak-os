# AnubhavOS — CLAUDE.md

A from-scratch, educational x86 32-bit hobby kernel written in C and Assembly.
Runs on QEMU via macOS (Apple Silicon or Intel). Uses GRUB for bootloading.

---

## Tech Stack

- **Language:** C (C99) + x86 Assembly (NASM, Intel syntax)
- **Cross-compiler:** `i686-elf-gcc` (installed at `~/opt/cross/bin/`)
- **Assembler:** `nasm`
- **Emulator:** `qemu-system-i386`
- **Bootloader:** GRUB (via Docker on macOS — `grub-mkrescue` not native)
- **Debugger:** GDB connected to QEMU's GDB server (`-s -S` flags)

## Build & Run Commands

```bash
# Build everything and create ISO
make

# Run in QEMU
make run

# Debug: starts QEMU frozen + opens GDB connected to localhost:1234
make debug

# Clean all build artifacts
make clean
```

**Note:** ISO creation uses Docker (Ubuntu 22.04 with `grub-mkrescue`). Docker must be running.

Add to `~/.zshrc` if cross-compiler is missing from PATH:
```bash
export PATH="$HOME/opt/cross/bin:$PATH"
```

## Repository Structure

```
anubhav-os/
├── boot/           # boot.asm (Multiboot header + entry), grub.cfg
├── kernel/
│   ├── arch/       # GDT, TSS, IDT, ISR/IRQ stubs (x86-specific)
│   ├── mm/         # PMM (physical), VMM/paging, heap (kmalloc/kfree)
│   ├── proc/       # process PCB, scheduler (round-robin), context_switch.asm, exec
│   ├── fs/         # VFS abstraction + ramfs implementation
│   ├── drivers/    # VGA text mode, PS/2 keyboard, PIT timer
│   └── syscall/    # syscall dispatcher + table (int 0x80)
├── userspace/
│   ├── lib/        # syscall wrappers (no libc), string helpers
│   └── shell/      # minimal interactive shell
├── tools/
│   └── mkramfs.c   # Host tool: packs a directory into initramfs.img
├── include/        # types.h, stdint.h, stddef.h (no stdlib)
├── linker.ld       # Kernel link script (loads at 0x100000)
├── linker_user.ld  # Userspace link script (loads at 0x400000)
├── Makefile
└── docs/problem.md # Full project blueprint (architecture, stage plan, glossary)
```

## Key Architecture Facts

- **32-bit protected mode only** — no 64-bit long mode
- **Kernel loads at:** `0x100000` (1 MB mark)
- **Userspace loads at:** `0x400000` (flat binary, no ELF headers)
- **User stack at:** `0xBFFFF000`
- **VGA text buffer:** `0xB8000` (80×25 chars)
- **Syscall mechanism:** `int 0x80`, EAX = syscall number, EBX/ECX/EDX = args
- **Scheduler:** preemptive round-robin, driven by PIT (IRQ0) at ~100Hz
- **Shell delivery:** compiled → stripped flat binary → packed into `initramfs.img` by `mkramfs` → loaded as GRUB Multiboot module → unpacked into ramfs at boot → `exec("shell.bin")` runs it in Ring 3

## TSS / Context Switch Critical Detail

On every context switch, `tss.esp0` **must** be updated to the incoming process's kernel stack top. Without this, any interrupt/syscall from userspace will corrupt the wrong stack.

```c
void scheduler_tick() {
    process_t *next = pick_next_process();
    tss_set_kernel_stack(next->kernel_stack_top); // REQUIRED before context_switch
    context_switch(current, next);
}
```

## Debugging Tips

- **Triple fault** (instant reset): bad GDT/IDT/stack — use `qemu -monitor stdio` + `info registers`
- **Page fault**: check CR2 for faulting address; fault handler should print it
- **Serial debug output**: write to port `0xE9` — appears on stdout with `-debugcon stdio`
- **GDB**: `target remote localhost:1234` after `make debug`; set breakpoints on `kmain`, ISRs, etc.

## Coding Conventions

- Freestanding C — no `#include <stdio.h>` or any stdlib headers; use `include/types.h`
- Assembly files use NASM Intel syntax (`.asm` extension)
- Kernel functions prefixed by subsystem: `pmm_`, `vmm_`, `idt_`, `vga_`, `sched_`, `sys_`
- No dynamic linking — everything statically compiled into kernel ELF or userspace flat binary
- Comments in assembly stubs should explain the calling convention and register contract

## Implementation Stages (from docs/problem.md)

1. Bootloader (Multiboot header, GRUB, boot.asm)
2. Kernel entry + VGA driver + kprintf
3. GDT + TSS + IDT + ISR/IRQ + PIC remap
4. PMM (bitmap) + VMM (paging) + Heap (kmalloc)
5. Processes + round-robin scheduler + PIT + context switch
6. System calls (int 0x80 dispatcher)
7. VFS + ramfs + file descriptors
8. Userspace exec loader + shell
