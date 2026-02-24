/*
 * linker.ld — Kernel linker script
 *
 * Loads the kernel at the 1 MB physical mark (0x100000), which is
 * standard for 32-bit x86 protected-mode kernels. Memory below 1 MB
 * is reserved for BIOS data, VGA buffers, and boot structures.
 *
 * Section order is important:
 *   1. .multiboot  — GRUB magic header; must be within first 8 KB of image
 *   2. .text       — executable code
 *   3. .rodata     — read-only data (string literals, const tables)
 *   4. .data       — initialised read-write data
 *   5. .bss        — zero-initialised data (stack lives here; not in binary)
 */

ENTRY(boot_start)

SECTIONS {
    . = 0x100000;           /* Load address: 1 MB */

    .text : {
        *(.multiboot)       /* Multiboot header MUST come first */
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Export symbols marking kernel extent for the PMM.
     * kernel_end is AFTER .bss (stack lives in .bss) so the heap
     * starts above both the code and the boot stack. */
    kernel_start = 0x100000;
    kernel_end   = .;
}
