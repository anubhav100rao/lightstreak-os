/*
 * kernel/arch/idt.c — Interrupt Descriptor Table
 *
 * Sets up all 256 IDT gates:
 *   0–31:   CPU exceptions → isr_handler()
 *   32–47:  Hardware IRQs  → irq_handler()
 *   48–255: Unused (pointing to a generic stub)
 *   0x80:   System call gate (ring 3 callable, set later by syscall_init)
 */

#include "idt.h"
#include "irq.h"
#include "isr.h"

#define IDT_ENTRIES 256

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idtr;

/*
 * Gate type/attribute byte for a 32-bit interrupt gate:
 *   bit 7   Present
 *   [6:5]   DPL (00=ring0 callable, 11=ring3 callable)
 *   bit 4   0 (not a storage segment)
 *   [3:0]   Gate type: 0xE = 32-bit interrupt gate
 *                       0xF = 32-bit trap gate
 *
 * Interrupt gate (0xE): clears IF on entry (interrupts disabled while handling)
 * Trap gate     (0xF): does NOT clear IF (used for syscall 0x80)
 */
#define IDT_GATE_INT_RING0 0x8E /* P=1 DPL=0 type=0xE */
#define IDT_GATE_INT_RING3 0xEE /* P=1 DPL=3 type=0xE (ring-3 callable) */

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags) {
  idt[num].offset_low = (uint16_t)(handler & 0xFFFF);
  idt[num].selector = sel;
  idt[num].zero = 0;
  idt[num].type_attr = flags;
  idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void) {
  idtr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
  idtr.base = (uint32_t)&idt;

  /* Zero all entries first */
  uint8_t *p = (uint8_t *)idt;
  for (int i = 0; i < (int)sizeof(idt); i++)
    p[i] = 0;

  /* CPU exception stubs (ISR 0–31) — ring 0 only */
  idt_set_gate(0, (uint32_t)isr0, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(1, (uint32_t)isr1, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(2, (uint32_t)isr2, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(3, (uint32_t)isr3, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(4, (uint32_t)isr4, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(5, (uint32_t)isr5, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(6, (uint32_t)isr6, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(7, (uint32_t)isr7, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(8, (uint32_t)isr8, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(9, (uint32_t)isr9, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(10, (uint32_t)isr10, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(11, (uint32_t)isr11, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(12, (uint32_t)isr12, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(13, (uint32_t)isr13, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(14, (uint32_t)isr14, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(15, (uint32_t)isr15, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(16, (uint32_t)isr16, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(17, (uint32_t)isr17, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(18, (uint32_t)isr18, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(19, (uint32_t)isr19, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(20, (uint32_t)isr20, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(21, (uint32_t)isr21, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(22, (uint32_t)isr22, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(23, (uint32_t)isr23, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(24, (uint32_t)isr24, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(25, (uint32_t)isr25, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(26, (uint32_t)isr26, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(27, (uint32_t)isr27, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(28, (uint32_t)isr28, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(29, (uint32_t)isr29, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(30, (uint32_t)isr30, 0x08, IDT_GATE_INT_RING0);
  idt_set_gate(31, (uint32_t)isr31, 0x08, IDT_GATE_INT_RING0);

  /* System call gate — int 0x80, Ring 3 callable (DPL=3) */
  idt_set_gate(0x80, (uint32_t)isr128, 0x08, IDT_GATE_INT_RING3);

  /* Hardware IRQ stubs (32–47) */
  irq_init();

  /* Load the IDT register */
  idt_flush(&idtr);
}
