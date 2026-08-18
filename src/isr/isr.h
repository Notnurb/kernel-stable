/*
 * isr.h - Interrupt Service Routine interface
 *
 * Provides IRQ handler registration so drivers can hook hardware
 * interrupts without modifying the core ISR dispatcher.
 */
#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* IRQ handler callback type (no args, no return) */
typedef void (*irq_handler_fn)(void);

/* Register a handler for hardware IRQ 0-15.
 * IRQ 0 = timer, IRQ 1 = keyboard, etc.
 * Handler is called with interrupts disabled; must send EOI itself
 * or let the ISR dispatcher handle it. */
void isr_register_irq(uint8_t irq, irq_handler_fn handler);

/* Unregister a handler for the given IRQ */
void isr_unregister_irq(uint8_t irq);

#endif /* ISR_H */
