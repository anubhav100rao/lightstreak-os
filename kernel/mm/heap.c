/*
 * kernel/mm/heap.c — Kernel heap allocator (free-list, first-fit)
 *
 * Layout in memory:
 *
 *   heap_start
 *   ┌──────────────┬──────────────────────────────────────────┐
 *   │  block_hdr_t │  user data (size bytes, 4-byte aligned)  │
 *   └──────────────┴──────────────────────────────────────────┘
 *   │  block_hdr_t │  ...                                      │
 *   └──────────────┴───────────────────────────────────────────┘
 *   heap_end
 *
 * block_hdr_t.magic is checked on kfree() to catch corruption early.
 */

#include "heap.h"
#include "../kernel.h"

#define HEAP_MAGIC  0xC0FFEE42u   /* Canary to detect corruption */
#define ALIGN4(x)   (((x) + 3u) & ~3u)
#define HDR_SIZE    sizeof(block_hdr_t)

typedef struct block_hdr {
    uint32_t        magic;    /* Must equal HEAP_MAGIC */
    uint32_t        size;     /* Usable bytes in this block (excluding header) */
    uint8_t         used;     /* 1 = allocated, 0 = free */
    struct block_hdr *next;   /* Next block header in the chain */
} block_hdr_t;

static block_hdr_t *heap_head = NULL;
static uint32_t     heap_start_addr;
static uint32_t     heap_end_addr;
static uint32_t     heap_used;

/* -------------------------------------------------------------------------
 * heap_init
 * ---------------------------------------------------------------------- */
void heap_init(uint32_t start, uint32_t size) {
    heap_start_addr = start;
    heap_end_addr   = start + size;
    heap_used       = 0;

    /* Create one giant free block spanning the entire heap */
    heap_head = (block_hdr_t *)start;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = size - (uint32_t)HDR_SIZE;
    heap_head->used  = 0;
    heap_head->next  = NULL;

    kprintf("[HEAP] Initialised at 0x%x, size %u KB\n",
            start, size / 1024);
}

/* -------------------------------------------------------------------------
 * kmalloc — first-fit allocation
 * ---------------------------------------------------------------------- */
void *kmalloc(size_t size) {
    if (!size) return NULL;
    size = (uint32_t)ALIGN4(size);   /* Always return 4-byte-aligned pointer */

    block_hdr_t *blk = heap_head;
    while (blk) {
        if (blk->magic != HEAP_MAGIC) {
            kprintf("[HEAP] CORRUPTION: bad magic at 0x%x\n", (uint32_t)blk);
            return NULL;
        }
        if (!blk->used && blk->size >= size) {
            /* Split if there's enough leftover space for another block */
            if (blk->size >= size + HDR_SIZE + 4) {
                block_hdr_t *split =
                    (block_hdr_t *)((uint8_t *)blk + HDR_SIZE + size);
                split->magic = HEAP_MAGIC;
                split->size  = blk->size - size - (uint32_t)HDR_SIZE;
                split->used  = 0;
                split->next  = blk->next;

                blk->size = size;
                blk->next = split;
            }
            blk->used = 1;
            heap_used += blk->size;
            return (void *)((uint8_t *)blk + HDR_SIZE);
        }
        blk = blk->next;
    }

    kprintf("[HEAP] Out of memory! Requested %u bytes\n", (uint32_t)size);
    return NULL;
}

/* -------------------------------------------------------------------------
 * kzalloc — allocate + zero
 * ---------------------------------------------------------------------- */
void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (size_t i = 0; i < size; i++) b[i] = 0;
    }
    return p;
}

/* -------------------------------------------------------------------------
 * kfree — mark block free + coalesce with next block if it's also free
 * ---------------------------------------------------------------------- */
void kfree(void *ptr) {
    if (!ptr) return;

    block_hdr_t *blk = (block_hdr_t *)((uint8_t *)ptr - HDR_SIZE);

    if (blk->magic != HEAP_MAGIC) {
        kprintf("[HEAP] kfree: bad magic at 0x%x — double-free or corruption\n",
                (uint32_t)blk);
        return;
    }
    if (!blk->used) {
        kprintf("[HEAP] kfree: double-free detected at 0x%x\n", (uint32_t)ptr);
        return;
    }

    heap_used -= blk->size;
    blk->used = 0;

    /* Coalesce: merge with next block if it's free */
    if (blk->next && !blk->next->used) {
        blk->size += HDR_SIZE + blk->next->size;
        blk->next  = blk->next->next;
    }
}

/* -------------------------------------------------------------------------
 * heap_dump_stats — debug helper
 * ---------------------------------------------------------------------- */
void heap_dump_stats(void) {
    uint32_t total  = heap_end_addr - heap_start_addr;
    uint32_t free_b = total - heap_used - (uint32_t)HDR_SIZE;
    kprintf("[HEAP] Total: %u KB  Used: %u B  Free: %u B\n",
            total / 1024, heap_used, free_b);
}
