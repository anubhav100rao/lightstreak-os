; kernel/arch/gdt.asm
;
; gdt_flush — loads the GDTR and performs a far jump to reload CS.
;
; C prototype: void gdt_flush(gdt_ptr_t *gdtr);
; Calling convention: cdecl — argument is on the stack at [esp+4].
;
; Why a far jump?
;   lgdt only updates the GDTR register. The CPU keeps using the old CS
;   (code segment) value cached in an internal shadow register until a
;   far jump or far call forces it to reload CS from the new GDT.
;   A far jump has the form: jmp selector:offset
;   We jump to 0x08:flush_cs (selector 0x08 = kernel code segment).

section .text
global gdt_flush

gdt_flush:
    mov eax, [esp+4]    ; eax = pointer to gdt_ptr_t
    lgdt [eax]          ; Load new GDT register

    ; Far jump to reload CS with the kernel code segment selector (0x08)
    jmp 0x08:.flush_cs

.flush_cs:
    ; Reload all data segment registers with kernel data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
