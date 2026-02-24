#ifndef KERNEL_H
#define KERNEL_H

#include "../include/types.h"

/* Kernel printf — supports %s %d %u %x %c %% */
void kprintf(const char *fmt, ...);

/* Serial debug output (port 0xE9 QEMU hack) — visible with -debugcon stdio */
void debug_print(const char *str);

/* Halt the CPU permanently */
void NORETURN khalt(void);

#endif /* KERNEL_H */
