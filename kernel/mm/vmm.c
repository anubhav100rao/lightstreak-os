/*
 * kernel/mm/vmm.c — Virtual Memory Manager
 *
 * Strategy:
 *   1. Allocate a kernel page directory (physical page from PMM).
 *   2. Identity-map the first 8 MB (virtual == physical) using two page
 *      tables (entries 0 and 1 in the page directory).
 *      This covers: BIOS data, kernel image, VGA buffer (0xB8000), and
 *      plenty of heap space.
 *   3. Load the kernel page directory into CR3.
 *   4. Enable paging by setting bit 31 of CR0.
 *
 * After paging is enabled, all existing pointers still work because
 * virtual addresses 0x000000–0x7FFFFF map directly to the same
 * physical addresses.
 *
 * Page fault handler:
 *   ISR 14 is already wired to isr_handler() which reads CR2 and prints
 *   the faulting address.  A separate vmm_page_fault_handler() is not
 *   needed at this stage — the generic exception handler is sufficient.
 */

#include "vmm.h"
#include "pmm.h"
#include "../kernel.h"

/* The kernel's root page directory (statically allocated, 4KB aligned) */
static page_directory_t kernel_dir ALIGNED(4096);

/* Two page tables to cover the first 8 MB identity mapping */
static page_table_t identity_pt0 ALIGNED(4096);  /* 0x00000000–0x003FFFFF */
static page_table_t identity_pt1 ALIGNED(4096);  /* 0x00400000–0x007FFFFF */

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void vmm_switch_directory(page_directory_t *dir) {
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"((uint32_t)dir)
        : "memory"
    );
}

page_directory_t *vmm_get_kernel_dir(void) {
    return &kernel_dir;
}

void vmm_map_page(page_directory_t *dir, uint32_t virt,
                  uint32_t phys, uint32_t flags) {
    uint32_t di = VMM_DIR_IDX(virt);
    uint32_t ti = VMM_TBL_IDX(virt);

    /* Get or create the page table for this directory entry */
    page_table_t *pt;
    if (!(dir->entries[di] & PAGE_PRESENT)) {
        /* Allocate a new page table */
        pt = (page_table_t *)pmm_alloc_page();
        if (!pt) {
            kprintf("[VMM] ERROR: pmm_alloc_page() returned NULL in vmm_map_page\n");
            return;
        }
        /* Zero the new page table */
        for (int i = 0; i < 1024; i++) pt->entries[i] = 0;
        dir->entries[di] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        pt = (page_table_t *)(dir->entries[di] & PAGE_MASK);
    }

    pt->entries[ti] = (phys & PAGE_MASK) | (flags & 0xFFF) | PAGE_PRESENT;

    /* Invalidate the TLB entry for this virtual address */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(page_directory_t *dir, uint32_t virt) {
    uint32_t di = VMM_DIR_IDX(virt);
    uint32_t ti = VMM_TBL_IDX(virt);

    if (!(dir->entries[di] & PAGE_PRESENT)) return;

    page_table_t *pt = (page_table_t *)(dir->entries[di] & PAGE_MASK);
    pt->entries[ti] = 0;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_init(void) {
    /* ---------------------------------------------------------------
     * Build two page tables that identity-map the first 8 MB.
     * Page table 0 → virtual 0x00000000–0x003FFFFF (4MB)
     * Page table 1 → virtual 0x00400000–0x007FFFFF (4MB)
     * --------------------------------------------------------------- */
    for (int i = 0; i < 1024; i++) {
        /* R/W present mapping */
        identity_pt0.entries[i] = (uint32_t)(i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }
    for (int i = 0; i < 1024; i++) {
        identity_pt1.entries[i] = (uint32_t)((1024 + i) * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }

    /* Zero the kernel directory */
    for (int i = 0; i < 1024; i++) kernel_dir.entries[i] = 0;

    /* Install the two page tables into the directory */
    kernel_dir.entries[0] = (uint32_t)&identity_pt0 | PAGE_PRESENT | PAGE_WRITE;
    kernel_dir.entries[1] = (uint32_t)&identity_pt1 | PAGE_PRESENT | PAGE_WRITE;

    /* Load CR3 with the page directory physical address */
    vmm_switch_directory(&kernel_dir);

    /* Enable paging: set bit 31 (PG) of CR0 */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    kprintf("[VMM] Paging enabled. Identity-mapped first 8 MB.\n");
}

/* Create a new page directory for a user process.
 * Copies kernel mappings from kernel_dir and allocates a fresh physical page. */
page_directory_t *vmm_create_user_directory(void) {
    page_directory_t *dir = (page_directory_t *)pmm_alloc_page();
    if (!dir) return NULL;

    /* Zero user entries, share kernel mappings */
    for (int i = 0; i < 1024; i++) {
        dir->entries[i] = kernel_dir.entries[i];
    }
    return dir;
}
