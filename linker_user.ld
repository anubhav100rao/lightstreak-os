/*
 * linker_user.ld — Userspace linker script
 *
 * Loads user programs at virtual address 0x400000.
 * The binary is stripped to a flat format with objcopy,
 * so section order matters: .text first, then data, then bss.
 */

ENTRY(_entry)

SECTIONS {
    . = 0x600000;

    .text : {
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
}
