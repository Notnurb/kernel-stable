/*
 * boot.s - Prism Kernel Stage 1 + Stage 2 Boot Code
 *
 * Stage 1 (Real Mode -> Protected Mode -> Long Mode):
 *   - Multiboot2 header for GRUB
 *   - Minimal GDT for the transition
 *   - Identity + higher-half page tables (first 2MiB, 2MiB pages)
 *   - Enable PAE, IA-32e mode, paging
 *   - Far jump into 64-bit long mode
 *
 * Stage 2 (Long Mode Entry):
 *   - Set up 64-bit stack and segment registers
 *   - Jump to kernel_main() at higher-half VMA
 *
 * This code lives in .text.boot / .rodata.boot (physical 1 MiB).
 * The C kernel lives at higher-half VMA 0xFFFFFFFF80100000.
 *
 * Architecture: x86_64
 */

#define HIGHER_HALF_BASE 0xFFFFFFFF80000000

/* ========================================================================
 * SECTION 1: Multiboot2 Header
 * Must be within the first 32KiB of the binary.
 * GRUB scans for magic 0xE85250D6 to identify compliant kernels.
 * ======================================================================== */

.section .multiboot
.align 8
mbhdr_start:
    .long 0xE85250D6                              /* magic */
    .long 0                                       /* architecture: i386 */
    .long (mbhdr_end - mbhdr_start)               /* header length */
    .long (0x100000000 - (0xE85250D6 + 0 + (mbhdr_end - mbhdr_start)))  /* checksum */

    /* End tag */
    .long 0x00000000
    .long 0x00000000
    .long 0x00000008
mbhdr_end:

/* ========================================================================
 * SECTION 2: Minimal GDT for long mode transition
 * ======================================================================== */

.section .rodata.boot
.align 16
boot_gdt:
    .quad 0                                     /* 0x00: Null descriptor */
    .quad 0x00AF9A000000FFFF                    /* 0x08: 64-bit kernel code */
    .quad 0x00AF92000000FFFF                    /* 0x10: 64-bit kernel data */
boot_gdt_end:

boot_gdt_descriptor:
    .word (boot_gdt_end - boot_gdt - 1)
    .quad boot_gdt

/* ========================================================================
 * SECTION 3: Page Tables — Identity + Higher-Half (2MiB pages)
 *
 * PML4:
 *   [0]   -> pdpt_id   (identity: virtual 0x0-0x1FFFFF -> phys 0x0-0x1FFFFF)
 *   [511] -> pdpt_hh   (higher-half: 0xFFFFFFFF80000000 -> phys 0x0-0x1FFFFF)
 *
 * Both map the same physical 2MiB. After entering 64-bit mode, we jump
 * to the higher-half entry point. Identity mapping keeps boot code alive.
 * ======================================================================== */

.section .rodata.boot
.align 4096
pml4_table:
    .quad (pdpt_id + 0x3)                       /* [0]   -> identity PDPT */
    .fill 510, 8, 0
    .quad (pdpt_hh + 0x3)                       /* [511] -> higher-half PDPT */

.align 4096
pdpt_id:
    .quad (pd_id + 0x3)                         /* [0] -> identity PD */
    .fill 511, 8, 0

.align 4096
pdpt_hh:
    .fill 510, 8, 0
    .quad (pd_hh + 0x3)                         /* [510] -> higher-half PD */
    .fill 1, 8, 0

.align 4096
pd_id:
    .quad 0x83                                  /* [0]: 2MiB page at phys 0x0 */
    .fill 511, 8, 0

.align 4096
pd_hh:
    .quad 0x83                                  /* [0]: 2MiB page at phys 0x0 */
    .fill 511, 8, 0

/* ========================================================================
 * SECTION 4: Entry Point (_start)
 *
 * Called by GRUB in 32-bit protected mode.
 * EAX = multiboot2 magic (0x36D76289)
 * EBX = pointer to multiboot2 info structure
 * ======================================================================== */

.section .text.boot
.code32
.global _start
.type _start, @function
_start:
    cli

    /* Set up a temporary stack (within the identity-mapped 2MiB region) */
    movl $0x9FC00, %esp
    movl %esp, %ebp

    /* Save multiboot2 info pointer (EBX) for the C kernel */
    movl %ebx, saved_mbi

    cld

    /* Enable A20 line */
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    /* Load the minimal GDT */
    lgdt boot_gdt_descriptor

    /* Reload segment registers */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs

    /* Enable PAE (CR4.PAE = bit 5) */
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    /* Load CR3 with the PML4 physical address */
    movl $pml4_table, %eax
    movl %eax, %cr3

    /* Enable IA-32e mode (IA32_EFER.LME = bit 8) */
    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    /* Enable paging (CR0.PG = bit 31) */
    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0

    /* Far jump to reload CS with 64-bit code segment.
     * After this, the CPU is in 64-bit mode.
     * We are still at a physical (identity-mapped) address. */
    ljmp $0x08, $long_mode_entry64

/* ========================================================================
 * SECTION 5: Long Mode Entry
 *
 * Now running in 64-bit mode with paging enabled.
 * Identity + higher-half page tables are active.
 * We are executing at a physical address (identity-mapped).
 * Jump to kernel_main at its higher-half VMA.
 * ======================================================================== */

.section .text.boot
.code64
.global long_mode_entry64
.type long_mode_entry64, @function
long_mode_entry64:
    /* Set up a 64-bit stack (identity-mapped, at physical 0x9F000) */
    movq $0x9F000, %rsp
    movq $0x9F000, %rbp

    /* Load data segment registers */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %gs

    /*
     * Pass the saved multiboot2 info pointer to kernel_main (RDI).
     * saved_mbi is at a physical address (identity-mapped), so accessible.
     */
    movl saved_mbi, %eax
    movq %rax, %rdi

    /*
     * Jump to kernel_main at higher-half VMA.
     * movabs loads a full 64-bit immediate into the register.
     * kernel_main is linked at the correct VMA by the linker.
     */
    movabs $kernel_main, %rax
    call *%rax

    /* If kernel_main returns, halt forever */
halt_loop:
    hlt
    jmp halt_loop

/* ========================================================================
 * SECTION 6: Saved data (physical address, reachable from boot code)
 * ======================================================================== */

.section .rodata.boot
.align 4
saved_mbi:
    .long 0
