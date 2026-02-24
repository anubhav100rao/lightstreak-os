#ifndef ARCH_TSS_H
#define ARCH_TSS_H

#include "../../include/types.h"

/*
 * Task State Segment — the CPU reads this when switching privilege levels.
 *
 * When a Ring-3 process executes 'int 0x80' (or any interrupt), the CPU
 * automatically switches to Ring 0 and needs to know which stack to use.
 * It reads that from tss.esp0 (kernel stack pointer) and tss.ss0 (kernel
 * stack segment = 0x10 = GDT_KERNEL_DATA).
 *
 * We only need one TSS for the whole kernel. Its esp0 field is updated
 * on every context switch to point to the incoming process's kernel stack.
 *
 * All other fields are zeroed and unused in our simple implementation.
 */
typedef struct {
    uint32_t prev_tss;  /* Back-link to previous TSS (unused) */
    uint32_t esp0;      /* Ring-0 stack pointer — updated on context switch */
    uint32_t ss0;       /* Ring-0 stack segment  — always GDT_KERNEL_DATA   */
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} PACKED tss_entry_t;

/* Initialise the TSS and load it via 'ltr' */
void tss_init(uint32_t kernel_stack);

/*
 * Update the kernel-stack pointer in the TSS.
 * Called on every context switch so each process gets the right kernel stack
 * when an interrupt fires while it is running in Ring 3.
 */
void tss_set_kernel_stack(uint32_t stack);

#endif /* ARCH_TSS_H */
