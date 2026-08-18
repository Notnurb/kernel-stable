/*
 * switch.s - Context switch for task scheduling
 *
 * void context_switch(uint64_t** old_sp, uint64_t* new_sp);
 *   RDI = pointer to variable that receives the old RSP
 *   RSI = new RSP value to load
 *
 * Saves callee-saved registers on the current stack, saves RSP,
 * loads the new RSP, restores the new task's callee-saved registers,
 * and returns to the new task's saved return address.
 *
 * When called from a timer ISR, the interrupted task's full state
 * (RIP, CS, RFLAGS, RSP, SS + all GPRs) is already on its stack
 * as the ISR frame. Saving RSP captures that entire frame. When we
 * later restore that RSP and the ISR returns via iretq, the task
 * resumes exactly where it was preempted.
 */

.section .text

.global context_switch
.type context_switch, @function
context_switch:
    /* Save RFLAGS + callee-saved registers on current stack */
    pushfq
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Save current stack pointer into old task's TCB */
    movq %rsp, (%rdi)

    /* Load new task's stack pointer */
    movq %rsi, %rsp

    /* Restore RFLAGS + callee-saved registers from new stack */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    popfq

    /* Return to the new task's saved return address.
     * If this is a fresh task, ret jumps to its entry function.
     * If resuming a preempted task, ret returns from the ISR handler
     * that called context_switch, and iretq restores full CPU state. */
    ret
