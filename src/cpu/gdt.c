/*
 * gdt.c - Global Descriptor Table implementation
 *
 * In 64-bit long mode, the GDT is simplified:
 *   - Base is always 0 (flat memory model, paging handles addressing)
 *   - Only the access and flag bits matter
 *
 * GDT layout (7 entries, 56 bytes):
 *   0x00: Null descriptor
 *   0x08: Kernel code (P=1, DPL=0, exec/read, L=1)
 *   0x10: Kernel data (P=1, DPL=0, data/write)
 *   0x18: User code  (P=1, DPL=3, exec/read, L=1)
 *   0x20: User data  (P=1, DPL=3, data/write)
 *   0x28: TSS low    (P=1, DPL=0, type=TSS available, 16 bytes)
 *   0x30: TSS high   (base address bits 32-63)
 *
 * TSS provides RSP0 (kernel stack pointer for ring 0 on ring 3 transitions)
 * and is dynamically updated by tss_set_rsp0() before entering user mode.
 */
#include "gdt.h"
#include "tss.h"
#include <string.h>
#include <stdint.h>

#define GDT_ENTRIES 7

/* GDT pointer structure */
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* GDT entries as raw uint64_t for easy construction */
static uint64_t gdt_entries[GDT_ENTRIES];

/* TSS — proper packed struct for correct field offsets.
 * Non-static so idt.c can configure IST stacks via its extern reference. */
tss64_t tss_value;

static struct gdt_ptr gdt_register;

void tss_set_rsp0(uint64_t rsp0) {
    tss_value.rsp0 = rsp0;
}

uint64_t tss_get_rsp0(void) {
    return tss_value.rsp0;
}

void kinit_gdt(void) {
    /* Clear GDT and TSS */
    memset(gdt_entries, 0, sizeof(gdt_entries));
    memset(&tss_value, 0, sizeof(tss_value));

    /*
     * GDT Entry 0 (0x00): Null descriptor — stays zeroed.
     * GDT Entry 1 (0x08): Kernel code segment.
     *   Access = 0x9A (P=1, DPL=0, S=1, executable, readable)
     *   Granularity byte = 0x20 (limit high=0, G=0, L=1, D=0)
     *   Full value: 0x00AF9A000000FFFF
     */
    gdt_entries[1] = 0x00AF9A000000FFFF;

    /*
     * GDT Entry 2 (0x10): Kernel data segment.
     *   Access = 0x92 (P=1, DPL=0, S=1, data, writable)
     *   Granularity byte = 0x00
     *   Full value: 0x00AF92000000FFFF
     */
    gdt_entries[2] = 0x00AF92000000FFFF;

    /*
     * GDT Entry 3 (0x18): User code segment.
     *   Access = 0xFA (P=1, DPL=3, S=1, executable, readable)
     *   Granularity byte = 0x20 (L=1 for 64-bit code)
     */
    gdt_entries[3] = 0x00AF9A000000FFFF;
    gdt_entries[3] = (gdt_entries[3] & 0xFFFFFFFFFFFF00FFULL) | (0xFAULL << 40);

    /*
     * GDT Entry 4 (0x20): User data segment.
     *   Access = 0xF2 (P=1, DPL=3, S=1, data, writable)
     */
    gdt_entries[4] = 0x00AF92000000FFFF;
    gdt_entries[4] = (gdt_entries[4] & 0xFFFFFFFFFFFF00FFULL) | (0xF2ULL << 40);

    /*
     * GDT Entries 5-6 (0x28-0x30): TSS descriptor (16 bytes = 2 entries)
     *
     * The TSS is a 104-byte structure. The descriptor encodes:
     *   - limit (103 = sizeof(tss64_t) - 1)
     *   - base address (64-bit, pointer to tss_value)
     *   - access = 0x89 (P=1, DPL=0, S=0, Type=9 = available 64-bit TSS)
     *
     * RSP0 is set dynamically by tss_set_rsp0() before entering ring 3.
     */
    uint64_t tss_base = (uint64_t)&tss_value;
    uint16_t tss_limit = sizeof(tss64_t) - 1;

    /* TSS low descriptor (entry 5) */
    gdt_entries[5] = (uint64_t)(tss_limit & 0xFFFF)         |
                     ((tss_base & 0xFFFF) << 16)             |
                     (((tss_base >> 16) & 0xFF) << 32)      |
                     (0x89ULL << 40)                         |
                     (((tss_base >> 24) & 0xFF) << 56);

    /* TSS high descriptor (entry 6) */
    gdt_entries[6] = (tss_base >> 32) & 0xFFFFFFFF;

    /* Load the GDT */
    gdt_register.limit = (uint16_t)(sizeof(gdt_entries) - 1);
    gdt_register.base  = (uint64_t)gdt_entries;
    asm volatile("lgdt %0" : : "m"(gdt_register));

    /* Reload segment registers */
    asm volatile(
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        :
        : "ax", "memory"
    );

    /* Load TSS into Task Register (TR) via LTR instruction */
    asm volatile("movw $0x28, %%ax\n\t"
                 "ltr %%ax"
                 :
                 :
                 : "ax", "memory");

    /* Far jump to reload CS (selector 0x08 = kernel code) */
    asm volatile(
        "pushq $0x08\n\t"
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        :
        :
        : "rax", "memory"
    );
}
