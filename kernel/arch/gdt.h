#ifndef ARCH_GDT_H
#define ARCH_GDT_H

#include "../../include/types.h"

/*
 * GDT segment selectors (byte offset into GDT × RPL bits).
 * Ring 0 segments have RPL = 00, ring 3 segments have RPL = 11 (0x3).
 */
#define GDT_KERNEL_CODE  0x08   /* Selector for kernel code segment */
#define GDT_KERNEL_DATA  0x10   /* Selector for kernel data segment */
#define GDT_USER_CODE    0x1B   /* Selector for user code  (0x18 | RPL 3) */
#define GDT_USER_DATA    0x23   /* Selector for user data  (0x20 | RPL 3) */
#define GDT_TSS_SEG      0x28   /* Selector for TSS descriptor */

/* GDT entry (8 bytes) — the hardware format */
typedef struct {
    uint16_t limit_low;     /* Bits [15:0]  of segment limit */
    uint16_t base_low;      /* Bits [15:0]  of base address  */
    uint8_t  base_mid;      /* Bits [23:16] of base address  */
    uint8_t  access;        /* Access byte (type, DPL, present) */
    uint8_t  granularity;   /* Flags [7:4] + limit [19:16] in [3:0] */
    uint8_t  base_high;     /* Bits [31:24] of base address  */
} PACKED gdt_entry_t;

/* GDTR — the value loaded with lgdt */
typedef struct {
    uint16_t limit;         /* Size of GDT in bytes minus 1 */
    uint32_t base;          /* Linear address of the GDT */
} PACKED gdt_ptr_t;

/* Initialise the GDT (and TSS descriptor within it) + flush CPU registers */
void gdt_init(void);

/* Set a single GDT entry (used internally and by TSS setup) */
void gdt_set_entry(int num, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t gran);

/* Assembly routine that loads the GDTR and far-jumps to reload CS */
extern void gdt_flush(gdt_ptr_t *gdtr);

/* Called by tss_init() to write the TSS descriptor into GDT slot 5 */
void gdt_set_tss_entry(uint32_t base, uint32_t limit);

#endif /* ARCH_GDT_H */
