# GDT, TSS, and IDT — Protected Mode Descriptor Tables

This document covers the three CPU descriptor tables that form the foundation
of x86 protected mode: the **Global Descriptor Table** (GDT), the **Task State
Segment** (TSS), and the **Interrupt Descriptor Table** (IDT).  It also covers
the PIC remapping and the ISR/IRQ stub machinery in `idt.asm`.

---

## Table of Contents

1. [Protected Mode Background](#1-protected-mode-background)
2. [Global Descriptor Table (GDT)](#2-global-descriptor-table-gdt)
3. [Task State Segment (TSS)](#3-task-state-segment-tss)
4. [Interrupt Descriptor Table (IDT)](#4-interrupt-descriptor-table-idt)
5. [ISR and IRQ Stubs (idt.asm)](#5-isr-and-irq-stubs-idtasm)
6. [PIC — Programmable Interrupt Controller](#6-pic--programmable-interrupt-controller)
7. [Exception Handler (isr.c)](#7-exception-handler-isrc)
8. [IRQ Handler (irq.c)](#8-irq-handler-irqc)
9. [End-to-End Interrupt Flow](#9-end-to-end-interrupt-flow)

---

## 1. Protected Mode Background

In **Real Mode** (the BIOS default), x86 uses a flat 20-bit address space with
no memory protection.  In **32-bit Protected Mode**, segment registers hold
*selectors* (indices into the GDT) that the CPU uses to enforce access rights
and privilege levels.

However, AnubhavOS uses the **flat memory model**: all segments have base 0 and
limit 4 GB, so segmentation is a no-op.  Real memory isolation is achieved
entirely through **paging** (see `docs/memory-management.md`).  Segmentation
still matters for two things:

1. **Privilege enforcement** — the DPL (Descriptor Privilege Level) field in
   the GDT determines whether Ring-3 code can access the segment.
2. **Gate callability** — the IDT gate's DPL field controls whether Ring-3
   code can trigger `int 0x80`.

---

## 2. Global Descriptor Table (GDT)

### 2.1 Structure

Each GDT entry is **8 bytes** with a peculiar bit layout (historical reasons):

```
Bit layout of a GDT entry (8 bytes total):
 63       56 55   52 51  48 47    40 39      16 15      0
 ┌──────────┬───────┬──────┬────────┬──────────┬─────────┐
 │ Base[31:24]│Flags │Lim[19:16]│Access│Base[23:0]│Lim[15:0]│
 └──────────┴───────┴──────┴────────┴──────────┴─────────┘

Access byte (bits 47–40):
  7: Present (must be 1 for valid descriptor)
  6–5: DPL (Descriptor Privilege Level: 0=Ring0, 3=Ring3)
  4: Descriptor type (1=code/data segment, 0=system)
  3: Executable (1=code segment, 0=data segment)
  2: Direction/Conforming
  1: Readable (code) / Writable (data)
  0: Accessed (set by CPU)

Granularity byte (bits 55–52 + bits 51–48):
  7: Granularity (0=byte, 1=4KB — multiplies limit by 4096)
  6: Size (0=16-bit, 1=32-bit default operand size)
  5–4: Reserved (0)
  3–0: Limit bits [19:16]
```

In C (`arch/gdt.h`):

```c
typedef struct {
    uint16_t limit_low;    // Limit[15:0]
    uint16_t base_low;     // Base[15:0]
    uint8_t  base_mid;     // Base[23:16]
    uint8_t  access;       // P | DPL | S | Type | A
    uint8_t  granularity;  // G | D/B | 0 | 0 | Limit[19:16]
    uint8_t  base_high;    // Base[31:24]
} PACKED gdt_entry_t;
```

### 2.2 Our GDT (6 entries)

```c
// gdt.c — six entries
void gdt_init(void) {
    // Entry 0: Null descriptor (required by spec — selector 0 must be null)
    gdt_set_entry(0, 0, 0, 0, 0);

    // Entry 1: Kernel code — selector 0x08
    //   base=0, limit=4GB, ring 0, execute+read, 32-bit, 4KB granularity
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);
    //                           ^^^^  ^^^^
    //                           |     G=1(4KB gran), D=1(32-bit), limit[19:16]=F
    //                           P=1, DPL=00, S=1, Type=0xA (execute/read)

    // Entry 2: Kernel data — selector 0x10
    //   base=0, limit=4GB, ring 0, read+write
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);
    //                           ^^^^
    //                           P=1, DPL=00, S=1, Type=0x2 (read/write)

    // Entry 3: User code — selector 0x1B  (= 0x18 | RPL3)
    //   ring 3, execute+read
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);
    //                           ^^^^
    //                           P=1, DPL=11, S=1, Type=0xA

    // Entry 4: User data — selector 0x23  (= 0x20 | RPL3)
    //   ring 3, read+write
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);

    // Entry 5: TSS placeholder — tss_init() fills this in
    gdt_set_entry(5, 0, 0, 0, 0);

    gdtr.limit = sizeof(gdt_entry_t) * 6 - 1;
    gdtr.base  = (uint32_t)&gdt;
    gdt_flush(&gdtr);
}
```

**GDT selector arithmetic**:
A selector is the **byte offset into the GDT** plus **RPL bits** in bits [1:0]:

```
Selector 0x00 = entry 0 (null)
Selector 0x08 = entry 1 (kernel code,  offset 8 bytes, RPL=0)
Selector 0x10 = entry 2 (kernel data,  offset 16 bytes, RPL=0)
Selector 0x18 = entry 3 (user code,    offset 24 bytes) — but we or in RPL=3
Selector 0x1B = 0x18 | 0x03  (user code, DPL=3 callable)
Selector 0x20 = entry 4 (user data,    offset 32 bytes)
Selector 0x23 = 0x20 | 0x03  (user data, DPL=3 callable)
Selector 0x28 = entry 5 (TSS,          offset 40 bytes)
```

### 2.3 Flushing the GDT (`gdt.asm`)

Loading the GDTR alone doesn't update the segment registers.  The CPU caches
the old CS descriptor until a **far jump** forces a reload:

```nasm
; gdt.asm
gdt_flush:
    mov eax, [esp+4]   ; eax = &gdtr
    lgdt [eax]         ; load the new GDT register

    ; Far jump: flushes the CS descriptor cache
    ; "0x08" is the kernel code selector, ".flush_cs" is the target offset
    jmp 0x08:.flush_cs

.flush_cs:
    ; Reload all data segment registers with kernel data selector
    mov ax, 0x10       ; kernel data = selector 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
```

**Why a far jump?**  A near jump (`jmp label`) only changes `EIP`.  A far jump
(`jmp seg:offset`) also loads `CS` — this is the only way to change CS in
protected mode without going through a call gate.

---

## 3. Task State Segment (TSS)

### 3.1 Purpose

The TSS is a CPU structure consulted when **switching privilege levels**.  When
a Ring-3 process executes `int 0x80` (or any interrupt fires), the CPU needs a
*kernel* stack to save the interrupted Ring-3 state.  It reads the kernel stack
pointer from `tss.esp0` (and the kernel stack segment from `tss.ss0`).

### 3.2 TSS structure

```c
// arch/tss.h
typedef struct {
    uint32_t prev_tss;   // Hardware task linking (unused)
    uint32_t esp0;       // Ring-0 stack pointer ← WE UPDATE THIS
    uint32_t ss0;        // Ring-0 stack segment ← always 0x10 (GDT_KERNEL_DATA)
    uint32_t esp1;       // Ring-1 (unused)
    uint32_t ss1;
    uint32_t esp2;       // Ring-2 (unused)
    uint32_t ss2;
    uint32_t cr3;        // (unused in our simple implementation)
    uint32_t eip;
    uint32_t eflags;
    // ... general purpose registers (unused) ...
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base; // Set beyond TSS end → no I/O permission bitmap
} PACKED tss_entry_t;
```

### 3.3 Initialisation

```c
// arch/tss.c
static tss_entry_t tss;   // single global TSS

void tss_init(uint32_t kernel_stack) {
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = base + sizeof(tss_entry_t) - 1;

    // Write TSS descriptor into GDT entry 5
    // Access 0x89 = P=1, DPL=0, S=0, Type=9 (32-bit available TSS)
    gdt_set_tss_entry(base, limit);

    // Zero the TSS
    memset(&tss, 0, sizeof(tss_entry_t));

    // Set the two fields that matter at runtime
    tss.ss0  = GDT_KERNEL_DATA;  // 0x10
    tss.esp0 = kernel_stack;     // initially the boot stack top

    // iomap_base beyond TSS → I/O permission bitmap disabled
    // (user programs can't use in/out instructions)
    tss.iomap_base = sizeof(tss_entry_t);

    // Load the task register: tells CPU where the TSS descriptor is in GDT
    __asm__ volatile ("ltr %%ax" : : "a"(GDT_TSS_SEG));  // GDT_TSS_SEG = 0x28
}
```

### 3.4 Updating the TSS on each context switch

**This is the single most critical action in the scheduler.**

```c
// proc/scheduler.c
void scheduler_tick(registers_t *regs) {
    // ... find next READY process ...

    process_t *prev = current;
    current = next;

    // MUST happen before context_switch:
    tss_set_kernel_stack(current->kernel_stack_top);
    //    ↑ sets tss.esp0 = next process's kernel stack top

    context_switch(prev, current);
}

// arch/tss.c
void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}
```

**Why this must happen before `context_switch`**:

Scenario without the update:
1. Shell process (PID 1) is about to run for the first time.
2. `tss.esp0` still points to idle's kernel stack.
3. Scheduler context-switches to shell; shell executes Ring-3 code.
4. Shell calls `sys_write` → `int 0x80`.
5. CPU looks up `tss.esp0` → gets idle's kernel stack pointer.
6. The syscall handler runs on idle's stack, corrupting it.
7. Next time idle runs, its kernel stack is garbage → crash.

With the update before context_switch, `tss.esp0` always points to the
*currently running* process's kernel stack.

---

## 4. Interrupt Descriptor Table (IDT)

### 4.1 Structure

Each IDT entry is **8 bytes**:

```c
typedef struct {
    uint16_t offset_low;   // Handler address bits [15:0]
    uint16_t selector;     // Code segment (always 0x08 = kernel code)
    uint8_t  zero;         // Reserved (must be 0)
    uint8_t  type_attr;    // Gate type + DPL + Present
    uint16_t offset_high;  // Handler address bits [31:16]
} PACKED idt_entry_t;
```

**Type/attribute byte**:

```
Bit 7:    Present (must be 1)
Bits 6-5: DPL (00=Ring0 callable, 11=Ring3 callable)
Bit 4:    0 (not a storage segment)
Bits 3-0: Gate type
           0xE = 32-bit interrupt gate  (clears IF on entry)
           0xF = 32-bit trap gate       (does NOT clear IF — used for int 0x80)
```

```
0x8E = 1_00_0_1110 = Present, DPL=0, interrupt gate  ← for exceptions and IRQs
0xEE = 1_11_0_1110 = Present, DPL=3, interrupt gate  ← callable from Ring 3
```

### 4.2 Gate types

| Gate | Clears IF? | DPL | Use |
|------|-----------|-----|-----|
| Interrupt gate (0xE) | YES | 0 or 3 | Exceptions, hardware IRQs |
| Trap gate (0xF) | NO | 3 | `int 0x80` syscall |

For our syscall gate we use `0xEE` (interrupt gate, DPL=3).  Note: this clears
IF on entry, meaning nested interrupts are disabled during syscall handling.
The shell's `keyboard_getchar()` uses `sti; hlt` inside the kernel to re-enable
interrupts while waiting.

### 4.3 Setting up the IDT (`idt.c`)

```c
void idt_init(void) {
    // Zero all 256 entries
    memset(idt, 0, sizeof(idt));

    idtr.limit = sizeof(idt_entry_t) * 256 - 1;
    idtr.base  = (uint32_t)&idt;

    // CPU exception stubs (Ring-0 only interrupt gates)
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);  // Divide by Zero
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);  // Debug
    // ... (gates 2-31) ...
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);  // Page Fault

    // System call gate: DPL=3 so Ring-3 code can trigger it
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0xEE);

    // Hardware IRQ stubs (gates 32-47) — set up by irq_init()
    irq_init();

    // Load the IDT register
    idt_flush(&idtr);
}

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}
```

---

## 5. ISR and IRQ Stubs (idt.asm)

When an interrupt fires, the CPU:
1. Pushes `EFLAGS`, `CS`, `EIP` (and on privilege change: `SS`, `ESP`).
2. Optionally pushes an error code.
3. Jumps to the address in the IDT gate.

Our assembly stubs normalize this into a uniform `registers_t` frame that C
handlers can read.

### 5.1 Macro-generated stubs

```nasm
; For exceptions without an error code: push dummy 0 so layout is consistent
%macro ISR_NO_ERR 1
global isr%1
isr%1:
    push dword 0    ; dummy error code
    push dword %1   ; interrupt number
    jmp isr_common_stub
%endmacro

; For exceptions that DO push an error code (e.g. Page Fault, GPF)
%macro ISR_ERR 1
global isr%1
isr%1:
    ; CPU already pushed error code before EIP
    push dword %1   ; interrupt number
    jmp isr_common_stub
%endmacro

; Hardware IRQs: no error code, vector number = 32+IRQ#
%macro IRQ 2
global irq%1
irq%1:
    push dword 0    ; no error code
    push dword %2   ; IDT vector (e.g. IRQ0 → 32)
    jmp irq_common_stub
%endmacro
```

Usage:

```nasm
ISR_NO_ERR  0   ; Divide by Zero
ISR_ERR     8   ; Double Fault (CPU pushes error code = 0, but still pushes it)
ISR_ERR    14   ; Page Fault (CPU pushes the error code)
ISR_NO_ERR 128  ; int 0x80 (system call, no error code)

IRQ  0, 32      ; PIT timer → IDT vector 32
IRQ  1, 33      ; PS/2 keyboard → IDT vector 33
```

### 5.2 Common stub — building `registers_t`

```nasm
; Stack layout on entry to isr_common_stub:
;   [esp+ 0]  interrupt number  (pushed by macro)
;   [esp+ 4]  error code        (CPU or dummy 0)
;   [esp+ 8]  eip               (CPU)
;   [esp+12]  cs                (CPU)
;   [esp+16]  eflags            (CPU)
;   [esp+20]  useresp           (CPU, only on Ring3→Ring0 transition)
;   [esp+24]  ss                (CPU, only on Ring3→Ring0 transition)

isr_common_stub:
    pusha               ; pushes: edi,esi,ebp,esp,ebx,edx,ecx,eax (8 regs)

    mov ax, ds
    push eax            ; save current DS

    mov ax, 0x10        ; switch to kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp            ; argument: pointer to registers_t on stack
    call isr_handler    ; C function in isr.c
    add esp, 4          ; clean up argument

    pop eax             ; restore original DS
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa                ; restore general-purpose registers
    add esp, 8          ; skip int_no and err_code
    iret                ; pops eip, cs, eflags (and useresp, ss if Ring3)
```

### 5.3 `registers_t` layout in C

The struct in `arch/idt.h` must exactly mirror what the stubs push:

```c
typedef struct {
    // Pushed by pusha (in reverse order: pusha pushes edi first, eax last)
    uint32_t edi, esi, ebp, esp_dummy;   // esp_dummy = ESP at pusha time (not useful)
    uint32_t ebx, edx, ecx, eax;

    // Pushed by stub
    uint32_t int_no;    // interrupt/exception number
    uint32_t err_code;  // error code (0 for non-error exceptions)

    // Pushed by CPU
    uint32_t eip, cs, eflags;

    // Pushed by CPU ONLY on Ring3→Ring0 transition
    uint32_t useresp, ss;
} PACKED registers_t;
```

**Example**: reading registers in the page fault handler:

```c
// In isr_handler():
if (regs->int_no == 14) {   // Page Fault
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    kprintf("Page fault at EIP=0x%x accessing 0x%x\n", regs->eip, cr2);
    // cr2 = the virtual address that caused the fault
}
```

---

## 6. PIC — Programmable Interrupt Controller

### 6.1 Why remap?

The 8259A PIC manages hardware IRQs 0–15.  By default, the BIOS programs the
master PIC to generate INT 8–15 and the slave to generate INT 70h–77h.
**INT 8–15 overlap with CPU exception vectors 8–15** (Double Fault, Invalid
TSS, etc.).  We must remap IRQs to non-conflicting vectors.

Our remapping: **master PIC → INT 32–39**, **slave PIC → INT 40–47**.

### 6.2 Initialisation sequence (ICW1–ICW4)

```c
// arch/irq.c
void pic_remap(void) {
    // ICW1: start initialisation, expect ICW4
    outb(PIC1_CMD,  0x11);  io_wait();   // 0x11 = INIT | ICW4
    outb(PIC2_CMD,  0x11);  io_wait();

    // ICW2: new vector base (the INT number for IRQ0 / IRQ8)
    outb(PIC1_DATA, 0x20);  io_wait();   // master: IRQ0 → INT 32
    outb(PIC2_DATA, 0x28);  io_wait();   // slave:  IRQ8 → INT 40

    // ICW3: cascade connection
    outb(PIC1_DATA, 0x04);  io_wait();   // master: slave on IRQ2 line
    outb(PIC2_DATA, 0x02);  io_wait();   // slave:  cascade identity = 2

    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01);  io_wait();
    outb(PIC2_DATA, 0x01);  io_wait();

    // Unmask all IRQ lines (0x00 = all unmasked)
    // NOTE: do NOT restore BIOS masks — GRUB may leave IRQ0 masked!
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}
```

**Why not restore BIOS masks?** GRUB can leave the PIC Interrupt Mask Register
(IMR) with IRQ0 (timer) masked (bit 0 set in PIC1_DATA).  If we restore BIOS
masks, the timer never fires and the scheduler never runs.  Explicitly writing
0x00 ensures all 16 IRQ lines are unmasked.

### 6.3 End-of-Interrupt (EOI)

After handling an IRQ, we must tell the PIC the interrupt is done.  Without EOI
the PIC holds the interrupt line asserted and no future IRQs of that priority or
lower can fire.

```c
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);  // Slave EOI (for IRQs 8–15)
    }
    outb(PIC1_CMD, 0x20);      // Master EOI (always)
}
```

**Critical ordering**: EOI must be sent **before** calling the IRQ handler:

```c
void irq_handler(registers_t *regs) {
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    pic_send_eoi(irq);   // ← FIRST: re-arm PIC

    if (irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq](regs);   // ← THEN: call handler
    }
}
```

**Why this order?**  The `scheduler_tick` handler calls `context_switch()`.
`context_switch` jumps to a different process's stack and that process resumes
via `ret` — the original `irq_handler` frame is **never returned to**.  If EOI
were called after `irq_handlers[irq](regs)`, the PIC would never be re-armed
and all future timer interrupts would be lost.

---

## 7. Exception Handler (isr.c)

```c
static const char *exception_names[] = {
    "Divide by Zero",         //  0
    "Debug",                  //  1
    // ...
    "Page Fault",             // 14
    // ...
};

void isr_handler(registers_t *regs) {
    // int 0x80 routes through isr_common_stub — check for syscall first
    if (regs->int_no == 0x80) {
        syscall_handler(regs);
        return;
    }

    // Print exception in red
    vga_set_color(vga_make_color(VGA_COLOR_RED, VGA_COLOR_BLACK));
    kprintf("*** KERNEL EXCEPTION ***\n");
    kprintf("Exception %u: %s\n", regs->int_no, exception_names[regs->int_no]);
    kprintf("Error code: 0x%x\n", regs->err_code);

    // Page fault: CR2 = faulting virtual address
    if (regs->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("Faulting address (CR2): 0x%x\n", cr2);
    }

    // Register dump
    kprintf("eax=0x%x  ebx=0x%x  ecx=0x%x  edx=0x%x\n",
            regs->eax, regs->ebx, regs->ecx, regs->edx);
    kprintf("eip=0x%x  cs=0x%x  eflags=0x%x\n",
            regs->eip, regs->cs, regs->eflags);

    khalt();   // halt — exception recovery not implemented
}
```

**Exception table (32 vectors)**:

| Vector | Name | Error Code? |
|--------|------|------------|
| 0 | Divide by Zero | No |
| 1 | Debug | No |
| 2 | Non-Maskable Interrupt | No |
| 3 | Breakpoint | No |
| 6 | Invalid Opcode | No |
| 8 | Double Fault | Yes (always 0) |
| 10 | Invalid TSS | Yes |
| 11 | Segment Not Present | Yes |
| 12 | Stack-Segment Fault | Yes |
| 13 | General Protection Fault | Yes |
| 14 | **Page Fault** | Yes (error flags) |
| 16 | x87 Floating-Point | No |
| 17 | Alignment Check | Yes |

---

## 8. IRQ Handler (irq.c)

```c
static irq_handler_t irq_handlers[16];   // function pointers

void irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq < 16)
        irq_handlers[irq] = handler;
}
```

How subsystems register handlers:

```c
// In timer initialisation (kmain):
irq_register(IRQ_TIMER,    scheduler_tick);   // IRQ0 → timer at 100 Hz

// In keyboard initialisation (keyboard.c):
irq_register(IRQ_KEYBOARD, keyboard_irq_handler); // IRQ1 → scancode reader
```

Where `IRQ_TIMER = 0` and `IRQ_KEYBOARD = 1` are defined in `arch/irq.h`.

---

## 9. End-to-End Interrupt Flow

### 9.1 Hardware IRQ (timer, every 10ms)

```
PIT chip: fires IRQ0
  │
  ▼ CPU hardware
  Privilege check: always taken (hardware interrupt, no DPL check)
  Push SS, ESP (if Ring3→Ring0 transition), EFLAGS, CS, EIP on kernel stack
  Load CS:EIP from IDT[32]  (vector 32 = IRQ0, set by irq_init)
  │
  ▼ irq0 stub (idt.asm)
  push 0          ; dummy error code
  push 32         ; IDT vector number (int_no)
  jmp irq_common_stub
  │
  ▼ irq_common_stub (idt.asm)
  pusha           ; save edi,esi,ebp,esp,ebx,edx,ecx,eax
  push ds
  mov ds, 0x10    ; switch to kernel data segment
  push esp        ; pointer to registers_t
  call irq_handler
  │
  ▼ irq_handler (irq.c)
  pic_send_eoi(0)           ; re-arm PIC FIRST
  irq_handlers[0](regs)    ; call scheduler_tick
  │
  ▼ scheduler_tick (scheduler.c)
  timer_tick()             ; increment tick counter
  [find next READY process]
  tss_set_kernel_stack(next->kernel_stack_top)
  context_switch(prev, next)
        │
        ▼  [if next is a new Ring-3 process: rets to proc_iret_trampoline]
        IRET → Ring 3 → shell continues executing
        │
        ▼  [if switching between kernel threads]
        rets into kthread_entry → sti → call entry_fn()
```

### 9.2 System call (`int 0x80`)

```
Shell (Ring 3):
  mov eax, 2    ; SYS_WRITE
  mov ebx, 1    ; fd = stdout
  mov ecx, buf  ; buffer address
  mov edx, 13   ; length
  int 0x80
  │
  ▼ CPU hardware
  DPL check: IDT[0x80] has DPL=3 → Ring-3 code CAN call this gate  ✓
  Push SS, ESP, EFLAGS, CS, EIP on kernel stack (tss.esp0)
  Clear IF (interrupt gate)
  Load CS:EIP from IDT[128]
  │
  ▼ isr128 stub (idt.asm)
  push 0          ; dummy error code
  push 128        ; int_no
  jmp isr_common_stub
  │
  ▼ isr_common_stub → isr_handler
  regs->int_no == 0x80?  YES → syscall_handler(regs)
  │
  ▼ syscall_handler (syscall.c)
  switch(regs->eax):
    case SYS_WRITE:
      sys_write(regs->ebx, (char*)regs->ecx, regs->edx)
      → vga_putchar() for each character
      regs->eax = bytes_written   ← return value
  │
  ▼ isr_common_stub resumes
  popa
  add esp, 8   (skip int_no + err_code)
  IRET → Ring 3 (restores EIP, CS, EFLAGS, ESP, SS)
  │
  shell sees return value in EAX
```

### 9.3 CPU Exception (page fault during kernel init)

```
Kernel accesses unmapped address 0xDEADBEEF:
  CPU:
    Push EFLAGS, CS, EIP
    Push error code (page fault error flags)
    Load CS:EIP from IDT[14]
    │
  isr14 stub:
    ; error code already on stack
    push 14        ; int_no
    jmp isr_common_stub
    │
  isr_handler(regs):
    regs->int_no == 14
    Read CR2 (= 0xDEADBEEF)
    kprintf("Page Fault at 0xDEADBEEF")
    khalt()   → CPU halts forever
```
