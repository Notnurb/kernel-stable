/*
 * timer.h - PIT (Programmable Interval Timer) interface
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* PIT I/O ports */
#define PIT_CHANNEL0_CMD   0x40
#define PIT_COMMAND_PORT   0x43

/* PIT command byte components */
#define PIT_ACCESS_BOTH   0x30  /* Access mode: low byte then high byte */
#define PIT_MODE_RATE_GEN 0x06  /* Mode 2: rate generator */

/* Default timer frequency */
#define TIMER_FREQUENCY    100
#define PIT_BASE_FREQUENCY 1193180

/* Timer ticks counter — incremented by the ISR in isr.c
 * Declared extern so other modules can access it */
extern uint64_t timer_ticks;

void kinit_timer(void);
void timer_wait(uint32_t ticks);
uint64_t timer_get_ticks(void);

#endif /* TIMER_H */
