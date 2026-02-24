/*
 * kernel/arch/tss.c — Task State Segment
 *
 * We use a single, global TSS for the kernel. The only fields that
 * matter at runtime are:
 *   ss0  — kernel stack segment (always 0x10 = GDT_KERNEL_DATA)
 *   esp0 — kernel stack pointer (updated on every context switch)
 *
 * The TSS descriptor in GDT entry 5 is a 32-bit available TSS (type 0x9).
 * It is loaded into TR (task register) with the 'ltr' instruction.
 */

#include "tss.h"
#include "gdt.h"

/* The single global TSS instance */
static tss_entry_t tss;

/* Assembly helper: load TR with the TSS selector */
static void tss_flush(void) {
    __asm__ volatile ("ltr %%ax" : : "a"(GDT_TSS_SEG));
}

void tss_init(uint32_t kernel_stack) {
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = (uint32_t)(base + sizeof(tss_entry_t) - 1);

    /* Write TSS descriptor into GDT entry 5 */
    gdt_set_tss_entry(base, limit);

    /* Zero the whole TSS, then set the fields we care about */
    uint8_t *p = (uint8_t *)&tss;
    for (size_t i = 0; i < sizeof(tss_entry_t); i++) p[i] = 0;

    tss.ss0  = GDT_KERNEL_DATA;
    tss.esp0 = kernel_stack;

    /* iomap_base beyond the TSS limit disables the I/O permission bitmap */
    tss.iomap_base = (uint16_t)sizeof(tss_entry_t);

    /* Load TR */
    tss_flush();
}

void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}
