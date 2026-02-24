/*
 * kernel/mm/pmm.c — Physical Memory Manager (bitmap allocator)
 *
 * Algorithm:
 *   1. At init, mark ALL pages as used.
 *   2. Walk the Multiboot memory map.  For each AVAILABLE region,
 *      free every page frame it contains (mark bits 0).
 *   3. Re-mark the kernel's own pages as used (they must never be handed out).
 *   4. pmm_alloc_page() does a linear scan for the first free bit.
 *
 * The bitmap array is statically sized for PMM_PAGES_MAX pages.
 * At 256 MB that is 65536 pages → 8192 bytes (8 KB) in .bss — negligible.
 */

#include "pmm.h"
#include "../kernel.h"

/* bitmap[i] bit j = page frame (i*32 + j) — 0 means free, 1 means used */
static uint32_t bitmap[PMM_PAGES_MAX / 32];

static uint32_t total_pages;
static uint32_t used_pages;

/* -------------------------------------------------------------------------
 * Linker-script symbols marking start and end of the kernel image
 * ---------------------------------------------------------------------- */
extern uint8_t kernel_start[];  /* = 0x100000 */
extern uint8_t kernel_end[];

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static inline void bit_set(uint32_t frame) {
    bitmap[frame / 32] |= (1u << (frame % 32));
}

static inline void bit_clear(uint32_t frame) {
    bitmap[frame / 32] &= ~(1u << (frame % 32));
}

static inline int bit_test(uint32_t frame) {
    return (int)(bitmap[frame / 32] >> (frame % 32)) & 1;
}

/* Mark every page in [base, base+length) as free */
static void pmm_free_region(uint32_t base, uint32_t length) {
    uint32_t page = base / PMM_PAGE_SIZE;
    uint32_t end  = (base + length) / PMM_PAGE_SIZE;
    for (; page < end && page < PMM_PAGES_MAX; page++) {
        if (bit_test(page)) {
            bit_clear(page);
            used_pages--;
        }
    }
}

/* Mark every page in [base, base+length) as used */
static void pmm_mark_region_used(uint32_t base, uint32_t length) {
    uint32_t page = base / PMM_PAGE_SIZE;
    /* Round length up to the next page boundary */
    uint32_t end  = (base + length + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    for (; page < end && page < PMM_PAGES_MAX; page++) {
        if (!bit_test(page)) {
            bit_set(page);
            used_pages++;
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void pmm_init(multiboot_info_t *mbi) {
    uint32_t mem_kb;

    /* Determine total usable RAM from Multiboot mem_upper field */
    if (mbi->flags & MBOOT_FLAG_MEM) {
        /* mem_upper = KB above 1MB; add the conventional 640KB below 1MB */
        mem_kb = mbi->mem_upper + 1024;
    } else {
        mem_kb = 64 * 1024; /* Fallback: assume 64MB */
    }

    total_pages = (mem_kb * 1024) / PMM_PAGE_SIZE;
    if (total_pages > PMM_PAGES_MAX) total_pages = PMM_PAGES_MAX;

    /* Step 1: Mark ALL pages as used */
    used_pages = total_pages;
    for (uint32_t i = 0; i < PMM_PAGES_MAX / 32; i++) {
        bitmap[i] = 0xFFFFFFFF;
    }

    /* Step 2: Walk the memory map, freeing available regions */
    if (mbi->flags & MBOOT_FLAG_MMAP) {
        uint32_t offset = 0;
        while (offset < mbi->mmap_length) {
            multiboot_mmap_entry_t *e =
                (multiboot_mmap_entry_t *)(mbi->mmap_addr + offset);

            if (e->type == MBOOT_MMAP_TYPE_AVAILABLE) {
                /* Only handle regions that fit in 32-bit address space */
                if (e->base_high == 0 && e->length_high == 0) {
                    pmm_free_region(e->base_low, e->length_low);
                } else if (e->base_high == 0) {
                    /* Region crosses 4GB boundary — only free the 32-bit part */
                    uint32_t available = 0xFFFFFFFFu - e->base_low + 1;
                    pmm_free_region(e->base_low, available);
                }
            }

            offset += e->size + 4; /* +4 for the 'size' field itself */
        }
    } else {
        /* No memory map: assume all memory above 1MB up to total is free */
        pmm_free_region(0x100000, (total_pages - 256) * PMM_PAGE_SIZE);
    }

    /* Step 3: Re-mark page 0 as used (null pointer protection) */
    if (!bit_test(0)) { bit_set(0); used_pages++; }

    /* Step 4: Re-mark the kernel image pages as used */
    uint32_t kstart = (uint32_t)kernel_start;
    uint32_t kend   = (uint32_t)kernel_end;
    pmm_mark_region_used(kstart, kend - kstart);

    kprintf("[PMM] Total: %u MB  Free: %u MB  Used by kernel: %u KB\n",
            (total_pages * PMM_PAGE_SIZE) / (1024 * 1024),
            (pmm_get_free_pages() * PMM_PAGE_SIZE) / (1024 * 1024),
            ((uint32_t)(kend - kstart)) / 1024);
}

void *pmm_alloc_page(void) {
    for (uint32_t i = 0; i < PMM_PAGES_MAX / 32; i++) {
        if (bitmap[i] == 0xFFFFFFFF) continue; /* All used — skip */
        for (uint32_t bit = 0; bit < 32; bit++) {
            uint32_t page = i * 32 + bit;
            if (page >= total_pages) return NULL;
            if (!bit_test(page)) {
                bit_set(page);
                used_pages++;
                return (void *)(page * PMM_PAGE_SIZE);
            }
        }
    }
    return NULL; /* Out of memory */
}

void pmm_free_page(void *p) {
    uint32_t frame = (uint32_t)p / PMM_PAGE_SIZE;
    if (frame >= total_pages) return;
    if (bit_test(frame)) {
        bit_clear(frame);
        used_pages--;
    }
}

uint32_t pmm_get_total_pages(void) { return total_pages; }
uint32_t pmm_get_free_pages(void)  { return total_pages - used_pages; }
uint32_t pmm_get_used_pages(void)  { return used_pages; }
