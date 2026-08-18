/*
 * serial.h - Serial port (COM1) driver for kernel debug output
 */
#ifndef SERIAL_H
#define SERIAL_H

#include "../cpu/pic.h"
#include <stdint.h>

#define SERIAL_COM1 0x3F8

static inline void serial_init(void) {
    outb(SERIAL_COM1 + 1, 0x00);  /* Disable interrupts */
    outb(SERIAL_COM1 + 3, 0x80);  /* Enable DLAB */
    outb(SERIAL_COM1 + 0, 0x03);  /* Set divisor lo (38400 baud) */
    outb(SERIAL_COM1 + 1, 0x00);  /* Set divisor hi */
    outb(SERIAL_COM1 + 3, 0x03);  /* 8 bits, no parity, one stop */
    outb(SERIAL_COM1 + 2, 0xC7);  /* Enable FIFO */
    outb(SERIAL_COM1 + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

static inline int serial_tx_ready(void) {
    return inb(SERIAL_COM1 + 5) & 0x20;
}

static inline void serial_putc(char c) {
    while (!serial_tx_ready());
    outb(SERIAL_COM1, (uint8_t)c);
}

static inline void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static inline void serial_puthex64(uint64_t val) {
    const char hex[] = "0123456789abcdef";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        serial_putc(hex[(val >> i) & 0xF]);
}

#endif /* SERIAL_H */
