/*
 * syscall.c - System call dispatch and handlers
 *
 * Configures IA32_STAR, IA32_LSTAR, IA32_FMASK MSRs for SYSCALL/SYSRET.
 * The SYSCALL entry point is in usermode.s; it saves registers and calls
 * syscall_dispatch() with the standard AMD64 syscall ABI.
 */
#include "syscall.h"
#include "../kernel/prism.h"
#include "../kernel/log.h"
#include "../drivers/serial.h"
#include "../task/task.h"
#include "../isr/isr.h"

/* MSR constants */
#define MSR_IA32_STAR    0xC0000081
#define MSR_IA32_LSTAR   0xC0000082
#define MSR_IA32_FMASK   0xC0000084

/* Forward declaration of the assembly entry point */
extern void syscall_entry(void);

/* Syscall handler function pointer table */
typedef uint64_t (*syscall_fn)(uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t);

static uint64_t sys_exit(uint64_t code, uint64_t b, uint64_t c,
                         uint64_t d, uint64_t e, uint64_t f) {
    (void)b; (void)c; (void)d; (void)e; (void)f;
    task_t* t = task_current();
    if (t) {
        t->state = TASK_DEAD;
        klog_hex(LOG_INFO, "syscall: exit pid=", t->pid);
        klog_hex(LOG_INFO, "  code=", code);
        /* Don't return — force a schedule. We're called from the syscall
         * entry path which will do a normal ISR-style return. Setting state
         * to DEAD means schedule() won't put us back on the ready list. */
        schedule();
        /* schedule() won't return here — we'll context_switch away and
         * this task is dead. But the compiler doesn't know that. */
    }
    return 0;
}

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len,
                          uint64_t d, uint64_t e, uint64_t f) {
    (void)d; (void)e; (void)f;
    /* For now, only fd 1 (stdout) goes to serial */
    if (fd != 1 || !buf || !len) return SYSCALL_ERR_INVAL;
    const char* data = (const char*)buf;
    for (uint64_t i = 0; i < len; i++) {
        serial_putc(data[i]);
    }
    return len;
}

static uint64_t sys_getpid(uint64_t a, uint64_t b, uint64_t c,
                           uint64_t d, uint64_t e, uint64_t f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    task_t* t = task_current();
    return t ? t->pid : 0;
}

static uint64_t sys_yield(uint64_t a, uint64_t b, uint64_t c,
                          uint64_t d, uint64_t e, uint64_t f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    schedule();
    return 0;
}

static uint64_t sys_sbrk(uint64_t a, uint64_t b, uint64_t c,
                         uint64_t d, uint64_t e, uint64_t f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    /* Stub — not implemented yet */
    return SYSCALL_ERR_NOSYS;
}

static uint64_t sys_test(uint64_t a, uint64_t b, uint64_t c,
                         uint64_t d, uint64_t e, uint64_t f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    return 0xDEAD;
}

static syscall_fn syscall_table[SYSCALL_COUNT] = {
    sys_exit,
    sys_write,
    sys_getpid,
    sys_yield,
    sys_sbrk,
    sys_test,
};

void syscall_init(void) {
    /* IA32_STAR layout:
     *   Bits 63:48 = SYSRET CS (CPU forces RPL=3, SS = this+8 with RPL=3)
     *   Bits 47:32 = SYSCALL CS (CPU forces RPL=0, SS = this+8 with RPL=0)
     *
     * Our GDT: kernel code=0x08, kernel data=0x10, user code=0x1B, user data=0x23
     * SYSCALL:  CS=STAR[47:32]=0x08, SS=0x08+8=0x10 ✓
     * SYSRET:   CS=STAR[63:48]=0x1B, SS=0x1B+8=0x23 ✓
     */
    uint64_t star = (0x1BULL << 48) | (0x08ULL << 32);
    uint64_t lstar = (uint64_t)syscall_entry;
    uint64_t fmask = 0x200;  /* Clear IF on SYSCALL to prevent nested interrupts */

    asm volatile(
        "wrmsr"
        :: "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32)), "c"(MSR_IA32_LSTAR)
    );
    asm volatile(
        "wrmsr"
        :: "a"((uint32_t)star), "d"((uint32_t)(star >> 32)), "c"(MSR_IA32_STAR)
    );
    asm volatile(
        "wrmsr"
        :: "a"((uint32_t)fmask), "d"((uint32_t)(fmask >> 32)), "c"(MSR_IA32_FMASK)
    );

    klog(LOG_INFO, "Syscall: MSRs configured (STAR/LSTAR/FMASK)\n");
}

uint64_t syscall_dispatch(uint64_t number, uint64_t arg0, uint64_t arg1,
                          uint64_t arg2, uint64_t arg3, uint64_t arg4,
                          uint64_t arg5) {
    if (number >= SYSCALL_COUNT) {
        return SYSCALL_ERR_NOSYS;
    }
    return syscall_table[number](arg0, arg1, arg2, arg3, arg4, arg5);
}
