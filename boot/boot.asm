; boot/boot.asm
; Multiboot-compliant kernel entry point.
; GRUB loads this, sets up the Multiboot info struct, and jumps here.
;
; Register contract on entry (Multiboot spec):
;   EAX = 0x2BADB002  (Multiboot magic — proves GRUB loaded us)
;   EBX = physical address of multiboot_info_t struct
;
; We set up a stack, push both values for kmain(), and call it.

; ---------------------------------------------------------------------------
; Multiboot header constants
; ---------------------------------------------------------------------------
MBOOT_MAGIC    equ 0x1BADB002   ; Magic number GRUB looks for
MBOOT_ALIGN    equ 1 << 0       ; Align loaded modules on page boundaries
MBOOT_MEMINFO  equ 1 << 1       ; Provide memory map
MBOOT_FLAGS    equ MBOOT_ALIGN | MBOOT_MEMINFO
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)   ; Must sum to 0 mod 2^32

; ---------------------------------------------------------------------------
; Multiboot header — must appear in the first 8KB of the kernel image
; Linker script places .multiboot first so GRUB finds it immediately.
; ---------------------------------------------------------------------------
section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

; ---------------------------------------------------------------------------
; Kernel stack — 16KB, in .bss (not in the binary, zeroed at load time)
; ---------------------------------------------------------------------------
section .bss
align 16
stack_bottom:
    resb 16384          ; 16 KB kernel stack
global stack_top
stack_top:

; ---------------------------------------------------------------------------
; Kernel entry point
; ---------------------------------------------------------------------------
section .text
global boot_start       ; Linker script ENTRY() references this
extern kmain            ; Defined in kernel/kernel.c

boot_start:
    ; Point the stack pointer at the top of our reserved stack region.
    ; x86 stacks grow downward, so stack_top is the initial esp.
    mov esp, stack_top

    ; Push arguments for kmain(uint32_t magic, multiboot_info_t *mbi).
    ; cdecl: arguments pushed right-to-left.
    push ebx            ; arg1: pointer to multiboot_info_t
    push eax            ; arg0: Multiboot magic (0x2BADB002)

    ; Transfer control to the C kernel entry point.
    call kmain

    ; kmain should never return. If it does, halt the CPU in a loop.
.hang:
    cli                 ; Disable interrupts
    hlt                 ; Halt until next interrupt (which won't come)
    jmp .hang           ; In case of NMI, loop back
