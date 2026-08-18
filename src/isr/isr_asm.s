/*
 * isr_asm.s - Interrupt Service Routine assembly stubs
 *
 * When the CPU takes an exception or hardware interrupt, it:
 *   1. Saves RIP, CS, RFLAGS, (SS, RSP if ring change) on the stack
 *   2. Pushes an error code for some exceptions (10, 11, 12, 13, 14, 17, 19, 21, 29, 30)
 *   3. Jumps to the IDT entry
 *
 * Our stubs ensure a uniform stack layout by:
 *   - Pushing a dummy error code (0) for exceptions that don't have one
 *   - Pushing the vector number
 *   - Saving all general registers
 *   - Calling isr_handler_c with RSP (pointing to the register save area)
 *
 * Stack layout from top (lowest address):
 *   [rsp+0x00]  RDI
 *   [rsp+0x08]  RSI
 *   [rsp+0x10]  RDX
 *   [rsp+0x18]  RCX
 *   [rsp+0x20]  RAX
 *   [rsp+0x28]  R8
 *   [rsp+0x30]  R9
 *   [rsp+0x38]  RBX
 *   [rsp+0x40]  RBP
 *   [rsp+0x48]  R12
 *   [rsp+0x50]  R13
 *   [rsp+0x58]  R14
 *   [rsp+0x60]  R15
 *   [rsp+0x68]  vector number  (pushed by stub)
 *   [rsp+0x70]  error code     (pushed by stub or CPU)
 *   ... CPU-pushed: old RIP, CS, RFLAGS, old SS, old RSP
 */

.section .text

/*
 * Macro for ISRs that DO NOT push an error code from the CPU.
 * We push 0 as a dummy error code to maintain uniform stack layout.
 */
.macro ISR_NOERRCODE vector
.global isr_\vector
isr_\vector:
    cli
    pushq $0                       # Dummy error code (CPU didn't push one)
    pushq $\vector                 # Vector number
    jmp isr_common_stub
.endm

/*
 * Macro for ISRs that DO push an error code from the CPU.
 * We push the vector number only.
 */
.macro ISR_ERRCODE vector
.global isr_\vector
isr_\vector:
    cli
    pushq $\vector                 # Vector number (error code already pushed by CPU)
    jmp isr_common_stub
.endm

/*
 * Register stubs for all 32 CPU exception vectors.
 *
 * Exceptions that push an error code: 8, 10, 11, 12, 13, 14, 17, 19, 21, 29, 30
 * (Notably: 29 is AC, but we won't handle it specially for now)
 */
ISR_NOERRCODE  0   /* Divide By Zero */
ISR_NOERRCODE  1   /* Debug */
ISR_NOERRCODE  2   /* NMI */
ISR_NOERRCODE  3   /* Breakpoint */
ISR_NOERRCODE  4   /* Overflow */
ISR_NOERRCODE  5   /* Bounds Check */
ISR_NOERRCODE  6   /* Invalid Opcode */
ISR_NOERRCODE  7   /* Coprocessor Not Available */
ISR_ERRCODE    8   /* Double Fault */
ISR_NOERRCODE  9   /* Coprocessor Segment Overrun */
ISR_ERRCODE   10   /* Invalid TSS */
ISR_ERRCODE   11   /* Segment Not Present */
ISR_ERRCODE   12   /* Stack-Segment Fault */
ISR_ERRCODE   13   /* General Protection Fault */
ISR_ERRCODE   14   /* Page Fault */
ISR_NOERRCODE 15   /* x87 FPU FP Exception (reserved on modern CPUs) */
ISR_NOERRCODE 16   /* x87 FPU FP Exception */
ISR_NOERRCODE 17   /* Alignment Check */
ISR_NOERRCODE 18   /* Machine Check */
ISR_NOERRCODE 19   /* SIMD FP Exception */
ISR_NOERRCODE 20   /* Virtualization Exception */
ISR_NOERRCODE 21   /* Control Protection Exception */
ISR_NOERRCODE 22   /* Reserved */
ISR_NOERRCODE 23   /* Reserved */
ISR_NOERRCODE 24   /* Reserved */
ISR_NOERRCODE 25   /* Reserved */
ISR_NOERRCODE 26   /* Reserved */
ISR_NOERRCODE 27   /* Reserved */
ISR_NOERRCODE 28   /* Reserved */
ISR_NOERRCODE 29   /* Reserved */
ISR_NOERRCODE 30   /* Reserved */
ISR_NOERRCODE 31   /* Reserved */

/* Also define a generic isr_stub that can be used for unknown interrupts */
.global isr_stub
isr_stub:
    cli
    pushq $0
    pushq $0
    jmp isr_common_stub

/*
 * Hardware IRQ stubs (vectors 32-47 after PIC remapping).
 * IRQ 0 = Timer (vector 32), IRQ 1 = Keyboard (vector 33), etc.
 */
ISR_NOERRCODE 32  /* IRQ 0:  Timer */
ISR_NOERRCODE 33  /* IRQ 1:  Keyboard */
ISR_NOERRCODE 34  /* IRQ 2:  Cascade */
ISR_NOERRCODE 35  /* IRQ 3:  COM2 */
ISR_NOERRCODE 36  /* IRQ 4:  COM1 */
ISR_NOERRCODE 37  /* IRQ 5:  LPT2 */
ISR_NOERRCODE 38  /* IRQ 6:  Floppy */
ISR_NOERRCODE 39  /* IRQ 7:  LPT1 / Spurious */
ISR_NOERRCODE 40  /* IRQ 8:  RTC */
ISR_NOERRCODE 41  /* IRQ 9:  ACPI */
ISR_NOERRCODE 42  /* IRQ 10: Open */
ISR_NOERRCODE 43  /* IRQ 11: Open */
ISR_NOERRCODE 44  /* IRQ 12: PS/2 Mouse */
ISR_NOERRCODE 45  /* IRQ 13: FPU */
ISR_NOERRCODE 46  /* IRQ 14: Primary ATA */
ISR_NOERRCODE 47  /* IRQ 15: Secondary ATA */

/*
 * isr_common_stub - Saves registers, calls C handler, restores, returns.
 *
 * The stack at entry has: vector number, error code.
 * Below those (higher addresses) is what the CPU pushed: RIP, CS, RFLAGS, etc.
 */
isr_common_stub:
    /* Save all general-purpose registers */
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rax
    pushq %r8
    pushq %r9
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* RSP now points to the top of the saved registers.
     * Pass RSP as the argument to isr_handler_c (RDI). */
    movq %rsp, %rdi
    call isr_handler_c

    /* Restore all general-purpose registers (reverse order) */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r9
    popq %r8
    popq %rax
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi

    /* Remove the vector number and error code from the stack */
    addq $16, %rsp

    /* Return from interrupt.
     * iretq pops: RIP, CS, RFLAGS, (SS, RSP if outer privilege level changed) */
    iretq
