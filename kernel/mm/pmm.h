#ifndef MM_PMM_H
#define MM_PMM_H

/*
 * kernel/mm/pmm.h — Physical Memory Manager
 *
 * Manages physical 4KB page frames using a bitmap:
 *   bit = 0 → frame is FREE
 *   bit = 1 → frame is USED
 *
 * Addresses returned by pmm_alloc_page() are physical (not virtual).
 * Before paging is enabled, physical == virtual.  After paging is enabled,
 * the kernel lives in an identity-mapped region so they remain equal.
 */

#include "../../include/types.h"
#include "../../include/multiboot.h"

#define PMM_PAGE_SIZE   4096                /* 4KB pages */
#define PMM_PAGES_MAX   (256 * 1024 * 1024 / PMM_PAGE_SIZE)  /* 256 MB max */

/* Initialise the PMM from the Multiboot memory map */
void pmm_init(multiboot_info_t *mbi);

/* Allocate one physical page frame; returns physical address or NULL */
void *pmm_alloc_page(void);

/* Free a physical page frame previously returned by pmm_alloc_page() */
void pmm_free_page(void *p);

/* Statistics */
uint32_t pmm_get_total_pages(void);
uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_used_pages(void);

#endif /* MM_PMM_H */
