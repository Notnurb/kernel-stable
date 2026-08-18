/*
 * idt.h - Interrupt Descriptor Table interface
 */
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Number of entries in the IDT */
#define IDT_ENTRIES 256

/* Interrupt handler function type */
typedef void (*isr_handler_t)(void);

/* Register an interrupt handler.
 *   n      - vector number
 *   handler- handler address
 *   selector- code segment (0x08 = kernel code)
 *   flags  - gate type/attr (0x8E = 64-bit interrupt gate, present)
 *   ist    - Interrupt Stack Table index (0 = normal RSP0/TSS switch,
 *            1..7 = use TSS.istN). Use a non-zero IST for #DF and #PF so
 *            that a fault on the kernel/ring-3 stack can't double-fault into
 *            a triple fault. */
void idt_set_gate(uint8_t n, uint64_t handler, uint16_t selector,
                  uint8_t flags, uint8_t ist);

/* Initialize the IDT */
void kinit_idt(void);

#endif /* IDT_H */
