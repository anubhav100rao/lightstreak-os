#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include "idt.h"

/* Hardware IRQs are remapped to IDT vectors 32–47 after pic_remap() */
#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1

/* Remap the two 8259 PICs so IRQs 0-15 map to IDT entries 32-47 */
void pic_remap(void);

/* Send End-of-Interrupt to the PIC(s) */
void pic_send_eoi(uint8_t irq);

/* C handler called by irq_common_stub in idt.asm */
void irq_handler(registers_t *regs);

/* Register a handler for a specific IRQ line (0-15) */
typedef void (*irq_handler_t)(registers_t *);
void irq_register(uint8_t irq, irq_handler_t handler);

/* Initialise IRQ dispatch table + PIC */
void irq_init(void);

/* Forward declarations for the 16 IRQ stubs defined in idt.asm */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

#endif /* ARCH_IRQ_H */
