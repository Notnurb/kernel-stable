/*
 * usermode.s - Ring 0 to Ring 3 transition + syscall entry
 *
 * enter_user_mode:
 *   Called when a kernel task should transition to ring 3.
 *   Sets TSS.RSP0, optionally switches CR3, builds an iretq frame,
 *   and does iretq to enter user mode.
 *
 * syscall_entry:
 *   AMD64 SYSCALL clobbers RCX->user RIP and R11->user RFLAGS.
 *   Strategy: push ALL user state to the user stack first (it's still
 *   mapped, same CR3), then switch to kernel stack (from TSS.RSP0),
 *   read back user state via saved RSP, build an ISR-compatible frame,
 *   call C dispatcher, write return value into frame, do iretq.
 *
 * struct task_t offsets (must match task.h):
 *   +0x00  pid            (uint64_t)
 *   +0x08  state          (int)
 *   +0x10  stack_pointer  (uint64_t*)
 *   +0x18  kernel_stack   (uint64_t*)
 *   +0x20  time_slice     (uint64_t)
 *   +0x28  next           (task_t*)
 *   +0x30  ring           (uint8_t)
 *   +0x31  pad[7]
 *   +0x38  cr3            (uint64_t)
 *   +0x40  user_rip       (uint64_t)
 *   +0x48  user_rsp       (uint64_t)
 *   +0x50  user_rflags    (uint64_t)
 */

.section .text

.set T_KSTACK,      0x18
.set T_RING,        0x30
.set T_CR3,         0x38
.set T_USER_RIP,    0x40
.set T_USER_RSP,    0x48
.set T_USER_RFLAGS, 0x50
.set KERNEL_STACK_SIZE, 8192

/* ============================================================ */
/* enter_user_mode - transition current kernel task to ring 3   */
/* ============================================================ */
.extern task_current
.extern tss_set_rsp0

.global enter_user_mode
.type enter_user_mode, @function
enter_user_mode:
    call task_current               /* rax = task_t* */

    /* Set TSS.RSP0 = kernel_stack + KERNEL_STACK_SIZE */
    movq T_KSTACK(%rax), %rdi
    testq %rdi, %rdi
    jz 1f
    addq $KERNEL_STACK_SIZE, %rdi
    pushq %rax
    call tss_set_rsp0
    popq %rax
1:

    /* Switch CR3 if user has its own address space */
    movq T_CR3(%rax), %rdi
    testq %rdi, %rdi
    jz 2f
    movq %rdi, %cr3
2:
    call task_current               /* reload after potential CR3 switch */

    /* Build iretq frame on current kernel stack */
    movq T_USER_RFLAGS(%rax), %r11
    orq  $0x200, %r11              /* ensure IF=1 */
    movq T_USER_RSP(%rax), %r10
    movq T_USER_RIP(%rax), %r9

    pushq $0x23                     /* SS = GDT_SEL_UDATA | RPL3 */
    pushq %r10                      /* RSP */
    pushq %r11                      /* RFLAGS */
    pushq $0x1B                     /* CS = GDT_SEL_UCODE | RPL3 */
    pushq %r9                       /* RIP */

    iretq

