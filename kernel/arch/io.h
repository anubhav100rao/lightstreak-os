#ifndef ARCH_IO_H
#define ARCH_IO_H

/*
 * kernel/arch/io.h — x86 port I/O helpers
 *
 * outb / inb use the x86 'out' and 'in' instructions to communicate
 * with hardware via I/O ports (a separate address space from RAM).
 * All functions are inlined to avoid call overhead in hot paths
 * like interrupt handlers.
 */

#include "../../include/types.h"

/* Write a byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write a word (2 bytes) to an I/O port */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a word from an I/O port */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*
 * I/O wait — writes to an unused port (0x80) to give slow ISA
 * hardware time to react. Used after PIC / PIT programming.
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* ARCH_IO_H */
