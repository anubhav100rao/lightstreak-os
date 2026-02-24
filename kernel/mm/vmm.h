#ifndef MM_VMM_H
#define MM_VMM_H

/*
 * kernel/mm/vmm.h — Virtual Memory Manager (x86 paging)
 *
 * x86 32-bit two-level paging:
 *   Virtual address breakdown: [31:22] dir index | [21:12] table index | [11:0] offset
 *
 *   Page Directory: 1024 × 4-byte entries.  Each entry (PDE) points to a page table.
 *   Page Table:     1024 × 4-byte entries.  Each entry (PTE) maps one 4KB frame.
 *
 * PDE / PTE common flags (low 12 bits):
 *   bit 0  P   = Present
 *   bit 1  R/W = Writable
 *   bit 2  U/S = User accessible (ring 3)
 *   bit 3  PWT = Write-through (we leave 0)
 *   bit 4  PCD = Cache disable  (we leave 0)
 *   bit 5  A   = Accessed (CPU sets)
 *   bit 6  D   = Dirty (CPU sets, PTE only)
 *   bit 7  PS  = Page Size (PDE: 1=4MB, 0=4KB)   leave 0
 *   bits [31:12] = physical address of next-level table / frame
 */

#include "../../include/types.h"

/* Page flag bits */
#define PAGE_PRESENT  (1u << 0)
#define PAGE_WRITE    (1u << 1)
#define PAGE_USER     (1u << 2)

/* Page sizes */
#define PAGE_SIZE     4096u
#define PAGE_MASK     (~(PAGE_SIZE - 1))

/* Extract directory and table indices from a virtual address */
#define VMM_DIR_IDX(va)   (((va) >> 22) & 0x3FF)
#define VMM_TBL_IDX(va)   (((va) >> 12) & 0x3FF)
#define VMM_PAGE_OFF(va)  ((va) & 0xFFF)

/* Page directory / table types */
typedef uint32_t pde_t;   /* Page Directory Entry */
typedef uint32_t pte_t;   /* Page Table Entry */

typedef struct { pde_t entries[1024]; } ALIGNED(4096) page_directory_t;
typedef struct { pte_t entries[1024]; } ALIGNED(4096) page_table_t;

/* Initialise VMM: build kernel page directory, identity-map kernel, enable paging */
void vmm_init(void);

/* Map virtual address virt → physical address phys with given flags */
void vmm_map_page(page_directory_t *dir, uint32_t virt,
                  uint32_t phys, uint32_t flags);

/* Remove a mapping */
void vmm_unmap_page(page_directory_t *dir, uint32_t virt);

/* Switch to a page directory (write to CR3) */
void vmm_switch_directory(page_directory_t *dir);

/* Get the current kernel page directory */
page_directory_t *vmm_get_kernel_dir(void);

/* Allocate and zero a new page directory (for a new process) */
page_directory_t *vmm_create_user_directory(void);

#endif /* MM_VMM_H */