/* ============================================================ */
/* syscall_entry - AMD64 SYSCALL handler                        */
/* ============================================================ */
.extern tss_get_rsp0
.extern syscall_dispatch

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    /*
     * On entry (from SYSCALL in ring 3):
     *   RAX = syscall number     RDI = arg0    RSI = arg1
     *   RDX = arg2              R10 = arg3    R8  = arg4    R9 = arg5
     *   RCX = user RIP          R11 = user RFLAGS
     *   RSP = user RSP          RBX/RBP/R12-R15 = user values (preserved)
     *
     * User stack push layout (15 pushes = 120 bytes, from r15 down):
     *   [r15 - 8]    = rax (syscall#)
     *   [r15 - 16]   = r11 (user RFLAGS)
     *   [r15 - 24]   = rcx (user RIP)
     *   [r15 - 32]   = rdi (arg0)
     *   [r15 - 40]   = rsi (arg1)
     *   [r15 - 48]   = rdx (arg2)
     *   [r15 - 56]   = r10 (arg3)
     *   [r15 - 64]   = r8  (arg4)
     *   [r15 - 72]   = r9  (arg5)
     *   [r15 - 80]   = rbx
     *   [r15 - 88]   = rbp
     *   [r15 - 96]   = r12
     *   [r15 - 104]  = r13
     *   [r15 - 112]  = r14
     *   [r15 - 120]  = r15
     */

    /* Phase 1: Save all user state to user stack */
    pushq %rax
    pushq %r11
    pushq %rcx
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* r15 = original user RSP (before our 15 pushes) */
    lea 120(%rsp), %r15

    /* Phase 2: Load kernel stack */
    call tss_get_rsp0
    movq %rax, %rsp

    /* Phase 3: Build ISR-compatible frame on kernel stack.
     * Push in reverse order (last push = lowest address = top of frame). */

    /* GPR saves (matching isr_common_stub push order) */
    pushq -120(%r15)                /* r15 (user r15) */
    pushq -112(%r15)                /* r14 (user r14) */
    pushq -104(%r15)                /* r13 (user r13) */
    pushq -96(%r15)                 /* r12 (user r12) */
    pushq -88(%r15)                 /* rbp (user rbp) */
    pushq -80(%r15)                 /* rbx (user rbx) */
    pushq -72(%r15)                 /* r9  (user arg5) */
    pushq -64(%r15)                 /* r8  (user arg4) */
    pushq -8(%r15)                  /* rax (syscall number) */
    pushq -24(%r15)                 /* rcx (user RIP) */
    pushq -48(%r15)                 /* rdx (user arg2) */
    pushq -40(%r15)                 /* rsi (user arg1) */
    pushq -32(%r15)                 /* rdi (user arg0) */

    /* Vector + error code */
    pushq $0x80
    pushq $0

    /* CPU-pushed portion for iretq (ring 3 to ring 0) */
    pushq -24(%r15)                 /* user RIP */
    pushq $0x1B                     /* user CS */
    pushq -16(%r15)                 /* user RFLAGS */
    pushq %r15                      /* user RSP (original) */
    pushq $0x23                     /* user SS */

    /* Phase 4: Call C syscall dispatcher.
     * SysV ABI: RDI=number, RSI=arg0, RDX=arg1, RCX=arg2,
     *           R8=arg3, R9=arg4, stack=arg5 */
    movq -8(%r15), %rdi             /* number */
    movq -32(%r15), %rsi            /* arg0 */
    movq -40(%r15), %rdx            /* arg1 */
    movq -48(%r15), %rcx            /* arg2 */
    movq -56(%r15), %r8             /* arg3 */
    movq -64(%r15), %r9             /* arg4 */
    movq -72(%r15), %rax
    pushq %rax                      /* arg5 on stack */

    call syscall_dispatch
    addq $8, %rsp                   /* clean up arg5 */

    /* Write return value into the GPR frame's rax slot.
     *
     * At this point (after the dispatch call + 8-byte arg5 cleanup),
     * rsp points at the top of the iret frame = [SS]. Going down the
     * stack: SS(+0) userRSP(+8) RFLAGS(+0x10) CS(+0x18) userRIP(+0x20)
     * [error](+0x28) [vector](+0x30) r15(+0x38) r14(+0x40) r13(+0x48)
     * r12(+0x50) rbp(+0x58) rbx(+0x60) r9(+0x68) r8(+0x70) rax(+0x78)
     * rcx(+0x80) rdx(+0x88) rsi(+0x90) rdi(+0x98)
     *
     * RAX is the 9th GPR pushed, so it lives at rsp + 0x78.
     * NOTE: the old code used 0x20, which is the user RIP slot — that
     * corrupted the return address and triple-faulted the process. */
    movq %rax, 0x78(%rsp)

    /* Phase 5: ISR epilogue + iretq back to ring 3 */
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
    addq $16, %rsp
    iretq
