# AnubhavOS — Implementation Todo

Progress tracker for all 8 stages. Update status as each item is completed.

Legend: `[ ]` = pending · `[x]` = done · `[-]` = in progress

---

## Stage 1 — Bootloader ✅

**Goal:** CPU jumps into your code. GRUB loads the kernel. No triple fault.

| Status | Task |
|---|---|
| [x] | Create `boot/boot.asm` — Multiboot header (magic, flags, checksum), 16KB stack in .bss, `boot_start` sets esp, pushes eax/ebx, calls `kmain` |
| [x] | Create `boot/grub.cfg` — menu entry pointing to `/boot/kernel.elf` |
| [x] | Create `linker.ld` — entry `boot_start`, load at `0x100000`, sections: `.multiboot` first, `.text`, `.rodata`, `.data`, `.bss` |
| [x] | Create `include/types.h` — `uint8_t`, `uint16_t`, `uint32_t`, `size_t`, `NULL` (no stdlib) |
| [x] | Create `include/stdint.h` and `include/stddef.h` — freestanding shims |
| [x] | Create stub `kernel/kernel.c` with `kmain(uint32_t magic, void *mbi)` that halts (`hlt` loop) |
| [x] | Create `Makefile` — `i686-elf-gcc` + `nasm`, link with `linker.ld`, ISO via native `i686-elf-grub-mkrescue`, targets: `all`, `run`, `debug`, `clean` |

**Test:** ✅ QEMU opens → GRUB menu appears → kernel loads → no triple fault.

---

## Stage 2 — Kernel Entry & VGA Output ✅

**Goal:** `kmain()` runs and prints text to the screen.

| Status | Task |
|---|---|
| [x] | Create `kernel/arch/io.h` — inline `outb(port, val)` and `inb(port)` using inline asm |
| [x] | Create `kernel/drivers/vga.h` + `kernel/drivers/vga.c` — `vga_init()`, `vga_putchar(char)`, `vga_puts(char*)`, scrolling when row 25 reached |
| [x] | Implement `kprintf(const char *fmt, ...)` in `kernel/kernel.c` — support `%s`, `%d`, `%u`, `%x`, `%c` |
| [x] | Update `kmain()` to call `vga_init()` then print `"AnubhavOS booting..."` in green (`VGA_COLOR_GREEN` fg, `VGA_COLOR_BLACK` bg) |
| [x] | Add `debug_print()` serial output (port 0xE9) visible with `-debugcon stdio` |

**Test:** ✅ Serial output confirms `AnubhavOS booting...` and `Multiboot OK`.

---

## Stage 3 — GDT, TSS & IDT (Interrupts) ✅

**Goal:** CPU descriptor tables set up. Exceptions caught gracefully instead of triple-faulting.

### GDT + TSS

| Status | Task |
|---|---|
| [x] | Create `kernel/arch/gdt.h` + `kernel/arch/gdt.c` — 6-entry GDT: null, kernel code (ring 0), kernel data (ring 0), user code (ring 3), user data (ring 3), TSS descriptor |
| [x] | Create `kernel/arch/gdt.asm` — `gdt_flush(gdtr*)`: `lgdt`, far jump to reload CS, reload segment registers |
| [x] | Create `kernel/arch/tss.h` + `kernel/arch/tss.c` — single `tss_entry_t` with `ss0=0x10`; `tss_init()` fills entry and loads with `ltr`; `tss_set_kernel_stack(uint32_t esp0)` updates `esp0` field |
| [x] | Wire `gdt_init()` → `tss_init()` → `idt_init()` into `kmain()` |

### IDT + ISR/IRQ

| Status | Task |
|---|---|
| [x] | Create `kernel/arch/idt.h` + `kernel/arch/idt.c` — 256-entry IDT, `idt_set_gate(num, handler, sel, flags)`, `idt_init()` loads with `lidt` |
| [x] | Create `kernel/arch/idt.asm` — 32 ISR stubs (macro-generated): stubs without error code push dummy 0, all stubs push interrupt number, call common `isr_common_stub`; 16 IRQ stubs similarly calling `irq_common_stub` |
| [x] | Create `kernel/arch/isr.c` — `isr_handler(registers_t *regs)`: prints exception name (map 0–21) + register dump via `kprintf`; halts |
| [x] | Create `kernel/arch/irq.c` — `pic_remap()` remaps PIC (IRQs 0–15 → IDT 32–47); `irq_handler(registers_t*)` dispatches to registered handler; `irq_register(int irq, void (*fn)(registers_t*))` |

