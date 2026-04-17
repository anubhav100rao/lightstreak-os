# AnubhavOS — Current Project Overview

This document is the quickest way to understand the project as it exists in
the repository today. It is intentionally practical: it focuses on the current
scope, the architecture that is actually wired up, the main developer
workflows, and the capabilities you can rely on right now.

Use this as the onboarding doc. For subsystem deep dives, follow the links in
the existing `docs/` set.

---

## 1. Project Scope

AnubhavOS is a from-scratch, educational 32-bit x86 hobby operating system
written in freestanding C and NASM assembly. The project is aimed at learning
the major OS layers directly:

- Bootstrapping a kernel with GRUB and Multiboot
- Setting up x86 protected-mode CPU structures
- Handling interrupts and hardware devices
- Managing physical and virtual memory
- Providing a kernel heap
- Exposing a syscall interface
- Loading a simple userspace-style shell from an initramfs image

The project is intentionally small in scope. It does not currently try to be a
production OS, and several advanced areas are out of scope for now:

- Networking
- Graphics or a GUI
- USB support
- SMP or multicore support
- 64-bit long mode
- Persistent storage and block-device drivers

---

## 2. Current State At A Glance

The repository already contains the full skeleton of a small OS:

- Boot path via GRUB and a Multiboot-compliant `boot/boot.asm`
- A kernel entry point in `kernel/kernel.c`
- GDT, TSS, IDT, ISR, and IRQ support under `kernel/arch/`
- PMM, VMM, and heap allocators under `kernel/mm/`
- PIT timer, PS/2 keyboard, and VGA text-mode drivers under
  `kernel/drivers/`
- A VFS layer and a RAM-backed filesystem under `kernel/fs/`
- A syscall dispatcher using `int 0x80`
- A shell binary built from `userspace/` and packed into an initramfs image

The important nuance is that the project is currently in a hybrid state:

- Several files describe or prepare for full user-mode process execution
  with separate process state and privilege transitions.
- The live boot path is simpler than that full design today.
- `exec()` loads `shell.bin` into memory and creates scheduler metadata, but
  `kmain()` then jumps to the shell entry directly at `0x600000`.
- The timer IRQ currently advances uptime, while the active shell session is
  not being time-sliced through the scheduler's full context-switch path.

That means the codebase already teaches many real OS concepts, but a few parts
are still more "scaffolded" than "fully exercised."

---

## 3. What Works Today

### Boot and CPU setup

- GRUB loads `kernel.elf` and `initramfs.img`
- `boot/boot.asm` sets up a 16 KB kernel stack and calls `kmain()`
- `kmain()` validates the Multiboot magic value
- The kernel initializes the GDT, TSS, and IDT
- PIC IRQs are remapped, and interrupts are enabled

### Memory management

- The PMM parses the Multiboot memory map and tracks page usage with a bitmap
- The VMM enables paging and identity-maps the first 8 MB
- The kernel heap is initialized as a free-list allocator
- Heap placement accounts for GRUB modules so the initramfs is not overwritten

### Drivers and low-level I/O

- VGA text-mode output works, including basic scrolling and color changes
- Serial-style debug output is sent to port `0xE9` for QEMU `-debugcon stdio`
- The PIT timer is programmed and tracks boot uptime
- The PS/2 keyboard IRQ handler translates scancodes into ASCII and buffers
  input for blocking reads

### Filesystem and packaging

- The only mounted filesystem is `ramfs`
- `mkramfs` packages a directory on the host into `initramfs.img`
- At boot, the kernel unpacks the initramfs into RAM-backed files
- The shell and a sample `hello.txt` file are available through VFS calls

### Syscalls and shell features

- Syscalls are dispatched through `int 0x80`
- The kernel currently supports `write`, `read`, `open`, `close`, `exit`,
  `getpid`, `readdir`, `uptime`, `meminfo`, and `ps`
- The shell supports:
  - `help`
  - `ls`
  - `cat <file>`
  - `echo ...`
  - `clear`
  - `uptime`
  - `meminfo`
  - `ps`
  - `exit`

---

## 4. What Is Partially Implemented Or Not Yet Fully Wired

These are the main areas where the repository is ahead of the live boot path:

- Full user-process entry via an `iret` transition is scaffolded in
  `process_create_user()`, but the current `exec()` path uses a kernel-thread
  trampoline and the shell is ultimately entered directly from `kmain()`.
- `vmm_create_user_directory()` exists, but `exec()` does not yet build a
  separate per-process user address space and switch into it.
- The scheduler and context switcher are present, but the currently registered
  timer handler only updates uptime rather than performing preemptive task
  switching during the shell session.
- The docs and comments sometimes describe a fuller Ring 3 process model than
  the runtime path currently exercises.

This is not a problem for the project goal. It just means the right mental
model is:

"The repo contains a lot of the multitasking/userspace infrastructure, but the
current demo path is still a simplified shell-on-top-of-a-live-kernel system."

---

## 5. Architecture Map

### Runtime flow

```text
BIOS
  -> GRUB
  -> boot/boot.asm
  -> kernel/kernel.c:kmain()
  -> arch init (GDT/TSS/IDT/PIC)
  -> memory init (PMM/VMM/heap)
  -> timer + process/scheduler scaffolding
  -> syscall init
  -> VFS + ramfs init
  -> keyboard init
  -> initramfs unpack
  -> exec("shell.bin")
  -> direct jump to shell entry at 0x600000
```

