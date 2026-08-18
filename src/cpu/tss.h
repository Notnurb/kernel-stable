/*
 * tss.h - Task State Segment for x86_64
 *
 * 64-bit TSS (104 bytes) provides:
 *   RSP0: Kernel stack pointer for ring 0 (used on ring 3→0 transitions)
 *   IST1-3: Interrupt Stack Tables (not used yet)
 *   IOPB: I/O Permission Bitmap offset (not used yet)
 */
#ifndef TSS_H
#define TSS_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint32_t reserved0;     /* bytes 0-3: Previous Task Link + reserved */
    uint64_t rsp0;          /* bytes 4-11: Ring 0 stack pointer */
    uint64_t rsp1;          /* bytes 12-19 */
    uint64_t rsp2;          /* bytes 20-27 */
    uint64_t reserved1;     /* bytes 28-35 */
    uint64_t ist1;          /* bytes 36-43: Interrupt Stack Table 1 */
    uint64_t ist2;          /* bytes 44-51 */
    uint64_t ist3;          /* bytes 52-59 */
    uint64_t reserved2;     /* bytes 60-67 */
    uint64_t reserved3;     /* bytes 68-75 */
    uint64_t reserved4;     /* bytes 76-83 */
    uint64_t reserved5;     /* bytes 84-91 */
    uint64_t reserved6;     /* bytes 92-99 */
    uint16_t iopb_offset;   /* bytes 100-101 */
    uint8_t  reserved7;     /* byte 102 */
} __attribute__((packed)) tss64_t;

/* Set RSP0 in the TSS (must be called before entering ring 3) */
void tss_set_rsp0(uint64_t rsp0);

/* Get current RSP0 value */
uint64_t tss_get_rsp0(void);

#endif /* TSS_H */