**Test:** ✅ Inline-asm divide-by-zero triggered → `[serial] EXCEPTION caught` confirmed → ISR 0 fires correctly. No triple-fault.

---

## Stage 4 — Memory Management ✅

**Goal:** Know available RAM, allocate/free physical pages, enable virtual memory (paging), provide `kmalloc`/`kfree`.

### 4a — Physical Memory Manager (PMM)

| Status | Task |
|---|---|
| [x] | Create `kernel/mm/pmm.h` + `kernel/mm/pmm.c` — bitmap allocator (`uint32_t[]`), each bit = one 4KB page |
| [x] | `pmm_init(uint32_t mem_size, multiboot_info_t *mbi)` — parse Multiboot memory map, mark all pages used, then free usable regions; mark kernel pages as used |
| [x] | `pmm_alloc_page()` — find first free bit, mark used, return physical address |
| [x] | `pmm_free_page(void *p)` — mark bit as free |
| [x] | Print memory map at boot via `kprintf` (regions, sizes, types) |

### 4b — Virtual Memory Manager (VMM / Paging)

| Status | Task |
|---|---|
| [x] | Create `kernel/mm/vmm.h` + `kernel/mm/vmm.c` — `page_directory_t` and `page_table_t` structs (1024 `uint32_t` entries each) |
| [x] | `vmm_init()` — allocate kernel page directory, identity-map first 8MB (covers kernel + VGA), enable paging by setting CR0 bit 31 |
| [x] | `vmm_map_page(page_directory_t*, uint32_t virt, uint32_t phys, uint32_t flags)` |
| [x] | `vmm_unmap_page(page_directory_t*, uint32_t virt)` |
| [x] | `vmm_switch_directory(page_directory_t*)` — write to CR3 |
| [x] | Page fault handler (ISR 14) — read CR2, print faulting address, halt |

### 4c — Kernel Heap

| Status | Task |
|---|---|
| [x] | Create `kernel/mm/heap.h` + `kernel/mm/heap.c` — free-list allocator with block headers and coalescing |
| [x] | `heap_init(uint32_t start, uint32_t size)` |
| [x] | `kmalloc(size_t size)` — returns aligned pointer |
| [x] | `kfree(void *ptr)` — marks block free, coalesces with next block |
| [x] | Wire `pmm_init()` → `vmm_init()` → `heap_init()` into `kmain()` |

**Test A:** ✅ Boot prints memory map; `kmalloc(64)` → `kfree()` round-trip succeeds; allocating two blocks returns different addresses.

**Test B:** ✅ Page fault handler catches unmapped address access cleanly.

---

## Stage 5 — Multitasking & Scheduler ✅

**Goal:** Multiple processes run concurrently, preempted by timer interrupts.

| Status | Task |
|---|---|
| [x] | Create `kernel/drivers/timer.h` + `kernel/drivers/timer.c` — `timer_init(uint32_t hz)` programs PIT channel 0 to fire IRQ0 at given frequency (~100 Hz); increment global tick counter |
| [x] | Create `kernel/proc/process.h` — `process_t` PCB: `pid`, `esp`, `eip`, `kernel_stack` (top ptr), `kernel_stack_size`, `page_dir*`, `state` enum (`RUNNING/READY/BLOCKED/ZOMBIE`), `next*` |
| [x] | Create `kernel/proc/process.c` — `process_create(entry_fn, page_dir*)`: allocates kernel stack via `kmalloc`, sets up initial stack frame so first `ret` lands at entry; assigns PID |
| [x] | Create `kernel/proc/context_switch.asm` — `context_switch(process_t *current, process_t *next)`: push all registers to current stack, save esp to `current->esp`, load `next->esp`, switch CR3 if page dirs differ, pop all registers, ret |
| [x] | Create `kernel/proc/scheduler.h` + `kernel/proc/scheduler.c` — round-robin linked list; `scheduler_init()`, `scheduler_add(process_t*)`, `scheduler_tick()` (called from timer IRQ), `scheduler_current()` |
| [x] | In `scheduler_tick()`: call `tss_set_kernel_stack(next->kernel_stack_top)` **before** `context_switch()` |
| [x] | Wire `timer_init(100)` + `scheduler_init()` into `kmain()`; register `scheduler_tick` as IRQ0 handler |

