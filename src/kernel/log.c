/*
 * log.c - Kernel logging and panic system
 *
 * All log output goes to serial (COM1). Panic output also goes to VGA.
 * Serial is the primary debug channel — it's reliable, timestamp-free,
 * and works before VGA is initialized.
 */
#include "log.h"
#include "prism.h"
#include "../drivers/serial.h"

static const char* level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "PANIC"
};

/* Minimum log level — messages below this are filtered out.
 * Set to LOG_DEBUG during development, LOG_INFO for production. */
static log_level_t min_level = LOG_DEBUG;

void kinit_log(void) {
    klog(LOG_INFO, "Log system initialized\n");
}

void klog(log_level_t level, const char* msg) {
    if (level < min_level) return;

    serial_puts("[");
    serial_puts(level_names[level]);
    serial_puts("] ");
    serial_puts(msg);
}

void klog_hex(log_level_t level, const char* msg, uint64_t val) {
    if (level < min_level) return;

    serial_puts("[");
    serial_puts(level_names[level]);
    serial_puts("] ");
    serial_puts(msg);
    serial_puthex64(val);
    serial_puts("\n");
}

/*
 * kpanic - Kernel panic.
 *
 * Prints the panic message to both serial and VGA in a visually
 * distinct way, then halts the CPU forever.
 */
void kpanic(const char* msg) {
    /* Disable interrupts so nothing can preempt us */
    asm volatile("cli" ::: "memory");

    /* Serial output */
    serial_puts("\n\n");
    serial_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    serial_puts("!!!           KERNEL PANIC          !!!\n");
    serial_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    serial_puts("  ");
    serial_puts(msg);
    serial_puts("\n");
    serial_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    /* VGA output */
    kprint("\n\n");
    kprint("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprint("!!!        KERNEL PANIC             !!!\n");
    kprint("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprint("  ");
    kprint(msg);
    kprint("\n");
    kprint("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    for (;;) {
        asm volatile("hlt" ::: "memory");
    }
}

void kpanic_hex(const char* msg, uint64_t val) {
    asm volatile("cli" ::: "memory");

    serial_puts("\n\n");
    serial_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    serial_puts("!!!           KERNEL PANIC          !!!\n");
    serial_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    serial_puts("  ");
    serial_puts(msg);
    serial_puts(" 0x");
    /* Inline hex output for serial */
    {
        const char hex[] = "0123456789ABCDEF";
        char buf[17] = "0000000000000000";
        for (int i = 15; i >= 0; i--) {
            buf[15 - i] = hex[(val >> (i * 4)) & 0xF];
        }
        serial_puts(buf);
    }
    serial_puts("\n");
    serial_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    kprint("\n\n");
    kprint("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprint("!!!        KERNEL PANIC             !!!\n");
    kprint("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprint("  ");
    kprint(msg);
    kprint(" 0x");
    kprint_hex(val);
    kprint("\n");
    kprint("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    for (;;) {
        asm volatile("hlt" ::: "memory");
    }
}
