/*
 * timer.c - PIT (Programmable Interval Timer) driver
 *
 * PIT channel 0 fires via IRQ 0 (vector 32 after PIC remapping).
 * Configured for 100 Hz (every 10ms).
 * Registers its handler via the ISR dispatch table.
 * Calls schedule() on each tick for preemptive multitasking.
 */
#include "timer.h"
#include "../isr/isr.h"
#include "../task/task.h"
#include "../cpu/pic.h"
#include "../kernel/prism.h"

uint64_t timer_ticks = 0;

static void timer_irq_handler(void) {
    timer_ticks++;
    /* EOI must be sent BEFORE schedule() context-switches.
     * If we switch tasks first, the PIC blocks further IRQs until
     * we eventually switch back and isr_handler_c sends EOI —
     * which means no more timer ticks until then. */
    pic_send_eoi(0);
    schedule();
}

void kinit_timer(void) {
    uint32_t divisor = 1193180 / 100;  /* 100 Hz */

    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_CMD, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_CMD, (uint8_t)((divisor >> 8) & 0xFF));

    isr_register_irq(0, timer_irq_handler);

    kprint("Timer: PIT ch0 @ 100Hz configured\n");
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

void timer_wait(uint32_t ticks) {
    uint64_t start = timer_ticks;
    while ((timer_ticks - start) < ticks) {
        asm volatile("hlt" ::: "memory");
    }
}
