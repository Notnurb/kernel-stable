/*
 * task.c - Kernel task management and round-robin scheduler
 *
 * Each task is a kernel thread with its own stack.
 * User-mode tasks additionally have a per-process address space (CR3),
 * user RIP/RSP/RFLAGS for ring 3 entry, and ring=3 flag.
 * The scheduler uses a circular linked list of READY tasks.
 * Preemption happens on timer IRQ (100 Hz) via schedule().
 *
 * How context switching works:
 *   1. Timer ISR fires → isr_common_stub saves all registers + ISR frame
 *   2. timer_irq_handler calls schedule()
 *   3. schedule() picks the next READY task, sets TSS.RSP0 for user tasks
 *   4. context_switch saves callee-saved regs + RSP of old task,
 *      loads the same from new task, and returns
 *   5. The ISR returns via iretq, restoring the new task's full state
 *
 * For user tasks, the return path goes through enter_user_mode which
 * does iretq to ring 3. Subsequent preemptions return via the normal
 * ISR path (iretq handles ring 0→3).
 */
#include "task.h"
#include "../mm/heap.h"
#include "../cpu/gdt.h"
#include "../kernel/prism.h"
#include "../kernel/log.h"
#include "../drivers/serial.h"

/* Task table — statically allocated, avoids malloc issues early on */
static task_t task_table[MAX_TASKS];
static uint64_t next_pid = 1;

/* Scheduler state */
static task_t* current_task = NULL;
static task_t* ready_head  = NULL;  /* head of READY circular list */
static task_t* ready_tail  = NULL;  /* tail for O(1) append */

/* ---- Internal helpers ---- */

static void ready_list_add(task_t* t) {
    t->next = NULL;
    if (!ready_head) {
        ready_head = ready_tail = t;
    } else {
        ready_tail->next = t;
        ready_tail = t;
    }
}

static task_t* ready_list_pop(void) {
    task_t* t = ready_head;
    if (t) {
        ready_head = t->next;
        if (!ready_head) ready_tail = NULL;
        t->next = NULL;
    }
    return t;
}

/* ---- Idle task ---- */

static void idle_entry(void) {
    for (;;) {
        asm volatile("hlt" ::: "memory");
    }
}

/* ---- Public API ---- */

void kinit_task(void) {
    /* Zero out the task table */
    for (int i = 0; i < MAX_TASKS; i++) {
        task_table[i] = (task_t){0};
    }

    /* Boot task: PID 1, uses the existing kernel stack.
     * Its stack_pointer will be set when it is first preempted
     * (the ISR frame becomes its saved state). */
    task_t* boot = &task_table[0];
    boot->pid = next_pid++;
    boot->state = TASK_RUNNING;
    boot->kernel_stack = NULL;  /* not allocated — uses boot stack */
    boot->stack_pointer = NULL; /* set on first context switch away */
    boot->time_slice = TIME_SLICE;
    boot->ring = 0;
    boot->cr3 = 0;
    current_task = boot;

    /* Idle task: PID 2, lowest priority, just halts */
    task_t* idle = &task_table[1];
    idle->pid = next_pid++;
    idle->state = TASK_READY;
    idle->time_slice = TIME_SLICE;

    /* Allocate a stack for the idle task */
    uint64_t* stack = (uint64_t*)kmalloc(TASK_STACK_SIZE);
    idle->kernel_stack = stack;

    /* Set up initial stack frame so context_switch "returns" to idle_entry.
     * Stack grows downward. Layout (high → low):
     *   [entry] [rbp=0] [rbx=0] [r12=0] [r13=0] [r14=0] [r15=0] ← RSP
     */
    uint64_t* sp = stack + (TASK_STACK_SIZE / sizeof(uint64_t));
    *(--sp) = (uint64_t)idle_entry;  /* return address */
    *(--sp) = 0x200;  /* RFLAGS: IF=1 (bit 9) so interrupts are enabled */
    *(--sp) = 0;  /* rbp */
    *(--sp) = 0;  /* rbx */
    *(--sp) = 0;  /* r12 */
    *(--sp) = 0;  /* r13 */
    *(--sp) = 0;  /* r14 */
    *(--sp) = 0;  /* r15 */
    idle->stack_pointer = sp;

    ready_list_add(idle);

    klog(LOG_INFO, "Task: scheduler initialized (boot=1 idle=2)\n");
}

