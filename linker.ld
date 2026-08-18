/*
 * linker.ld - Linker script for the Prism kernel
 *
 * Two-region layout:
 *   1. Boot code + data at physical 1MiB (used in 32-bit mode)
 *   2. C kernel at higher-half, with VMA = HIGHER_HALF_BASE + PA
 *
 * The boot.s 2MiB page maps: VA = HIGHER_HALF_BASE + PA.
 * Therefore VMA must equal HIGHER_HALF_BASE + LMA for all kernel sections.
 * We compute each section's LMA from its VMA to maintain this invariant.
 */

ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

HIGHER_HALF_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
    /* ==== Region 1: Boot code + data at physical 1 MiB ==== */
    . = 1M;

    /* .multiboot MUST be first in the output section so GRUB finds it
     * within the first 32KiB of the ELF file. */
    .text.boot : {
        *(.multiboot)
        *(.text.boot)
    }

    .rodata.boot : ALIGN(4K) {
        *(.rodata.boot)
    }

    /* ==== Region 2: C kernel at higher-half ==== */
    /* Capture physical address after boot data, set VMA = HIGHER_HALF_BASE + PA */
    . = ALIGN(4K);
    __phys_kern_start = .;
    . = HIGHER_HALF_BASE + __phys_kern_start;

    .text : AT(__phys_kern_start) ALIGN(4K) {
        *(.text .text.*)
    }

    /* For each subsequent section, derive LMA from VMA to maintain VMA = HIGHER_HALF_BASE + LMA */
    .rodata : AT(ADDR(.rodata) - HIGHER_HALF_BASE) ALIGN(4K) {
        *(.rodata .rodata.*)
    }

    .data : AT(ADDR(.data) - HIGHER_HALF_BASE) ALIGN(4K) {
        *(.data .data.*)
    }

    .bss : AT(ADDR(.bss) - HIGHER_HALF_BASE) ALIGN(4K) {
        *(COMMON)
        *(.bss .bss.*)
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