**Test:** ✅ Two kernel-mode processes interleave `A` and `B` output on screen, proving preemptive switching works.

---

## Stage 6 — System Calls ✅

**Goal:** Safe user→kernel gateway via `int 0x80`.

| Status | Task |
|---|---|
| [x] | Create `kernel/syscall/syscall_table.h` — define `SYS_EXIT 1`, `SYS_WRITE 2`, `SYS_READ 3`, `SYS_OPEN 4`, `SYS_CLOSE 5`, `SYS_GETPID 6` |
| [x] | Create `kernel/syscall/syscall.h` + `kernel/syscall/syscall.c` — `syscall_init()` registers IDT gate 0x80; `syscall_handler(registers_t*)` dispatches on `regs->eax` |
| [x] | Implement `sys_write(int fd, const void *buf, size_t len)` — fd 1 → VGA, fd 2 → VGA (stderr), future fds → ramfs |
| [x] | Implement `sys_exit(int code)` — mark current process `ZOMBIE`, trigger `scheduler_tick()` |
| [x] | Implement `sys_getpid()` — return `scheduler_current()->pid` |
| [x] | Wire `syscall_init()` into `kmain()` |

**Test:** ✅ Kernel-mode code executes `int $0x80` with `eax=SYS_WRITE, ebx=1, ecx=msg, edx=len` → message appears on screen. `SYS_GETPID` returns correct PID.

---

## Stage 7 — Filesystem (VFS + ramfs) ✅

**Goal:** Processes can open, read, write, and close named files in memory.

| Status | Task |
|---|---|
| [x] | Create `kernel/fs/vfs.h` + `kernel/fs/vfs.c` — `fs_ops_t` interface struct (open, read, write, close, readdir); `vfs_mount(fs_ops_t*)`, `vfs_open()`, `vfs_read()`, `vfs_write()`, `vfs_close()` |
| [x] | Create `kernel/fs/ramfs.h` + `kernel/fs/ramfs.c` — flat array of file entries (`name[64]`, `data*`, `size`, `used`); `ramfs_create()`, `ramfs_open()`, `ramfs_read()`, `ramfs_write()`, `ramfs_close()`, `ramfs_readdir()` |
| [x] | Add per-process file descriptor table to `process_t` — `fd_entry_t` + `fd_table[32]` array; fd 0 = stdin (keyboard), fd 1 = stdout (VGA), fd 2 = stderr (VGA) |
| [x] | Wire `sys_open()`, `sys_read()`, `sys_write()`, `sys_close()` to VFS layer (allocate/free fd slots in process fd table) |
| [x] | Wire `vfs_init()` + `ramfs_init()` + `vfs_mount(&ramfs_ops)` into `kmain()` |

**Test:** ✅ In `kmain()`: create `hello.txt`, write `"Hello from AnubhavOS!"`, close, reopen, read back → screen shows message.

---

## Stage 8 — Userspace & Shell ✅

**Goal:** A real userspace process (Ring 3 shell) runs and accepts keyboard commands.

### 8a — Host Tool: mkramfs

| Status | Task |
|---|---|
| [x] | Create `tools/mkramfs.c` — compiled on macOS (not cross-compiled); reads a directory, writes a binary `initramfs.img`: header with file count + per-file (name, offset, size) entries, followed by raw file data blobs |

### 8b — Keyboard Driver

| Status | Task |
|---|---|
| [x] | Create `kernel/drivers/keyboard.h` + `kernel/drivers/keyboard.c` — IRQ1 handler; scancode-to-ASCII table (US QWERTY + shift); circular input ring buffer; `keyboard_getchar()` for blocking read |
| [x] | Register keyboard IRQ1 handler via `irq_register(IRQ_KEYBOARD, keyboard_irq_handler)` |
| [x] | Implement `sys_read(0, buf, len)` — reads from keyboard ring buffer (block if empty) |

### 8c — Userspace Linker & Lib

