#ifndef MM_HEAP_H
#define MM_HEAP_H

/*
 * kernel/mm/heap.h — Kernel heap (kmalloc / kfree)
 *
 * Implements a simple free-list heap.  Each allocation is prefixed by a
 * block_header_t that records its size and whether it is in use.
 *
 *   [ header | user data ... ] [ header | user data ... ] ...
 *
 * kmalloc() does a first-fit search through the free list.
 * kfree()   marks the block free and coalesces adjacent free blocks.
 *
 * The heap lives at a fixed virtual address inside the kernel's identity-
 * mapped region (declared in kernel.c after VMM is initialised).
 */

#include "../../include/types.h"

/* Initialise the heap at [start, start+size) */
void heap_init(uint32_t start, uint32_t size);

/* Allocate at least 'size' bytes; returns 4-byte-aligned pointer or NULL */
void *kmalloc(size_t size);

/* Allocate and zero 'size' bytes */
void *kzalloc(size_t size);

/* Free a pointer previously returned by kmalloc / kzalloc */
void kfree(void *ptr);

/* Diagnostic */
void heap_dump_stats(void);

#endif /* MM_HEAP_H */
