/*
 * kernel/arch/irq.c — Hardware interrupt (IRQ) handler + PIC setup
 *
 * The 8259A PIC manages IRQs 0–15. By default they overlap with CPU
 * exception vectors 0–15. We remap them to IDT vectors 32–47 to avoid
 * that conflict, then register them in the IDT.
 */

#include "irq.h"
#include "idt.h"
#include "../arch/io.h"
#include "../kernel.h"

/* PIC I/O ports */
#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

/* Initialization Command Words */
#define PIC_ICW1_INIT  0x11   /* Initialize + expect ICW4 */
#define PIC_ICW4_8086  0x01   /* 8086/88 mode */

#define PIC_EOI        0x20   /* End-of-Interrupt command */

/* Table of registered IRQ handlers */
static irq_handler_t irq_handlers[16];

/* -------------------------------------------------------------------------
 * pic_remap — remap PIC IRQs 0-15 to IDT vectors 32-47
 *
 * NOTE: We do NOT restore the pre-remap masks here.  GRUB (and some BIOSes)
 * can leave IRQs masked, which would silently block the timer.  Instead we
 * explicitly unmask all 16 IRQ lines after the remap so every registered
 * handler can fire.  Individual subsystems may add masking later.
 * ---------------------------------------------------------------------- */
void pic_remap(void) {
    /* Start initialisation sequence (ICW1) */
    outb(PIC1_CMD,  PIC_ICW1_INIT); io_wait();
    outb(PIC2_CMD,  PIC_ICW1_INIT); io_wait();

    /* ICW2: new vector base offsets */
    outb(PIC1_DATA, 0x20); io_wait();   /* Master: IRQs 0-7  → INT 32-39 */
    outb(PIC2_DATA, 0x28); io_wait();   /* Slave:  IRQs 8-15 → INT 40-47 */

    /* ICW3: cascade wiring */
    outb(PIC1_DATA, 0x04); io_wait();   /* Master: slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();   /* Slave:  cascade identity = 2 */

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, PIC_ICW4_8086); io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086); io_wait();

    /* Unmask all IRQ lines (0x00 = no bits masked).
     * This is safer than restoring BIOS masks which may have IRQ0 blocked. */
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);   /* Slave PIC also needs EOI */
    }
    outb(PIC1_CMD, PIC_EOI);
}

/* -------------------------------------------------------------------------
 * irq_handler — called from irq_common_stub in idt.asm
 *
 * IMPORTANT: EOI must be sent BEFORE calling the handler.
 * If the handler is the scheduler and it performs a context_switch(),
 * it will never return here — the new process resumes directly from its
 * saved stack via 'ret'.  Sending EOI first ensures the PIC is re-armed
 * for the next timer tick regardless of whether we context-switch.
 * ---------------------------------------------------------------------- */
void irq_handler(registers_t *regs) {
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    /* Re-arm the PIC before dispatching so IRQs keep firing after a
     * context switch (which bypasses the normal return path). */
    pic_send_eoi(irq);

    if (irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq](regs);
    }
}

void irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

/* -------------------------------------------------------------------------
 * irq_init — remap PIC + install IRQ gates in IDT
 * ---------------------------------------------------------------------- */
void irq_init(void) {
    /* Zero handler table */
    for (int i = 0; i < 16; i++) irq_handlers[i] = (irq_handler_t)0;

    pic_remap();

    /* Install IRQ stubs into IDT at vectors 32–47 */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    /* Enable hardware interrupts */
    __asm__ volatile ("sti");
}
