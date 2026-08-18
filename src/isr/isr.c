/*
 * isr.c - C-level interrupt service routines
 *
 * Called from isr_common_handler in isr_asm.s.
 * Dispatches to registered handler for hardware IRQs, handles exceptions.
 */
#include "../kernel/prism.h"
#include "isr.h"
#include "../cpu/idt.h"
#include "../cpu/pic.h"
#include "../drivers/serial.h"

/*
 * isr_frame_t - The stack layout when an ISR fires.
 * Matches the layout expected by isr_common_stub in isr_asm.s.
 *
 * Push order (first pushed = highest address, last pushed = lowest):
 *   [frame + 0x60]  rdi   (pushed first)
 *   [frame + 0x58]  rsi
 *   [frame + 0x50]  rdx
 *   [frame + 0x48]  rcx
 *   [frame + 0x40]  rax
 *   [frame + 0x38]  r8
 *   [frame + 0x30]  r9
 *   [frame + 0x28]  rbx
 *   [frame + 0x20]  rbp
 *   [frame + 0x18]  r12
 *   [frame + 0x10]  r13
 *   [frame + 0x08]  r14
 *   [frame + 0x00]  r15   (pushed last, rsp points here)
 *   [frame + 0x68]  vector number  (pushed by stub before GPRs)
 *   [frame + 0x70]  error code     (pushed by stub/CPU before vector)
 *   ... CPU-pushed: RIP, CS, RFLAGS, SS, RSP
 */
typedef struct isr_frame {
    uint64_t r15, r14, r13, r12, rbp, rbx, r9, r8;
    uint64_t rax, rcx, rdx, rsi, rdi;
} isr_frame_t;

/* IRQ handler dispatch table (IRQ 0-15) */
static irq_handler_fn irq_handlers[16] = {0};

void isr_register_irq(uint8_t irq, irq_handler_fn handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void isr_unregister_irq(uint8_t irq) {
    if (irq < 16) {
        irq_handlers[irq] = 0;
    }
}

/*
 * isr_handler_c - Called from assembly for all interrupts.
 */
void isr_handler_c(isr_frame_t* frame) {
    uint64_t* sp = (uint64_t*)frame;
    uint8_t vector = (uint8_t)(sp[13]);   /* vector at frame + 0x68 */
    uint64_t error_code = sp[14];          /* error code at frame + 0x70 */

    /* Hardware interrupts (vectors 32-47) */
    if (vector >= 32) {
        uint8_t irq = vector - 32;
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }
        pic_send_eoi(irq);
        return;
    }

    /* CPU exceptions (vectors 0-31) */
    static const char* exceptions[] = {
        "Div By Zero", "Debug", "NMI", "Breakpoint", "Overflow",
        "Bounds", "Inv Opcode", "Coprocessor", "Double Fault",
        "Copro Overrun", "Invalid TSS", "Seg Not Pres", "Stack Fault",
        "GPF", "Page Fault", "x87 FP", "Alignment", "MChi",
        "SIMD FP", "Virt", "Control Prot"
    };

    serial_puts("\n!!! KERNEL PANIC: CPU Exception !!!\n");
    serial_puts("  vector: 0x");
    {
        char buf[3] = "00";
        buf[0] = "0123456789ABCDEF"[(vector >> 4) & 0xF];
        buf[1] = "0123456789ABCDEF"[vector & 0xF];
        serial_puts(buf);
    }
    serial_puts(" (");
    serial_puts((vector < 21) ? exceptions[vector] : "Reserved");
    serial_puts(")\n  error:  ");
    serial_puthex64(error_code);
    serial_puts("\n");

    /* For page faults, print CR2 (faulting address) and frame info */
    {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_puts("  CR2:    0x");
        serial_puthex64(cr2);
        serial_puts("\n");
    }
    /* CPU frame: sp[0..4] = RIP, CS, RFLAGS, RSP, SS */
    serial_puts("  RIP:    0x");
    serial_puthex64(sp[15]);
    serial_puts("\n  CS:     0x");
    serial_puthex64(sp[16]);
    serial_puts("\n  RFLAGS: 0x");
    serial_puthex64(sp[17]);
    serial_puts("\n  RSP:    0x");
    serial_puthex64(sp[18]);
    serial_puts("\n  SS:     0x");
    serial_puthex64(sp[19]);
    serial_puts("\n");

    kprint("\n!!! PANIC !!!\n");
    kprint("CPU Exception: vector ");
    kprint_hex(vector);
    kprint(" (");
    kprint((vector < 21) ? exceptions[vector] : "Reserved");
    kprint(") err=");
    kprint_hex(error_code);
    kprint("\nHalting.\n");

    for (;;) {
        asm volatile("hlt" ::: "memory");
    }
}
