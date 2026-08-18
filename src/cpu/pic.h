/*
 * pic.h - Programmable Interrupt Controller interface
 */
#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* PIC I/O ports */
#define PIC_MASTER_CMD   0x20
#define PIC_MASTER_DATA  0x21
#define PIC_SLAVE_CMD    0xA0
#define PIC_SLAVE_DATA   0xA1

/* PIC vector offsets after remapping */
#define PIC_MASTER_OFFSET 32
#define PIC_SLAVE_OFFSET  40

void kinit_pic(void);
void pic_send_eoi(uint8_t irq);

/* Outb/inb I/O port helpers */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

#endif /* PIC_H */
