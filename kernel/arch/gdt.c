/*
 * kernel/arch/gdt.c — Global Descriptor Table
 *
 * GDT layout (6 entries):
 *   0: Null descriptor      (required by spec)
 *   1: Kernel code  (ring 0, execute/read)   → selector 0x08
 *   2: Kernel data  (ring 0, read/write)     → selector 0x10
 *   3: User code    (ring 3, execute/read)   → selector 0x1B (0x18 | RPL 3)
 *   4: User data    (ring 3, read/write)     → selector 0x23 (0x20 | RPL 3)
 *   5: TSS          (set by tss_init)        → selector 0x28
 *
 * All code/data segments use base=0, limit=4GB (flat model).
 * Memory protection is handled by paging, not segmentation.
 */

#include "gdt.h"
#include "tss.h"

#define GDT_ENTRIES 6

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdtr;

/*
 * Access byte:
 *   bit 7   Present
 *   [6:5]   DPL (privilege level)
 *   bit 4   Descriptor type (1=code/data segment)
 *   bit 3   Executable
 *   bit 2   Direction/Conforming
 *   bit 1   Readable(code)/Writable(data)
 *   bit 0   Accessed (leave 0, CPU sets it)
 *
 * Granularity byte:
 *   bit 7   Granularity (1=4KB)
 *   bit 6   Size (1=32-bit)
 *   bits[3:0] Limit[19:16]
 */
void gdt_set_entry(int num, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t gran) {
    gdt[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[num].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].access      = access;
    gdt[num].granularity = (uint8_t)((limit >> 16) & 0x0F) | gran;
    gdt[num].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void) {
    /* 0: Null — required, all zeros */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* 1: Kernel code — ring 0, execute+read, 4GB flat */
    gdt_set_entry(1, 0, 0xFFFFF,
                  0x9A,   /* P=1 DPL=0 S=1 Type=0xA (exec/read) */
                  0xCF);  /* G=1 D=1 limit[19:16]=F */

    /* 2: Kernel data — ring 0, read/write, 4GB flat */
    gdt_set_entry(2, 0, 0xFFFFF,
                  0x92,   /* P=1 DPL=0 S=1 Type=0x2 (read/write) */
                  0xCF);

    /* 3: User code — ring 3, execute+read, 4GB flat */
    gdt_set_entry(3, 0, 0xFFFFF,
                  0xFA,   /* P=1 DPL=3 S=1 Type=0xA */
                  0xCF);

    /* 4: User data — ring 3, read/write, 4GB flat */
    gdt_set_entry(4, 0, 0xFFFFF,
                  0xF2,   /* P=1 DPL=3 S=1 Type=0x2 */
                  0xCF);

    /* 5: TSS placeholder — tss_init() fills this in */
    gdt_set_entry(5, 0, 0, 0, 0);

    gdtr.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    gdtr.base  = (uint32_t)&gdt;

    /* Load GDT register and reload segment registers */
    gdt_flush(&gdtr);
}

/* Called by tss_init() to write the TSS system descriptor into GDT entry 5 */
void gdt_set_tss_entry(uint32_t base, uint32_t limit) {
    gdt_set_entry(5, base, limit,
                  0x89,   /* P=1 DPL=0 S=0 Type=9 (32-bit available TSS) */
                  0x00);  /* byte granularity, no size flag for TSS */
}
