# Boot Process

This document explains every step from power-on to the first C instruction in
`kmain()`.  Topics covered: the Multiboot spec, GRUB configuration, the boot
assembly entry point, the kernel linker script, and the `kmain()` initialisation
sequence.

---

## Table of Contents

1. [The Multiboot Specification](#1-the-multiboot-specification)
2. [GRUB Configuration](#2-grub-configuration)
3. [boot.asm — The Kernel Entry Point](#3-bootasm--the-kernel-entry-point)
4. [linker.ld — The Kernel Linker Script](#4-linkerld--the-kernel-linker-script)
5. [kmain() — First C Code](#5-kmain--first-c-code)
6. [Common Boot Failures and Diagnosis](#6-common-boot-failures-and-diagnosis)

---

## 1. The Multiboot Specification

The **Multiboot 1 specification** is a contract between a bootloader (GRUB) and
a kernel.  It lets a kernel declare "I am Multiboot-compliant" by placing a
small header near the start of its binary.  GRUB then loads the kernel, fills
in an information structure, and jumps to the kernel's entry point with
registers set to known values.

### 1.1 What GRUB checks

GRUB scans the first **8 KB** of the kernel binary looking for a 4-byte-aligned
magic number:

```
0x1BADB002   ← GRUB looks for exactly this value
```

If found, it validates a checksum over the next two words:

```c
magic + flags + checksum == 0  (mod 2^32)
```

Our header in `boot.asm`:

```nasm
MBOOT_MAGIC    equ 0x1BADB002
MBOOT_ALIGN    equ 1 << 0    ; bit 0: align loaded modules on page boundaries
MBOOT_MEMINFO  equ 1 << 1    ; bit 1: provide memory map in multiboot_info_t
MBOOT_FLAGS    equ MBOOT_ALIGN | MBOOT_MEMINFO   ; = 0x03
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)  ; ensures sum == 0

section .multiboot
align 4
    dd MBOOT_MAGIC     ; 0x1BADB002
    dd MBOOT_FLAGS     ; 0x00000003
    dd MBOOT_CHECKSUM  ; 0xE4524FFB
```

The linker script places `.multiboot` first inside `.text`, guaranteeing the
header is within the first 8 KB.

### 1.2 Register contract at kernel entry

When GRUB jumps to `boot_start`, the CPU is in **32-bit protected mode** with:

| Register | Value | Meaning |
|----------|-------|---------|
| `EAX` | `0x2BADB002` | Magic confirming a Multiboot-compliant bootloader |
| `EBX` | physical address | Pointer to `multiboot_info_t` struct filled by GRUB |

All other registers are undefined.  Interrupts are disabled.  The stack is
undefined (we immediately set up our own).

### 1.3 `multiboot_info_t` structure

The struct is defined in `include/multiboot.h`.  The fields we use:

```c
typedef struct {
    uint32_t flags;        // Which fields below are valid (bitmask)
    uint32_t mem_lower;    // KB of low memory  (if bit 0 of flags set)
    uint32_t mem_upper;    // KB of high memory (if bit 0 of flags set)
    // ...
    uint32_t mods_count;   // Number of loaded modules   (if bit 3 set)
    uint32_t mods_addr;    // Physical addr of first multiboot_mod_t (if bit 3)
    // ...
    uint32_t mmap_length;  // Bytes in memory map buffer  (if bit 6 set)
    uint32_t mmap_addr;    // Physical addr of first mmap entry  (if bit 6)
    // ...
} PACKED multiboot_info_t;
```

**Checking flags before use** is mandatory — each bit indicates whether the
corresponding field was filled in:

```c
// Example: only read mmap if GRUB says it's valid
if (mbi->flags & MBOOT_FLAG_MMAP) {
    // safe to use mbi->mmap_addr and mbi->mmap_length
}

// Example: only read modules if bit 3 is set
if (mbi->flags & (1u << 3) && mbi->mods_count > 0) {
    multiboot_mod_t *mod = (multiboot_mod_t *)mbi->mods_addr;
    // mod->mod_start, mod->mod_end are valid physical addresses
}
```

### 1.4 Memory map entries

Each entry in the Multiboot memory map:

```c
typedef struct {
    uint32_t size;          // Entry size (NOT including this field!)
    uint32_t base_low;      // Physical base [31:0]
    uint32_t base_high;     // Physical base [63:32] (always 0 on 32-bit)
    uint32_t length_low;    // Region length [31:0]
    uint32_t length_high;   // Region length [63:32]
    uint32_t type;          // 1 = available RAM, anything else = reserved
} PACKED multiboot_mmap_entry_t;
```

Navigation between entries uses the `size` field (the struct is **variable-length**):

```c
// Iterating the memory map (from pmm.c)
uint32_t offset = 0;
while (offset < mbi->mmap_length) {
    multiboot_mmap_entry_t *e =
        (multiboot_mmap_entry_t *)(mbi->mmap_addr + offset);

    if (e->type == MBOOT_MMAP_TYPE_AVAILABLE) {   // type == 1
        // free region: e->base_low, e->length_low
    }

    offset += e->size + 4;   // +4 because 'size' field is NOT included in 'size'
}
```

Typical output on a 32 MB QEMU VM:

```
Region 0x00000000 – 0x0009FBFF  available (639 KB)  ← conventional memory
Region 0x0009FC00 – 0x0009FFFF  reserved            ← EBDA
Region 0x000F0000 – 0x000FFFFF  reserved            ← ROM/BIOS
Region 0x00100000 – 0x01FFFFFF  available (31 MB)   ← our kernel lives here
Region 0x00FFFC00 – 0x00FFFFFF  reserved
```

---

## 2. GRUB Configuration

File: `boot/grub.cfg`

```
set timeout=3     # Auto-boot after 3 seconds
set default=0     # Default to entry 0

menuentry "AnubhavOS" {
    multiboot /boot/kernel.elf      # Load kernel; tell GRUB it's Multiboot
    module    /boot/initramfs.img   # Load initramfs as a Multiboot module
    boot                            # Hand off to kernel
}
```

**`multiboot` command**: Verifies the Multiboot header, loads the ELF, parses
program headers to load segments at their stated physical addresses, sets up
`multiboot_info_t`.

**`module` command**: Loads the file into physical memory (right after the
kernel image), fills in `mbi->mods_count = 1` and `mbi->mods_addr` → a
`multiboot_mod_t {mod_start, mod_end, cmdline, reserved}`.

**Why pass `initramfs.img` as a module?**: The kernel needs to know *where* the
file system image is in physical memory.  Embedding it in the kernel ELF would
force a rebuild on every filesystem change.  GRUB modules let them live as
separate files while the kernel discovers their address via `mbi`.

---

## 3. boot.asm — The Kernel Entry Point

File: `boot/boot.asm`

```nasm
; ---------------------------------------------------------------------------
; Multiboot header — placed in section .multiboot so linker puts it first
; ---------------------------------------------------------------------------
section .multiboot
align 4
    dd 0x1BADB002       ; magic
    dd 0x00000003       ; flags (ALIGN | MEMINFO)
    dd 0xE4524FFB       ; checksum  (0 - magic - flags, truncated to 32 bits)

; ---------------------------------------------------------------------------
; Kernel stack — 16 KB in .bss (not in binary, zero-initialised at load time)
; ---------------------------------------------------------------------------
section .bss
align 16
stack_bottom:
    resb 16384          ; 16 KB boot stack
global stack_top
stack_top:              ; stack_top exported so tss_init() can use it

; ---------------------------------------------------------------------------
; Entry point
; ---------------------------------------------------------------------------
section .text
global boot_start
extern kmain

boot_start:
    mov esp, stack_top  ; set stack pointer to top of our stack region

    ; cdecl: args pushed right-to-left
    push ebx            ; arg1: multiboot_info_t *mbi
    push eax            ; arg0: uint32_t magic (0x2BADB002)
    call kmain          ; never returns

.hang:                  ; safety net if kmain() returns (should never happen)
    cli
    hlt
    jmp .hang
```

**Why `align 16` for the stack?**  The System V x86 ABI requires the stack
pointer to be 16-byte aligned on function entry.  Starting with 16-byte
alignment ensures every `call` preserves alignment.

**Why `resb` (not `db`)?**  `resb N` in `.bss` reserves N bytes without emitting
any data into the binary.  The OS loader (GRUB) zero-initialises `.bss` at load
time.  Using `db 0, 0, ...` would add 16 KB of zeros to the binary.

**EAX / EBX preservation**: GRUB sets EAX=0x2BADB002 and EBX=mbi pointer.
We push them before calling `kmain` to pass them as C function arguments
(`push ebx` first because cdecl is right-to-left).  Once they are on the
stack they are safe even if GRUB's internal data is overwritten.

---

## 4. linker.ld — The Kernel Linker Script

File: `linker.ld`

```ld
ENTRY(boot_start)       /* Execution begins here */

SECTIONS {
    . = 0x100000;       /* Load at 1 MB physical address */

    .text : {
        *(.multiboot)   /* Multiboot header MUST be within first 8 KB of image */
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(COMMON)       /* Uninitialised globals from C */
        *(.bss)
        *(.bss.*)
    }

    /* kernel_end marks the first byte AFTER the kernel image.
     * PMM starts the heap here (after aligning to a page boundary).
     * This must be AFTER .bss so the heap doesn't overlap the boot stack. */
    kernel_start = 0x100000;
    kernel_end   = .;
}
```

**Why `0x100000` (1 MB)?**  The first 1 MB of physical memory is reserved: the
BIOS lives in the top 384 KB (0xA0000–0xFFFFF), and below 0xA0000 is split
between conventional memory (0–640 KB) and hardware-mapped regions (VGA at
0xB8000).  Loading above 1 MB avoids all of these.

**Why `.multiboot` before `.text`?**  `*(.multiboot)` collects the
`section .multiboot` fragment from `boot.asm`.  If this line were missing or
placed after `.text`, the Multiboot header would appear past the first 8 KB and
GRUB would fail to find it.

**`kernel_end` usage in `kmain()`**:

```c
extern uint8_t kernel_end[];   // declared in kernel.c

// Find safe heap start: after kernel image AND GRUB modules
uint32_t safe_end = (uint32_t)kernel_end;
if (mbi->flags & (1u << 3) && mbi->mods_count > 0) {
    multiboot_mod_t *mods = (multiboot_mod_t *)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count; i++) {
        if (mods[i].mod_end > safe_end)
            safe_end = mods[i].mod_end;
    }
}
uint32_t heap_base = (safe_end + 0xFFFu) & ~0xFFFu;  // page-align
heap_init(heap_base, 2 * 1024 * 1024);               // 2 MB heap
```

---

## 5. kmain() — First C Code

File: `kernel/kernel.c`

`kmain` is the C entry point.  It runs once at boot and never returns (it
becomes the idle loop).  The initialisation sequence is **order-sensitive** —
each stage depends on those before it.

```c
void kmain(uint32_t magic, multiboot_info_t *mbi) {

    // Stage 2: VGA (no dependencies — just memory-mapped I/O)
    vga_init();
    kprintf("AnubhavOS booting...\n");

    // Verify GRUB loaded us correctly
    if (magic != 0x2BADB002) {
        kprintf("ERROR: Bad Multiboot magic: 0x%x\n", magic);
        khalt();
    }

    // Stage 3: CPU tables (order: GDT → TSS → IDT)
    gdt_init();   // loads new GDT, far-jumps to reload CS
    tss_init((uint32_t)stack_top);  // writes TSS descriptor into GDT[5], ltr
    idt_init();   // sets up 256 gates, remaps PIC, installs IRQ stubs, STI

    // Stage 4: Memory management (order: PMM → VMM → Heap)
    pmm_init(mbi);           // parse mmap, build page bitmap
    vmm_init();              // identity-map 8 MB, enable paging
    heap_init(heap_base, 2*1024*1024);  // free-list allocator

    // Stage 5: Processes and scheduling
    process_init();
    scheduler_init();        // creates idle process (PID 0)
    timer_init(100);         // programme PIT at 100 Hz
    irq_register(0, scheduler_tick);  // IRQ0 → preemptive scheduler

    // Stage 6: System calls (IDT gate already set in idt_init)
    syscall_init();

    // Stage 7: Filesystem
    vfs_init();
    ramfs_init();            // mounts ramfs via vfs_mount()

    // Stage 8: Input and shell
    keyboard_init();         // register IRQ1 handler
    initramfs_load(mod_start, mod_size);  // unpack GRUB module into ramfs
    process_t *shell = exec("shell.bin"); // create Ring-3 process

    // kmain becomes the idle loop
    for (;;) {
        __asm__ volatile("sti; hlt");  // wait for next interrupt
    }
}
```

**Why `sti; hlt` in the idle loop?**  `hlt` suspends the CPU until the next
interrupt.  Without `sti`, interrupts are disabled and `hlt` would stall
forever.  This loop burns zero CPU cycles while the shell is waiting for input.

**Initialisation order constraints**:

| Constraint | Reason |
|------------|--------|
| GDT before TSS | `tss_init()` writes a descriptor into GDT entry 5 |
| TSS before IDT | `idt_init()` calls `irq_init()` which calls `sti`; without a valid TSS, a Ring-3 interrupt would use garbage for `esp0` |
| PMM before VMM | VMM calls `pmm_alloc_page()` to get physical pages for page tables |
| VMM before Heap | Heap lives in the identity-mapped region; trying to use it before paging is on would be fine in practice, but allocating VMM structures from a pre-heap allocator is messier |
| Heap before Processes | `process_create()` calls `kzalloc()` |
| Scheduler before Timer | The timer IRQ immediately fires; without `scheduler_init()` the handler would find `current == NULL` and crash |
| `initramfs_load` before `exec` | `exec("shell.bin")` opens the file via VFS; the file must already be in ramfs |
| Everything before idle loop | The idle loop enables interrupts; from that point the scheduler may switch to the shell at any moment |

---

## 6. Common Boot Failures and Diagnosis

### Triple fault on boot

**Symptoms**: QEMU resets immediately after loading, no output.

**Causes and fixes**:

| Cause | Diagnosis | Fix |
|-------|-----------|-----|
| Bad Multiboot header | GRUB shows "Error: no multiboot" | Verify magic/flags/checksum; ensure `.multiboot` section is first |
| GDT loaded with wrong base/limit | Immediate reset after first instruction | Use `qemu -monitor stdio` + `info registers`; check CS, DS values |
| Stack pointer not set | First function call corrupts low memory | Ensure `mov esp, stack_top` runs before `call kmain` |
| IDT not set up when interrupt fires | Reset on first timer/keyboard event | Verify `idt_init()` is called before any interrupt can fire |

### "Bad Multiboot magic" printed

GRUB placed something other than the Multiboot magic in EAX.  Causes:
- Kernel was not loaded with `multiboot` (check grub.cfg)
- Running on bare metal with a legacy bootloader

### Kernel loads but hangs at stage N

With `-debugcon stdio` the `debug_print` output tells you exactly which stage
succeeded last:

```bash
make run   # uses -debugcon stdio — debug_print goes to terminal
```

```
[serial] AnubhavOS booting...  ← vga_init + multiboot check OK
[serial] Multiboot OK
[serial] GDT loaded            ← gdt_init OK
[serial] TSS loaded            ← tss_init OK
[serial] IDT+IRQ loaded        ← idt_init + irq_init OK
[serial] PMM ready             ← pmm_init OK
[serial] Paging enabled        ← vmm_init OK
[serial] Heap ready            ← heap_init OK
[serial] Scheduler running     ← process_init + scheduler_init + timer OK
[serial] Syscall init          ← syscall_init OK
[serial] VFS test OK           ← vfs + ramfs OK
[serial] Keyboard ready        ← keyboard_init OK
```

If output stops at `[serial] GDT loaded`, the fault is in `tss_init()`.

### Page fault during boot

**Symptom**: `*** KERNEL EXCEPTION *** Exception 14: Page Fault` with a
`Faulting address (CR2): 0xXXXXXXXX`.

**Causes**:
- Accessing an address outside the first 8 MB identity map before more pages are mapped
- NULL pointer dereference (address 0x00000000 is unmapped)
- Heap overrun past the 2 MB heap region

**Diagnosis**: The CR2 value directly tells you what address was accessed.
Cross-reference it with the kernel memory map.  If it's `0x00000000`, it's a
NULL pointer dereference.  If it's just above `kernel_end + 2 MB`, the heap
overflowed.
