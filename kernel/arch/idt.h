#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include "../../include/types.h"

/*
 * Saved CPU state pushed by the ISR/IRQ stubs before calling C handlers.
 * Layout must exactly match the push order in idt.asm.
 */
typedef struct {
    /* Pushed by pusha */
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;

    /* Pushed by isr/irq stub */
    uint32_t int_no;    /* Interrupt/exception number */
    uint32_t err_code;  /* Error code (0 for most exceptions) */

    /* Pushed automatically by the CPU on exception */
    uint32_t eip, cs, eflags;

    /* Pushed by CPU only when privilege level changes (Ring 3 → Ring 0) */
    uint32_t useresp, ss;
} PACKED registers_t;

/* IDT gate descriptor (8 bytes) */
typedef struct {
    uint16_t offset_low;    /* Bits [15:0]  of handler address */
    uint16_t selector;      /* Code segment selector */
    uint8_t  zero;          /* Must be 0 */
    uint8_t  type_attr;     /* Gate type + DPL + present bit */
    uint16_t offset_high;   /* Bits [31:16] of handler address */
} PACKED idt_entry_t;

/* IDTR — the value loaded with lidt */
typedef struct {
    uint16_t limit;
    uint32_t base;
} PACKED idt_ptr_t;

/* Initialise the IDT, set all 256 gates, load with lidt */
void idt_init(void);

/* Set a single IDT gate */
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags);

/* Assembly: load IDTR */
extern void idt_flush(idt_ptr_t *idtr);

#endif /* ARCH_IDT_H */
