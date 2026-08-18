/*
 * log.h - Kernel logging and panic system
 *
 * Provides log levels (DEBUG, INFO, WARN, ERROR) that output to serial,
 * and a panic function that dumps to both serial and VGA then halts.
 *
 * This is the primary debug interface — serial output is way more
 * reliable than VGA for logs (no cursor issues, works before VGA init).
 */
#ifndef LOG_H
#define LOG_H

#include <stdint.h>

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_PANIC,
} log_level_t;

/* Initialize the logging system */
void kinit_log(void);

/* Log a message with a level prefix to serial.
 * Format: "[LEVEL] message\n" */
void klog(log_level_t level, const char* msg);

/* Log a hex value with a level prefix.
 * Format: "[LEVEL] msg 0xHEXVAL\n" */
void klog_hex(log_level_t level, const char* msg, uint64_t val);

/* Kernel panic — print to both serial and VGA, then halt.
 * Takes a short message string. */
void kpanic(const char* msg);

/* Kernel panic with a hex value for extra context.
 * Format: "PANIC: msg 0xHEXVAL" */
void kpanic_hex(const char* msg, uint64_t val);

#endif /* LOG_H */
