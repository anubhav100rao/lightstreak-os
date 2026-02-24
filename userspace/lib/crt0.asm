; userspace/lib/crt0.asm — C runtime startup stub
;
; This MUST be the first object file linked so it sits at offset 0
; of the flat binary. When the kernel jumps to 0x400000, execution
; starts here, which calls _start in shell.c.
;
; If _start() ever returns, we invoke SYS_EXIT(0).

section .text
global _entry
extern _start

_entry:
    call _start         ; call the real entry point in C

    ; _start should never return, but just in case:
    mov eax, 1          ; SYS_EXIT
    xor ebx, ebx        ; exit code 0
    int 0x80
    jmp $               ; hang if exit fails
