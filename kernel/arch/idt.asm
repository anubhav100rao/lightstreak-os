; kernel/arch/idt.asm
;
; ISR and IRQ stubs in assembly.
;
; Why assembly stubs?
;   The CPU pushes only eip/cs/eflags (and optionally useresp/ss) on an
;   exception. We must save all general-purpose registers ourselves, push a
;   unified 'registers_t' frame on the stack, call the C handler, then
;   restore everything and return with 'iret'.
;
; Two kinds of stubs:
;   ISR stubs (0–31): CPU exceptions. Some push an error code; others don't.
;   IRQ stubs (32–47): Hardware interrupts remapped from PIC. No error code.
;
; Common stub pattern:
;   1. Push dummy error code (0) if the CPU didn't push one.
;   2. Push the interrupt number.
;   3. pusha — saves eax, ecx, edx, ebx, esp, ebp, esi, edi (8 regs).
;   4. Push ds.
;   5. Load kernel data segment into ds/es/fs/gs.
;   6. Call C handler (isr_common_handler or irq_common_handler).
;   7. Pop ds, popa, skip int_no+err_code on stack, iret.

; ---------------------------------------------------------------------------
; Common handlers (defined in isr.c and irq.c)
; ---------------------------------------------------------------------------
extern isr_handler
extern irq_handler

; ---------------------------------------------------------------------------
; idt_flush — loads the IDTR
; C prototype: void idt_flush(idt_ptr_t *idtr);
; ---------------------------------------------------------------------------
global idt_flush
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    ret

; ---------------------------------------------------------------------------
; Macros to generate ISR stubs
; ---------------------------------------------------------------------------

; ISR stub WITHOUT error code (CPU doesn't push one — we push 0 ourselves)
%macro ISR_NO_ERR 1
global isr%1
isr%1:
    push dword 0        ; dummy error code
    push dword %1       ; interrupt number
    jmp isr_common_stub
%endmacro

; ISR stub WITH error code (CPU already pushed one before eip)
%macro ISR_ERR 1
global isr%1
isr%1:
    ; error code already on stack from CPU
    push dword %1
    jmp isr_common_stub
%endmacro

; IRQ stub — no error code, shift vector by 32 to get IRQ number back
%macro IRQ 2
global irq%1
irq%1:
    push dword 0        ; no error code
    push dword %2       ; IDT vector (32+IRQ#)
    jmp irq_common_stub
%endmacro

; ---------------------------------------------------------------------------
; ISR stubs 0–31  (CPU exceptions)
; Exceptions that push an error code: 8, 10, 11, 12, 13, 14, 17, 21
; ---------------------------------------------------------------------------
ISR_NO_ERR  0   ; Divide by Zero
ISR_NO_ERR  1   ; Debug
ISR_NO_ERR  2   ; Non-Maskable Interrupt
ISR_NO_ERR  3   ; Breakpoint
ISR_NO_ERR  4   ; Overflow
ISR_NO_ERR  5   ; Bound Range Exceeded
ISR_NO_ERR  6   ; Invalid Opcode
ISR_NO_ERR  7   ; Device Not Available
ISR_ERR     8   ; Double Fault (error code = 0, but CPU still pushes it)
ISR_NO_ERR  9   ; Coprocessor Segment Overrun (legacy, never fires on modern HW)
ISR_ERR    10   ; Invalid TSS
ISR_ERR    11   ; Segment Not Present
ISR_ERR    12   ; Stack-Segment Fault
ISR_ERR    13   ; General Protection Fault
ISR_ERR    14   ; Page Fault
ISR_NO_ERR 15   ; Reserved
ISR_NO_ERR 16   ; x87 Floating-Point Exception
ISR_ERR    17   ; Alignment Check
ISR_NO_ERR 18   ; Machine Check
ISR_NO_ERR 19   ; SIMD Floating-Point Exception
ISR_NO_ERR 20   ; Virtualisation Exception
ISR_ERR    21   ; Control Protection Exception
ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28
ISR_NO_ERR 29
ISR_NO_ERR 30
ISR_NO_ERR 31

; ---------------------------------------------------------------------------
; System call stub — int 0x80 (vector 128)
; Uses isr_common_stub so it goes through isr_handler() in C, which routes
; int_no==128 to syscall_handler().
; ---------------------------------------------------------------------------
ISR_NO_ERR 128

; ---------------------------------------------------------------------------
; IRQ stubs 0–15 (hardware interrupts, remapped to IDT vectors 32–47)
; ---------------------------------------------------------------------------
IRQ  0, 32
IRQ  1, 33
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40
IRQ  9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; ---------------------------------------------------------------------------
; Common ISR stub
; Stack layout when we arrive here:
;   [esp+0]  interrupt number   (pushed by macro)
;   [esp+4]  error code         (pushed by CPU or macro dummy)
;   [esp+8]  eip                (pushed by CPU)
;   [esp+12] cs                 (pushed by CPU)
;   [esp+16] eflags             (pushed by CPU)
;   [esp+20] useresp            (pushed by CPU on privilege change)
;   [esp+24] ss                 (pushed by CPU on privilege change)
; ---------------------------------------------------------------------------
isr_common_stub:
    pusha               ; push edi,esi,ebp,esp,ebx,edx,ecx,eax
                        ; registers_t starts here (struct has no segment fields)

    mov ax, 0x10        ; load kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp            ; pass pointer to registers_t as argument
    call isr_handler
    add esp, 4          ; clean up argument

    popa                ; restore general-purpose registers
    add esp, 8          ; skip int_no and err_code
    iret                ; pop eip, cs, eflags (and useresp, ss on ring change)

; ---------------------------------------------------------------------------
; Common IRQ stub — identical structure, calls irq_handler instead
; ---------------------------------------------------------------------------
irq_common_stub:
    pusha

    mov ax, 0x10        ; load kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call irq_handler
    add esp, 4

    popa
    add esp, 8
    iret
