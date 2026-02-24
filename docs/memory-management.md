# Memory Management

AnubhavOS has three layers of memory management, each building on the previous:

1. **PMM** (`mm/pmm.c`) — Physical Memory Manager: tracks which 4 KB physical
   page frames are free using a bitmap.
2. **VMM** (`mm/vmm.c`) — Virtual Memory Manager: sets up x86 two-level page
   tables and enables paging.
3. **Heap** (`mm/heap.c`) — Kernel heap: a free-list allocator that provides
   `kmalloc`/`kfree` from a fixed-size region in the identity-mapped kernel.

---

## Table of Contents

1. [Physical Memory Manager (PMM)](#1-physical-memory-manager-pmm)
2. [Virtual Memory Manager (VMM)](#2-virtual-memory-manager-vmm)
3. [Kernel Heap](#3-kernel-heap)
4. [Memory Layout After Init](#4-memory-layout-after-init)
5. [Common Pitfalls](#5-common-pitfalls)

---

## 1. Physical Memory Manager (PMM)

### 1.1 Design: bitmap allocator

Each bit in the bitmap represents one 4 KB physical page frame:
- **`0`** = frame is free
- **`1`** = frame is used

```c
// mm/pmm.h
#define PMM_PAGE_SIZE   4096
#define PMM_PAGES_MAX   (256 * 1024 * 1024 / PMM_PAGE_SIZE)  // 65536 pages for 256 MB

// mm/pmm.c
static uint32_t bitmap[PMM_PAGES_MAX / 32];  // 2048 uint32_t = 8 KB in .bss

static uint32_t total_pages;
static uint32_t used_pages;
```

Accessing bit `page` in the bitmap:

```c
// Frame N is at: bitmap[N/32], bit position N%32

static inline void bit_set  (uint32_t frame) {
    bitmap[frame / 32] |=  (1u << (frame % 32));
}
static inline void bit_clear(uint32_t frame) {
    bitmap[frame / 32] &= ~(1u << (frame % 32));
}
static inline int  bit_test (uint32_t frame) {
    return (bitmap[frame / 32] >> (frame % 32)) & 1;
}
```

Example: frame 100 is stored at `bitmap[3]` (index = 100/32 = 3), bit 4
(100 % 32 = 4).

### 1.2 Initialisation

`pmm_init()` implements a "mark everything used, then free available regions"
strategy to avoid accidentally handing out reserved memory:

```c
void pmm_init(multiboot_info_t *mbi) {
    // Step 1: determine total RAM from Multiboot fields
    uint32_t mem_kb;
    if (mbi->flags & MBOOT_FLAG_MEM) {
        // mem_upper = KB above 1 MB; add 1 MB (lower conventional memory)
        mem_kb = mbi->mem_upper + 1024;
    } else {
        mem_kb = 64 * 1024;   // fallback: assume 64 MB
    }

    total_pages = (mem_kb * 1024) / PMM_PAGE_SIZE;
    if (total_pages > PMM_PAGES_MAX) total_pages = PMM_PAGES_MAX;

    // Step 2: mark ALL pages as used (conservative default)
    used_pages = total_pages;
    for (uint32_t i = 0; i < PMM_PAGES_MAX / 32; i++)
        bitmap[i] = 0xFFFFFFFF;    // all bits = 1 = all used

    // Step 3: walk Multiboot memory map, free available regions
    if (mbi->flags & MBOOT_FLAG_MMAP) {
        uint32_t off = 0;
        while (off < mbi->mmap_length) {
            multiboot_mmap_entry_t *e =
                (multiboot_mmap_entry_t *)(mbi->mmap_addr + off);

            if (e->type == MBOOT_MMAP_TYPE_AVAILABLE && e->base_high == 0) {
                pmm_free_region(e->base_low, e->length_low);
            }
            off += e->size + 4;
        }
    }

    // Step 4: re-mark page 0 as used (null pointer guard)
    if (!bit_test(0)) { bit_set(0); used_pages++; }

    // Step 5: re-mark the kernel image as used
    pmm_mark_region_used((uint32_t)kernel_start,
                         (uint32_t)kernel_end - (uint32_t)kernel_start);

    kprintf("[PMM] Total: %u MB  Free: %u MB  Kernel: %u KB\n",
            (total_pages * 4096) / (1024*1024),
            (pmm_get_free_pages() * 4096) / (1024*1024),
            ((uint32_t)(kernel_end - kernel_start)) / 1024);
}
```

**Why free-then-protect instead of protect-everything-first?**
The Multiboot mmap might report overlapping regions.  Starting from "all used"
and only freeing explicitly available regions guarantees safety.

### 1.3 Allocation and Freeing

```c
// Linear scan for the first free bit
void *pmm_alloc_page(void) {
    for (uint32_t i = 0; i < PMM_PAGES_MAX / 32; i++) {
        if (bitmap[i] == 0xFFFFFFFF) continue;  // all 32 frames in this word are used

        for (uint32_t bit = 0; bit < 32; bit++) {
            uint32_t page = i * 32 + bit;
            if (page >= total_pages) return NULL;   // past end of RAM

            if (!bit_test(page)) {
                bit_set(page);
                used_pages++;
                return (void *)(page * PMM_PAGE_SIZE);  // return PHYSICAL address
            }
        }
    }
    return NULL;  // out of memory
}

void pmm_free_page(void *p) {
    uint32_t frame = (uint32_t)p / PMM_PAGE_SIZE;
    if (frame >= total_pages) return;
    if (bit_test(frame)) {
        bit_clear(frame);
        used_pages--;
    }
}
```

**Example allocation sequence**:

```
Initial state (simplified to 8 pages, frames 0-7):
  frame:  0  1  2  3  4  5  6  7
  bitmap: 1  1  0  0  0  1  0  0   (1=used, 0=free)

pmm_alloc_page():
  scan: frame 0 used, frame 1 used, frame 2 FREE
  → bit_set(2), used_pages++
  → return physical address 0x2000  (2 * 4096)

pmm_alloc_page() again:
  → frame 3 → return 0x3000

pmm_free_page(0x2000):
  → frame = 0x2000 / 4096 = 2
  → bit_clear(2), used_pages--
```

### 1.4 Statistics

```c
uint32_t pmm_get_total_pages(void) { return total_pages; }
uint32_t pmm_get_free_pages(void)  { return total_pages - used_pages; }
uint32_t pmm_get_used_pages(void)  { return used_pages; }
```

Used by `sys_meminfo` to report memory usage to the shell:

```
Physical Memory:
  Total: 32768 KB
  Free:  29440 KB
  Used:  3328 KB
```

---

## 2. Virtual Memory Manager (VMM)

### 2.1 x86 Two-Level Paging

A 32-bit virtual address is split into three fields:

```
  31          22 21          12 11           0
  ┌─────────────┬──────────────┬─────────────┐
  │  Dir index  │ Table index  │   Offset    │
  │   10 bits   │   10 bits    │   12 bits   │
  └─────────────┴──────────────┴─────────────┘
       (PD)           (PT)         (page)
```

- **Page Directory (PD)**: 1024 × 4-byte PDEs (4 KB total, 4KB-aligned)
- **Page Table (PT)**: 1024 × 4-byte PTEs (4 KB total, 4KB-aligned)
- **Leaf page**: 4 KB of actual data

Translation for virtual address `virt`:

```c
#define VMM_DIR_IDX(va)  (((va) >> 22) & 0x3FF)  // bits [31:22]
#define VMM_TBL_IDX(va)  (((va) >> 12) & 0x3FF)  // bits [21:12]
#define VMM_PAGE_OFF(va) ((va) & 0xFFF)            // bits [11:0]

// Address translation:
pde_t pde = kernel_dir.entries[VMM_DIR_IDX(virt)];  // look up in PD
page_table_t *pt = pde & 0xFFFFF000;                  // physical addr of PT
pte_t pte = pt->entries[VMM_TBL_IDX(virt)];           // look up in PT
uint32_t phys = (pte & 0xFFFFF000) | VMM_PAGE_OFF(virt);
```

### 2.2 Page flags

```c
#define PAGE_PRESENT  (1u << 0)   // Must be set for valid mapping
#define PAGE_WRITE    (1u << 1)   // Writable (if 0, read-only)
#define PAGE_USER     (1u << 2)   // Accessible from Ring 3 (if 0, Ring-0 only)
```

Other flags (bits 3–11) include write-through, cache disable, accessed, dirty
— all left at 0 in our implementation.

### 2.3 Initialisation: identity-mapping the first 8 MB

```c
// mm/vmm.c
static page_directory_t kernel_dir ALIGNED(4096);
static page_table_t identity_pt0 ALIGNED(4096);  // 0x00000000–0x003FFFFF
static page_table_t identity_pt1 ALIGNED(4096);  // 0x00400000–0x007FFFFF

void vmm_init(void) {
    // Build page table 0: maps frames 0–1023 (0–4MB)
    for (int i = 0; i < 1024; i++) {
        identity_pt0.entries[i] = (uint32_t)(i * PAGE_SIZE)
                                   | PAGE_PRESENT | PAGE_WRITE;
        // virtual 0x000000 + i*4096 → physical i*4096
    }

    // Build page table 1: maps frames 1024–2047 (4MB–8MB)
    for (int i = 0; i < 1024; i++) {
        identity_pt1.entries[i] = (uint32_t)((1024 + i) * PAGE_SIZE)
                                   | PAGE_PRESENT | PAGE_WRITE;
        // virtual 0x400000 + i*4096 → physical (1024+i)*4096
    }

    // Zero the page directory, then install the two PTs
    for (int i = 0; i < 1024; i++) kernel_dir.entries[i] = 0;

    kernel_dir.entries[0] = (uint32_t)&identity_pt0 | PAGE_PRESENT | PAGE_WRITE;
    // dir[0] covers virtual 0x00000000–0x003FFFFF

    kernel_dir.entries[1] = (uint32_t)&identity_pt1 | PAGE_PRESENT | PAGE_WRITE;
    // dir[1] covers virtual 0x00400000–0x007FFFFF

    // Load CR3 with physical address of page directory
    vmm_switch_directory(&kernel_dir);

    // Enable paging: set bit 31 of CR0
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}
```

**What the identity map covers**:

```
Virtual 0x00000000 → Physical 0x00000000   (NULL / BIOS data / IVT)
Virtual 0x000B8000 → Physical 0x000B8000   (VGA text buffer)
Virtual 0x00100000 → Physical 0x00100000   (kernel image)
Virtual 0x00400000 → Physical 0x00400000   (user load address — but this is
                                            used for user procs only, not kernel)
... up to ...
Virtual 0x007FFFFF → Physical 0x007FFFFF   (end of identity map)
```

After `vmm_init()`, **all existing pointers still work** because every kernel
virtual address equals its physical address within the first 8 MB.

### 2.4 Mapping additional pages (`vmm_map_page`)

Used by `exec()` to map user code and stack pages:

```c
void vmm_map_page(page_directory_t *dir, uint32_t virt,
                  uint32_t phys, uint32_t flags) {
    uint32_t di = VMM_DIR_IDX(virt);   // which directory entry
    uint32_t ti = VMM_TBL_IDX(virt);   // which table entry

    page_table_t *pt;
    if (!(dir->entries[di] & PAGE_PRESENT)) {
        // Allocate a new page table from PMM
        pt = (page_table_t *)pmm_alloc_page();
        for (int i = 0; i < 1024; i++) pt->entries[i] = 0;  // zero it
        dir->entries[di] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        pt = (page_table_t *)(dir->entries[di] & 0xFFFFF000);
    }

    // Set the leaf entry
    pt->entries[ti] = (phys & 0xFFFFF000) | (flags & 0xFFF) | PAGE_PRESENT;

    // Invalidate this virtual address in the TLB
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}
```

**`invlpg`**: The CPU caches recent page table entries in the TLB (Translation
Lookaside Buffer).  After modifying a PTE, `invlpg addr` invalidates the TLB
entry for that address to force a fresh lookup on the next access.

**Example**: exec maps user code at 0x400000:

```c
// exec.c: map one page of the shell binary
void *phys_page = pmm_alloc_page();         // e.g. returns 0x01200000
vmm_map_page(user_dir, 0x400000, (uint32_t)phys_page,
             PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
// Now virtual 0x400000 in user_dir → physical 0x01200000
// PAGE_USER flag: Ring-3 code can read/write this page
```

### 2.5 User page directories (`vmm_create_user_directory`)

Each user process needs its own page directory so it has a private virtual
address space:

```c
page_directory_t *vmm_create_user_directory(void) {
    // Allocate a 4KB-aligned physical page for the new directory
    page_directory_t *dir = (page_directory_t *)pmm_alloc_page();
    if (!dir) return NULL;

    // Copy all 1024 kernel entries into the new directory
    // This shares the kernel's identity-mapped pages with the user process
    for (int i = 0; i < 1024; i++) {
        dir->entries[i] = kernel_dir.entries[i];
    }
    // NOTE: user-specific page tables (for 0x400000, stack) are NOT copied yet.
    // exec() adds those separately via vmm_map_page().

    return dir;
}
```

**Why copy kernel entries?**  When a Ring-3 interrupt fires (e.g. `int 0x80`),
the CPU switches privilege levels but does **not** change CR3.  The interrupt
handler runs in Ring 0 using the **user process's** page directory.  If the
kernel pages are not present in that directory, every instruction in the syscall
handler would cause a page fault.

**Switching page directories**:

```c
void vmm_switch_directory(page_directory_t *dir) {
    __asm__ volatile (
        "mov %0, %%cr3"
        : : "r"((uint32_t)dir) : "memory"
    );
}
// Writing to CR3 automatically flushes the entire TLB
```

---

## 3. Kernel Heap

### 3.1 Design: free-list with block headers

The heap lives in a contiguous region of the identity-mapped kernel space.
Each allocation is preceded by a `block_hdr_t`:

```
Memory layout:
┌──────────────┬────────────────────────────────┐
│  block_hdr_t │  user data (returned ptr)      │
│  magic       │                                │
│  size        │  size bytes                    │
│  used        │                                │
│  next ──────►│ [next block_hdr_t]             │
└──────────────┴────────────────────────────────┘
```

```c
// mm/heap.c
#define HEAP_MAGIC  0xC0FFEE42u   // canary to detect corruption

typedef struct block_hdr {
    uint32_t        magic;  // must be HEAP_MAGIC
    uint32_t        size;   // usable bytes (NOT including this header)
    uint8_t         used;   // 1 = allocated, 0 = free
    struct block_hdr *next; // next block in the chain
} block_hdr_t;
```

### 3.2 Initialisation

```c
void heap_init(uint32_t start, uint32_t size) {
    heap_start_addr = start;
    heap_end_addr   = start + size;

    // Create one giant free block
    heap_head        = (block_hdr_t *)start;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = size - sizeof(block_hdr_t);  // all space minus header
    heap_head->used  = 0;
    heap_head->next  = NULL;
}
```

After `heap_init(heap_base, 2*1024*1024)`:

```
heap_base:
  ┌──────────────────────────────────────────────────────┐
  │ block_hdr_t: magic=0xC0FFEE42, size=2097136, used=0  │
  │ [2 MB - sizeof(block_hdr_t) of free space]           │
  └──────────────────────────────────────────────────────┘
```

### 3.3 `kmalloc` — first-fit with splitting

```c
void *kmalloc(size_t size) {
    if (!size) return NULL;
    size = (size + 3u) & ~3u;   // round up to 4-byte alignment

    block_hdr_t *blk = heap_head;
    while (blk) {
        if (blk->magic != HEAP_MAGIC) { /* corruption! */ return NULL; }

        if (!blk->used && blk->size >= size) {
            // Found a free block large enough

            // Split if there's enough space for another block afterwards
            if (blk->size >= size + sizeof(block_hdr_t) + 4) {
                block_hdr_t *split =
                    (block_hdr_t *)((uint8_t *)blk + sizeof(block_hdr_t) + size);
                split->magic = HEAP_MAGIC;
                split->size  = blk->size - size - sizeof(block_hdr_t);
                split->used  = 0;
                split->next  = blk->next;

                blk->size = size;
                blk->next = split;
            }

            blk->used = 1;
            heap_used += blk->size;
            return (void *)((uint8_t *)blk + sizeof(block_hdr_t));
        }
        blk = blk->next;
    }
    return NULL;  // out of memory
}
```

**Splitting example**:

```
Before kmalloc(64):
  ┌─────────────────────┐
  │ hdr: size=2MB, free │──►  NULL
  └─────────────────────┘

After kmalloc(64):
  ┌────────────────┬──────────┬──────────────────────────────┐
  │ hdr: sz=64,used│ 64 bytes │ hdr: sz=2MB-64-hdrsize, free │──► NULL
  └────────────────┴──────────┴──────────────────────────────┘
     ▲ returned ptr starts here
```

### 3.4 `kfree` — mark free and coalesce

```c
void kfree(void *ptr) {
    if (!ptr) return;

    // Walk back to find the header
    block_hdr_t *blk = (block_hdr_t *)((uint8_t *)ptr - sizeof(block_hdr_t));

    if (blk->magic != HEAP_MAGIC) {
        kprintf("[HEAP] kfree: bad magic — corruption or double-free\n");
        return;
    }
    if (!blk->used) {
        kprintf("[HEAP] kfree: double-free at 0x%x\n", (uint32_t)ptr);
        return;
    }

    heap_used -= blk->size;
    blk->used = 0;

    // Coalesce with the NEXT block if it's also free
    if (blk->next && !blk->next->used) {
        blk->size += sizeof(block_hdr_t) + blk->next->size;
        blk->next  = blk->next->next;
    }
}
```

**Coalescing example**:

```
Before kfree(ptr_A):
  ┌──────────────┬────────┬──────────────┬─────────┐
  │ hdr: sz=64,A │ 64B    │ hdr: sz=64,B │  64B    │ (B is free)
  └──────────────┴────────┴──────────────┴─────────┘

After kfree(ptr_A):
  ┌────────────────────────────────────────────────┐
  │ hdr: sz=64+hdr_size+64=152, free               │
  └────────────────────────────────────────────────┘
  (A and B merged into one free block)
```

**Limitation**: we only coalesce with the *next* block, not the previous.  This
means fragmentation can accumulate over time.  For a simple kernel this is
acceptable.

### 3.5 `kzalloc`

```c
void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (size_t i = 0; i < size; i++) b[i] = 0;
    }
    return p;
}
```

Used for PCB allocation (`process_create`) so all fields start at zero.

### 3.6 HEAP_MAGIC corruption check

Every block header stores `0xC0FFEE42`.  On `kmalloc` and `kfree`, we verify
it matches.  A wrong value means:

- A buffer overflow wrote past the end of an allocation into the next block header.
- The heap start address was wrong (e.g. the heap overlaps the GRUB module).

```c
// Typical corruption output:
[HEAP] CORRUPTION: bad magic at 0xXXXXXXXX
```

---

## 4. Memory Layout After Init

After all three subsystems initialise, physical memory looks like:

```
Physical address
────────────────────────────────────────────────────────
0x00000000    Page 0: PMM keeps this USED (null guard)
0x00000004    BIOS / IVT / etc.
0x000B8000    VGA text buffer (80×25 × 2 bytes = 4000 bytes)
0x00100000    kernel_start — kernel code (.text)
              kernel .rodata
              kernel .data
              kernel .bss (includes: boot stack 16KB, PMM bitmap 8KB)
kernel_end    <- linker exports this; ~100-200 KB after 0x100000 (varies)
              GRUB module: initramfs.img (typically ~5 KB)
mod_end       <- mods[0].mod_end
page_align(mod_end)  HEAP START — 2 MB free-list heap
heap_end      First PMM-managed free page frames
────────────────────────────────────────────────────────
0x01FFFFFF    End of 32 MB physical RAM (QEMU -m 32M)
```

Virtual addresses after `vmm_init()`:

```
Virtual 0x00000000 – 0x007FFFFF: identity-mapped (kernel lives here)
Virtual 0x00400000 – (varies):   user process code (per-process page dir)
Virtual 0xBFFFC000 – 0xBFFFFFFF: user stack (per-process page dir)
Everything else: not mapped → page fault on access
```

---

## 5. Common Pitfalls

### PMM hands out kernel memory

**Cause**: Step 5 of `pmm_init` (mark kernel region as used) was skipped.
**Symptom**: `vmm_map_page` calls `pmm_alloc_page()`, which returns a physical
address that overlaps with kernel code.  Writing a page table entry there
corrupts the kernel.
**Fix**: Ensure `pmm_mark_region_used(kernel_start, kernel_end - kernel_start)`
runs after the mmap walk.

### Heap zeroes the initramfs

**Cause**: `heap_init(kernel_end, 2MB)` is called before `initramfs_load()`.
GRUB placed `initramfs.img` right after the kernel at `kernel_end`.
`heap_init` doesn't clear memory itself, but `kmalloc` returns addresses in
that range, and callers using `kzalloc` will zero out initramfs data.
**Fix**: Scan `mbi->mods_addr` and start the heap after the highest
`mods[i].mod_end`.  This is already done in `kmain()`:

```c
uint32_t safe_end = (uint32_t)kernel_end;
multiboot_mod_t *mods = (multiboot_mod_t *)mbi->mods_addr;
for (uint32_t i = 0; i < mbi->mods_count; i++) {
    if (mods[i].mod_end > safe_end)
        safe_end = mods[i].mod_end;
}
uint32_t heap_base = (safe_end + 0xFFFu) & ~0xFFFu;
```

### Page fault in the first `vmm_map_page` call

**Cause**: `vmm_map_page` is called before `vmm_init()` is complete.  Accessing
the new page table before paging is enabled works (physical == virtual), but
calling `invlpg` before CR0.PG is set is illegal.
**Fix**: Call `vmm_init()` before any `vmm_map_page` calls.

### Page fault when switching to user page directory

**Cause**: `vmm_create_user_directory` copies `kernel_dir.entries[0]` and
`kernel_dir.entries[1]`, but the *physical* address of `identity_pt0` and
`identity_pt1` is valid because those structs are statically allocated in the
identity-mapped kernel.  If the kernel were mapped above 3 GB (high-half
kernel), this would not work without extra translation.
**This is fine for AnubhavOS** since everything is in the low 8 MB.

### Double-free detected

```c
[HEAP] kfree: double-free detected at 0xXXXXXXXX
```

A pointer was freed twice.  Common cause: `process_destroy` calls `kfree` on
both `p->kernel_stack` and `p` itself, but somehow `p` was already freed.
The magic canary check prevents this from silently corrupting the free list.
