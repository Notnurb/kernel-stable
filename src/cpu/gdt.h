/*
 * gdt.h - Global Descriptor Table interface
 */
#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* GDT entry indices */
#define GDT_NULL   0
#define GDT_KCODE  1
#define GDT_KDATA  2
#define GDT_UCODE  3
#define GDT_UDATA  4
#define GDT_TSS    5
#define GDT_TSS_HI 6  /* TSS spans two entries (16 bytes) */

/* GDT selector values (shifted to leave RPL bits) */
#define GDT_SEL_NULL   0x00
#define GDT_SEL_KCODE  0x08
#define GDT_SEL_KDATA  0x10
#define GDT_SEL_UCODE  0x18
#define GDT_SEL_UDATA  0x20
#define GDT_SEL_TSS    0x28

void kinit_gdt(void);

/* Dynamic TSS access — defined in gdt.c */
#include "tss.h"

#endif /* GDT_H */