task_t* task_create(void (*entry)(void)) {
    /* Find a free slot */
    task_t* t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_DEAD) {
            t = &task_table[i];
            break;
        }
    }
    if (!t) return NULL;

    /* Allocate kernel stack */
    uint64_t* stack = (uint64_t*)kmalloc(TASK_STACK_SIZE);
    if (!stack) return NULL;

    /* Set up initial frame (same layout as context_switch expects) */
    uint64_t* sp = stack + (TASK_STACK_SIZE / sizeof(uint64_t));
    *(--sp) = (uint64_t)entry;  /* return address */
    *(--sp) = 0x200;  /* RFLAGS: IF=1 (bit 9) so interrupts are enabled */
    *(--sp) = 0;  /* rbp */
    *(--sp) = 0;  /* rbx */
    *(--sp) = 0;  /* r12 */
    *(--sp) = 0;  /* r13 */
    *(--sp) = 0;  /* r14 */
    *(--sp) = 0;  /* r15 */

    t->pid = next_pid++;
    t->state = TASK_READY;
    t->kernel_stack = stack;
    t->stack_pointer = sp;
    t->time_slice = TIME_SLICE;
    t->ring = 0;
    t->cr3 = 0;
    t->next = NULL;

    ready_list_add(t);

    klog_hex(LOG_DEBUG, "task_create pid=", t->pid);
    return t;
}

/*
 * task_create_user - Create a user-mode task (ring 3).
 *
 * The task's kernel stack is set up so that context_switch "returns"
 * to enter_user_mode, which does the actual ring 0→3 transition.
 */
task_t* task_create_user(uint64_t user_rip, uint64_t user_rsp,
                         uint64_t user_rflags, uint64_t cr3) {
    /* Find a free slot */
    task_t* t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_DEAD) {
            t = &task_table[i];
            break;
        }
    }
    if (!t) return NULL;

    /* Allocate kernel stack */
    uint64_t* stack = (uint64_t*)kmalloc(TASK_STACK_SIZE);
    if (!stack) return NULL;

    /* Set up initial frame so context_switch returns to enter_user_mode.
     * enter_user_mode will read user_rip/rsp/rflags from the task struct,
     * set TSS.RSP0, and do iretq to ring 3. */
    extern void enter_user_mode(void);
    uint64_t* sp = stack + (TASK_STACK_SIZE / sizeof(uint64_t));
    *(--sp) = (uint64_t)enter_user_mode;  /* return address */
    *(--sp) = 0x200;  /* RFLAGS: IF=1 */
    *(--sp) = 0;  /* rbp */
    *(--sp) = 0;  /* rbx */
    *(--sp) = 0;  /* r12 */
    *(--sp) = 0;  /* r13 */
    *(--sp) = 0;  /* r14 */
    *(--sp) = 0;  /* r15 */

    t->pid = next_pid++;
    t->state = TASK_READY;
    t->kernel_stack = stack;
    t->stack_pointer = sp;
    t->time_slice = TIME_SLICE;
    t->ring = 3;
    t->cr3 = cr3;
    t->user_rip = user_rip;
    t->user_rsp = user_rsp;
    t->user_rflags = user_rflags;
    t->next = NULL;

    ready_list_add(t);

    klog_hex(LOG_INFO, "task_create_user pid=", t->pid);
    return t;
}

/*
 * schedule - Called from timer IRQ or task_yield().
 *
 * Picks the next READY task and context-switches to it.
 * The current task goes back to the READY list if still alive.
 *
 * For user tasks, TSS.RSP0 is set to the new task's kernel stack top
 * BEFORE the context switch, so that any subsequent interrupt from
 * ring 3 uses the correct kernel stack.
 */
extern void tss_set_rsp0(uint64_t rsp0);

void schedule(void) {
    if (!current_task) return;

    /* Find next task in the round-robin list */
    task_t* next = ready_list_pop();
    if (!next) return;  /* nothing else to run */

    task_t* old = current_task;

    /* Put the old task back in the ready list if it's still alive */
    if (old->state == TASK_RUNNING) {
        old->state = TASK_READY;
        old->time_slice = TIME_SLICE;
        ready_list_add(old);
    }

    /* Switch to the new task */
    current_task = next;
    next->state = TASK_RUNNING;
    next->time_slice = TIME_SLICE;

    /* Set TSS.RSP0 for user tasks so ring 3→0 interrupts use the right stack */
    if (next->ring == 3) {
        tss_set_rsp0((uint64_t)next->kernel_stack + TASK_STACK_SIZE);
    }

    context_switch(&old->stack_pointer, next->stack_pointer);
}

void task_yield(void) {
    if (!current_task || current_task->state != TASK_RUNNING) return;
    current_task->state = TASK_READY;
    current_task->time_slice = TIME_SLICE;
    ready_list_add(current_task);
    schedule();
}

task_t* task_current(void) {
    return current_task;
}