### Subsystem layout

```text
boot/
  GRUB-facing entry stub and Multiboot header

kernel/arch/
  x86 descriptor tables, interrupt stubs, PIC/IRQ handling, I/O helpers

kernel/mm/
  physical pages, paging, heap allocator

kernel/drivers/
  VGA, PIT timer, PS/2 keyboard

kernel/fs/
  VFS dispatch and in-memory ramfs

kernel/syscall/
  int 0x80 dispatcher and syscall numbers

kernel/proc/
  PCB structures, scheduler, context switch code, shell loader

userspace/lib/
  startup stub, string helpers, syscall wrappers

userspace/shell/
  simple REPL using only syscalls

tools/
  host-side helper to build initramfs images
```

### Current execution model

The easiest way to think about the system is:

- The kernel owns all hardware-facing setup and core services
- The shell is built as a standalone flat binary with syscall wrappers
- The shell is loaded from a GRUB module into RAM
- The shell then runs in a simplified execution path that demonstrates the
  syscall interface and filesystem without yet relying on full per-process
  user-mode scheduling

---

## 6. Memory And Addressing Notes

These constants matter when reading the repo:

- Kernel link/load address: `0x100000`
- Identity-mapped region: first `8 MB`
- VGA text buffer: `0xB8000`
- Shell load address in current code: `0x600000`
- QEMU RAM setting in `make run`: `32 MB`

One small but important gotcha: some older comments or docs mention
`0x400000` for userspace, but the current `linker_user.ld` and `exec.c` both
use `0x600000`. When in doubt, trust the current code.

---

## 7. Build, Run, And Debug Workflows

### Build

```bash
make
```

This performs the full pipeline:

- Compile kernel C and assembly
- Link `build/kernel.elf`
- Build the host tool `build/mkramfs`
- Build `build/shell.elf`
- Convert it to `build/shell.bin`
- Pack `build/initramfs.img`
- Create `build/anubhav-os.iso`

### Run

```bash
make run
```

This launches QEMU with:

- the generated ISO
- `32 MB` of RAM
- serial debug output on stdout via `-debugcon stdio`
- `-no-reboot` and `-no-shutdown` so faults are visible

### Debug

```bash
make debug
gdb build/kernel.elf -ex 'target remote localhost:1234'
```

`make debug` starts QEMU paused with the GDB stub exposed on port `1234`.

### Clean

```bash
make clean
```

This removes object files and the entire `build/` directory.

---

## 8. Practical Developer Workflows

### If you want to understand boot first

Read these in order:

1. `boot/boot.asm`
2. `linker.ld`
3. `kernel/kernel.c`

### If you want to understand interrupts and privilege setup

Read:

1. `kernel/arch/gdt.c`
2. `kernel/arch/tss.c`
3. `kernel/arch/idt.c`
4. `kernel/arch/idt.asm`
5. `kernel/arch/isr.c`
6. `kernel/arch/irq.c`

### If you want to understand memory

Read:

1. `kernel/mm/pmm.c`
2. `kernel/mm/vmm.c`
3. `kernel/mm/heap.c`

### If you want to understand the shell path

Read:

1. `userspace/shell/shell.c`
2. `userspace/lib/syscall_wrappers.h`
3. `kernel/syscall/syscall.c`
4. `kernel/fs/vfs.c`
5. `kernel/fs/ramfs.c`
6. `kernel/proc/exec.c`
7. `kernel/kernel.c`

---

## 9. Current Capabilities By User Experience

From the perspective of someone booting the OS in QEMU today, the project can:

- Boot into a custom kernel
- Print boot diagnostics to the screen
- Handle keyboard input
- Expose a simple shell prompt
- List and read files from a RAM-backed filesystem
- Print text to the VGA console through syscalls
- Report uptime and PMM memory statistics
- Show the scheduler's current process list

What a user cannot do yet:

- Launch multiple independent user programs from the shell
- Rely on full user/kernel isolation for the shell path
- Use persistent storage
- Use networking or graphics
- Use a mature process lifecycle model with waiting, signals, or IPC

---

## 10. Known Documentation Drift

The repo already contains several detailed documents, but not all of them have
kept pace with the exact runtime path. While reading the project, keep these
differences in mind:

- Some docs describe Stage 8 as fully complete in a strong Ring 3 sense.
- The current code path still launches the shell through a simplified route.
- Some references mention `0x400000`, but the current userspace linker script
  and loader use `0x600000`.

The deeper docs are still valuable. This file is just the best "current truth"
starting point.

---

## 11. Suggested Next Milestones

If the goal is to move the project from a working educational shell demo to a
more fully realized OS, the most natural next steps are:

1. Switch the live shell launch path to the existing `process_create_user()`
   plus `iret`-based Ring 3 entry.
2. Give `exec()` a real user address space and user stack instead of relying
   on the current direct load-and-jump path.
3. Re-enable scheduler-driven preemption for the active shell session.
4. Expand the shell into a program launcher instead of a single bundled binary.
5. Add automated boot tests or kernel self-tests so regressions are easier to
   catch.

---

## 12. Where To Go Next

After this overview, the most useful supporting docs are:

- `docs/architecture.md`
- `docs/boot.md`
- `docs/gdt-tss-idt.md`
- `docs/memory-management.md`
- `docs/filesystem.md`
- `docs/syscalls.md`
- `docs/shell.md`
- `docs/building.md`
- `docs/testing.md`
