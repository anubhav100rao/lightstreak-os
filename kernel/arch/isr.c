/*
 * kernel/arch/isr.c — CPU exception handler
 *
 * isr_handler() is called from the common ISR stub in idt.asm.
 * It prints the exception name, register state, and halts.
 */

#include "isr.h"
#include "../drivers/vga.h"
#include "../kernel.h"
#include "../syscall/syscall.h"

static const char *exception_names[] = {
    "Divide by Zero",              /*  0 */
    "Debug",                       /*  1 */
    "Non-Maskable Interrupt",      /*  2 */
    "Breakpoint",                  /*  3 */
    "Overflow",                    /*  4 */
    "Bound Range Exceeded",        /*  5 */
    "Invalid Opcode",              /*  6 */
    "Device Not Available",        /*  7 */
    "Double Fault",                /*  8 */
    "Coprocessor Segment Overrun", /* 9 */
    "Invalid TSS",                 /* 10 */
    "Segment Not Present",         /* 11 */
    "Stack-Segment Fault",         /* 12 */
    "General Protection Fault",    /* 13 */
    "Page Fault",                  /* 14 */
    "Reserved",                    /* 15 */
    "x87 Floating-Point",          /* 16 */
    "Alignment Check",             /* 17 */
    "Machine Check",               /* 18 */
    "SIMD Floating-Point",         /* 19 */
    "Virtualisation",              /* 20 */
    "Control Protection",          /* 21 */
    "Reserved",                    /* 22 */
    "Reserved",                    /* 23 */
    "Reserved",                    /* 24 */
    "Reserved",                    /* 25 */
    "Reserved",                    /* 26 */
    "Reserved",                    /* 27 */
    "Reserved",                    /* 28 */
    "Reserved",                    /* 29 */
    "Reserved",                    /* 30 */
    "Reserved",                    /* 31 */
};

void isr_handler(registers_t *regs) {
  /* int 0x80 → system call, not a CPU exception */
  if (regs->int_no == 0x80) {
    syscall_handler(regs);
    return;
  }

  debug_print("[serial] EXCEPTION caught\n");
  /* Print exception header in red */
  vga_set_color(vga_make_color(VGA_COLOR_RED, VGA_COLOR_BLACK));

  const char *name =
      (regs->int_no < 32) ? exception_names[regs->int_no] : "Unknown";

  kprintf("\n*** KERNEL EXCEPTION ***\n");
  kprintf("Exception %u: %s\n", regs->int_no, name);
  kprintf("Error code: 0x%x\n", regs->err_code);

  /* For page faults (14), CR2 holds the faulting virtual address */
  if (regs->int_no == 14) {
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    kprintf("Faulting address (CR2): 0x%x\n", cr2);
  }

  kprintf("\nRegisters:\n");
  kprintf("  eax=0x%x  ebx=0x%x  ecx=0x%x  edx=0x%x\n", regs->eax, regs->ebx,
          regs->ecx, regs->edx);
  kprintf("  esi=0x%x  edi=0x%x  ebp=0x%x\n", regs->esi, regs->edi, regs->ebp);
  kprintf("  eip=0x%x  cs=0x%x  eflags=0x%x\n", regs->eip, regs->cs,
          regs->eflags);

  vga_set_color(vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
  kprintf("\nSystem halted.\n");

  khalt();
}
