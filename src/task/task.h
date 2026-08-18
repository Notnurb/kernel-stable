/*
 * task.h - Kernel task/thread management
 *
 * Implements preemptive multitasking via kernel threads.
 * Each task has its own kernel stack and TCB (Thread Control Block).
 * Context switching saves/restores callee-saved registers + RSP;
 * the ISR frame on the stack serves as the full saved CPU state
 * for preemptive switches from timer interrupts.
 */
#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

#define TASK_STACK_SIZE  8192   /* 8 KiB kernel stack per thread */
#define MAX_TASKS        32
#define TIME_SLICE       10     /* ticks per task (10 = 100ms at 100Hz) */

typedef enum {
    TASK_DEAD = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
} task_state_t;

typedef struct task {
    uint64_t        pid;
    task_state_t    state;
    uint64_t*       stack_pointer;  /* Saved RSP (points into kernel stack or ISR frame) */
    uint64_t*       kernel_stack;   /* Base of allocated kernel stack (for kfree) */
    uint64_t        time_slice;     /* Remaining ticks before preemption */
    struct task*    next;           /* Linked list pointer for scheduler */
    uint8_t         ring;           /* 0 = kernel task, 3 = user task */
    uint8_t         _pad[7];        /* align to 8 bytes */
    uint64_t        cr3;            /* PML4 physical address (0 = use kernel PML4) */
    uint64_t        user_rip;       /* Ring 3 entry point */
    uint64_t        user_rsp;       /* Ring 3 stack pointer */
    uint64_t        user_rflags;    /* Initial RFLAGS for ring 3 */
} task_t;

/* Initialize the task system. Creates boot task (PID 1) and idle task. */
void kinit_task(void);

/* Create a new kernel thread running `entry`. Returns the new task, or NULL. */
task_t* task_create(void (*entry)(void));

/* Create a new user-mode task. Returns the new task, or NULL.
 * user_rip: entry point in ring 3, user_rsp: initial ring 3 stack,
 * cr3: physical address of user PML4 (0 for kernel page tables). */
task_t* task_create_user(uint64_t user_rip, uint64_t user_rsp,
                         uint64_t user_rflags, uint64_t cr3);

/* Called from timer IRQ. Decrement time slice, preempt if expired. */
void schedule(void);

/* Cooperative yield — give up remaining time slice to next ready task. */
void task_yield(void);

/* Get the currently running task */
task_t* task_current(void);

/* Context switch: save old task's callee-saved state, restore new's.
 * Implemented in switch.s. */
void context_switch(uint64_t** old_sp, uint64_t* new_sp);

#endif /* TASK_H */