| Status | Task |
|---|---|
| [x] | Create `linker_user.ld` — loads at `0x400000`; sections: `.text`, `.rodata`, `.data`, `.bss` |
| [x] | Create `userspace/lib/syscall_wrappers.h` — inline asm wrappers: `sys_write()`, `sys_read()`, `sys_exit()`, `sys_getpid()`, `sys_open()`, `sys_close()`, `sys_readdir()`, `sys_uptime()`, `sys_meminfo()`, `sys_ps()` |
| [x] | Create `userspace/lib/string.h` + `userspace/lib/string.c` — `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `memcpy`, `memset`, `itoa`, `utoa` (no libc) |

### 8d — Exec Loader + initramfs

| Status | Task |
|---|---|
| [x] | Implement `initramfs_load(uint32_t mod_start, uint32_t size)` in `kernel/fs/ramfs.c` — parse initramfs.img header, copy each file's data into a new ramfs entry |
| [x] | Create `kernel/proc/exec.h` + `kernel/proc/exec.c` — `exec(const char *filename)`: look up file in ramfs, allocate new page directory, map user pages at `0x400000`, copy binary, allocate user stack at `0xBFFFF000`, create PCB with Ring 3 segments, add to scheduler |
| [x] | In `kmain()`: read Multiboot module pointer from `mbi->mods_addr`, call `initramfs_load()`, then call `exec("shell.bin")` |

### 8e — Shell

| Status | Task |
|---|---|
| [x] | Create `userspace/shell/shell.c` — REPL: `sys_read(0, buf, 1)` for input, parse command, dispatch |
| [x] | Shell command: `help` — print list of commands |
| [x] | Shell command: `ls` — `sys_readdir()` → list files with sizes |
| [x] | Shell command: `cat <file>` — `sys_open`, `sys_read`, print, `sys_close` |
| [x] | Shell command: `echo <args>` — `sys_write` the arguments |
| [x] | Shell command: `clear` — write 80×25 spaces to VGA via `sys_write` |
| [x] | Shell command: `uptime` — `SYS_UPTIME` syscall returns seconds since boot |
| [x] | Shell command: `meminfo` — `SYS_MEMINFO` syscall returns free/used pages from PMM |
| [x] | Shell command: `ps` — `SYS_PS` syscall iterates scheduler run queue, prints PIDs + states |

### 8f — Makefile Updates

| Status | Task |
|---|---|
| [x] | Add `tools/mkramfs` build target (compiled with host `cc`, not cross-compiler) |
| [x] | Add shell build pipeline: `shell.c` → `shell.elf` (cross-compiled, linked at `0x400000`) → `shell.bin` (stripped with `i686-elf-objcopy -O binary`) |
| [x] | Add `initramfs.img` build target: run `./build/mkramfs build/initramfs_root build/initramfs.img` |
| [x] | Update GRUB ISO target to include `initramfs.img` as a Multiboot module (add `module /boot/initramfs.img` to `grub.cfg`) |

**Build:** ✅ `make clean && make` builds everything: kernel (18 C + 4 ASM), mkramfs (host tool), shell.bin (4722 bytes), initramfs.img (4985 bytes), ISO.

**Boot test (headless QEMU, `-debugcon stdio`):** ✅ All 8 init stages confirmed via serial output:
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
After keyboard init the kernel loads initramfs, exec()s shell.bin into Ring 3, and enters the idle loop. Run `make run` to interact with the shell (VGA output only).

---

## Bonus

| Status | Task |
|---|---|
| [x] | Serial debug output — `debug_print(const char*)` writes to port `0xE9`; add `-debugcon stdio` to QEMU run command |
| [ ] | Kernel unit tests — `tests/test_pmm.c`, `tests/test_heap.c`, `tests/test_vmm.c`; ASSERT macro; run at kmain boot before scheduler starts; exit via `isa-debug-exit` device with code 0 (pass) or 1 (fail) |

---

## Quick Boot Sequence Reference

```
Power on → BIOS → GRUB → kernel.elf loaded at 0x100000
  → initramfs.img loaded as Multiboot module
    → boot_start (asm): stack setup, call kmain()
      → kmain():
          gdt_init() + tss_init()
          idt_init() + pic_remap()
          pmm_init() + vmm_init() + heap_init()
          vga_init()
          timer_init(100) + scheduler_init()
          syscall_init()
          vfs_init() + ramfs_init()
          initramfs_load()   ← shell.bin now in ramfs
          exec("shell.bin")  ← Ring 3 process queued
        → timer fires → scheduler picks shell → context_switch()
          → CPU drops to Ring 3 at 0x400000
            → shell REPL running
```

---

*Last updated: 2026-02-24*
