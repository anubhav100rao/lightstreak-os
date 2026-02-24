#ifndef MULTIBOOT_H
#define MULTIBOOT_H

/*
 * include/multiboot.h — Multiboot 1 specification structures
 *
 * GRUB fills in a multiboot_info_t and passes its address in EBX to the
 * kernel entry point.  We only declare the fields we actually use.
 *
 * Spec: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 */

#include "types.h"

/* Flags indicating which multiboot_info_t fields are valid */
#define MBOOT_FLAG_MEM      (1 << 0)   /* mem_lower / mem_upper valid */
#define MBOOT_FLAG_BOOT_DEV (1 << 1)
#define MBOOT_FLAG_CMDLINE  (1 << 2)
#define MBOOT_FLAG_MODS     (1 << 3)   /* mods_count / mods_addr valid */
#define MBOOT_FLAG_MMAP     (1 << 6)   /* mmap_length / mmap_addr valid */

/* multiboot_info_t — the struct GRUB builds before calling us */
typedef struct {
    uint32_t flags;

    /* Memory (valid if MBOOT_FLAG_MEM) */
    uint32_t mem_lower;     /* KB of conventional memory (below 1 MB) */
    uint32_t mem_upper;     /* KB of upper memory (above 1 MB) */

    uint32_t boot_device;
    uint32_t cmdline;

    /* Modules (valid if MBOOT_FLAG_MODS) */
    uint32_t mods_count;    /* Number of loaded modules */
    uint32_t mods_addr;     /* Physical address of first multiboot_mod_t */

    uint32_t syms[4];       /* Symbol table info (unused) */

    /* Memory map (valid if MBOOT_FLAG_MMAP) */
    uint32_t mmap_length;   /* Total size of the mmap buffer in bytes */
    uint32_t mmap_addr;     /* Physical address of first mmap entry */

    /* Remaining fields (drives, config table, boot loader name, etc.) */
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} PACKED multiboot_info_t;

/*
 * Memory map entry.  Each entry is prefixed by its 'size' field (not
 * including size itself).  Navigate with:
 *   entry = (entry_t *)((uint8_t *)entry + entry->size + 4)
 *
 * base and length are 64-bit (low + high words on 32-bit x86).
 */
typedef struct {
    uint32_t size;          /* Size of this entry (excluding this field) */
    uint32_t base_low;      /* Physical base address bits [31:0]  */
    uint32_t base_high;     /* Physical base address bits [63:32] */
    uint32_t length_low;    /* Region length bits [31:0]           */
    uint32_t length_high;   /* Region length bits [63:32]          */
    uint32_t type;          /* 1 = available RAM, anything else = reserved */
} PACKED multiboot_mmap_entry_t;

#define MBOOT_MMAP_TYPE_AVAILABLE 1

/* Module entry (one per GRUB module, e.g. initramfs.img) */
typedef struct {
    uint32_t mod_start;     /* Physical address of module data */
    uint32_t mod_end;       /* Physical address of end of module data */
    uint32_t cmdline;       /* Physical address of module command line */
    uint32_t reserved;
} PACKED multiboot_mod_t;

#endif /* MULTIBOOT_H */
