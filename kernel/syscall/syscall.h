#ifndef SYSCALL_SYSCALL_H
#define SYSCALL_SYSCALL_H

/*
 * kernel/syscall/syscall.h — System call interface
 *
 * syscall_init() is called from kmain() to announce readiness.
 * syscall_handler() is invoked by isr_handler() when int_no == 0x80.
 */

#include "../../include/types.h"
#include "../arch/idt.h"   /* registers_t */

/* Initialise the syscall subsystem (logging only — IDT gate set in idt_init) */
void syscall_init(void);

/* Dispatch a system call.  Called from isr_handler() for int 0x80 */
void syscall_handler(registers_t *regs);

#endif /* SYSCALL_SYSCALL_H */
