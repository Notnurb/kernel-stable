/*
 * idt.c - Interrupt Descriptor Table implementation
 *
 * The IDT maps interrupt vectors (0-255) to handler functions.
 *
 * IDT Entry format (64-bit):
 *   Bits 0-15:   Low 16 bits of handler address
 *   Bits 16-31:  Code segment selector
 *   Bits 32-39:  Zero (reserved, except for types 14/15 where it's the IST)
 *   Bits 40-43:  Gate type and attributes (bits 0-3 of this byte hold the type)
 *                 Bit 4-5: DPL (Descriptor Privilege Level)
 *                 Bit 7: P (Present bit)
 *   Bits 44-63:  High 48 bits of handler address
 *
 * For 64-bit interrupts, we use 64-bit interrupt gates (type 14 = 0x8E).
 *
 * CPU exceptions (0-31) are hardwired. IRQs 32-47 come from the PIC after remapping.
 */
#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "pmm.h"
#include "paging.h"
#include <string.h>
#include "../kernel/prism.h"

/* IDT entry structure (packed, 16 bytes) */
struct idt_entry {
    uint16_t base_low;       /* Bits 0-15 of handler address */
    uint16_t selector;       /* Code segment selector in GDT */
    uint8_t  ist;            /* IST (bits 0-2) + reserved (bits 3-7) */
    uint8_t  flags;          /* Type and attributes */
    uint16_t base_mid;       /* Bits 16-31 of handler address */
    uint32_t base_high;      /* Bits 32-63 of handler address */
    uint32_t reserved;       /* Must be zero */
} __attribute__((packed));

/* Dedicated IST stacks for fault handlers that must not use the faulting
 * task's stack (otherwise a fault on the stack => double fault => triple
 * fault). Sized 4 KiB each, allocated from the PMM and identity-mapped. */
#define IST_COUNT 3          /* IST1, IST2, IST3 */
#define IST_STACK_PAGES 1    /* 4 KiB per IST stack */
static uint8_t* ist_stacks[IST_COUNT];

/* IST index assignments (TSS.istN, 1-based) */
#define IST_DF   1   /* Double fault — most critical, never reuse */
#define IST_PF   2   /* Page fault */
#define IST_GP   3   /* General protection fault */

/* Allocate + map a 4KiB IST stack, return its TOP virtual address. */
static uint64_t ist_alloc(uint8_t idx) {
    phys_addr_t frame = pmm_alloc_frame();
    if (!frame) return 0;
    /* Identity-mapped region (first 2MiB) is always accessible. */
    uint64_t va = frame + HIGHER_HALF_BASE;
    memset((void*)va, 0, PMM_FRAME_SIZE);
    ist_stacks[idx - 1] = (uint8_t*)va;
    return va + PMM_FRAME_SIZE;   /* stack grows downward */
}

/* IDT pointer (loaded by lidt) */
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* The IDT array (256 entries) */
static struct idt_entry idt_entries[IDT_ENTRIES];
static struct idt_ptr   idt_register;

/*
 * idt_set_gate - Install an interrupt handler in the IDT.
 *
 * Parameters:
 *   n       - Interrupt vector number (0-255)
 *   handler - Address of the handler function
 *   selector- Code segment selector (0x08 = kernel code)
 *   flags   - Gate type and attributes (0x8E = 64-bit interrupt gate, present)
 */
void idt_set_gate(uint8_t n, uint64_t handler, uint16_t selector, uint8_t flags, uint8_t ist) {
    idt_entries[n].base_low    = (uint16_t)(handler & 0xFFFF);
    idt_entries[n].selector    = selector;
    idt_entries[n].ist         = ist & 0x7;
    idt_entries[n].flags       = flags;
    idt_entries[n].base_mid    = (uint16_t)((handler >> 16) & 0xFFFF);
    idt_entries[n].base_high   = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt_entries[n].reserved    = 0;
}

/*
 * Default exception handler — just prints which exception occurred.
 * In a real kernel, each exception handler would be unique.
 *
 * For now, all unhandled exceptions print a message and halt.
 * The actual ISR stubs are in isr_asm.s.
 */
void isr_default_handler(void) {
    /* This will be called from assembly stubs on exception */
    kprint("EXCEPTION: Unhandled interrupt\n");
    kprint("Halting CPU.\n");
    for (;;) {
        asm volatile("hlt" ::: "memory");
    }
}

/*
 * Timer interrupt handler (IRQ 32 after PIC remapping)
 */
void isr_timer_handler(void) {
    pic_send_eoi(0);
}

/*
 * kinit_idt - Set up the IDT and load it.
 */
void kinit_idt(void) {
    idt_register.limit = sizeof(idt_entries) - 1;
    idt_register.base  = (uint64_t)&idt_entries;

    /* Clear the IDT entries */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_entries[i] = (struct idt_entry){0, 0, 0, 0, 0, 0, 0};
    }

    /*
     * Register CPU exception handlers (vectors 0-31).
     * These are defined in isr_asm.s as individual stubs that push the
     * vector number and call isr_handler_c.
     *
     * Flag 0x8E = 64-bit interrupt gate, present, ring 0
     */
    extern void isr_0(void);  extern void isr_1(void);  extern void isr_2(void);
    extern void isr_3(void);  extern void isr_4(void);  extern void isr_5(void);
    extern void isr_6(void);  extern void isr_7(void);  extern void isr_8(void);
    extern void isr_9(void);  extern void isr_10(void); extern void isr_11(void);
    extern void isr_12(void); extern void isr_13(void); extern void isr_14(void);
    extern void isr_15(void); extern void isr_16(void); extern void isr_17(void);
    extern void isr_18(void); extern void isr_19(void); extern void isr_20(void);
    extern void isr_21(void); extern void isr_22(void); extern void isr_23(void);
    extern void isr_24(void); extern void isr_25(void); extern void isr_26(void);
    extern void isr_27(void); extern void isr_28(void); extern void isr_29(void);
    extern void isr_30(void); extern void isr_31(void);

    void* exception_stubs[32] = {
        isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
        isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
        isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
        isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    };

    /* Allocate dedicated IST stacks BEFORE wiring. These run with interrupts
     * disabled and must be valid even if the faulting task's own stack is
     * corrupted. */
    uint64_t df_top = ist_alloc(IST_DF);
    uint64_t pf_top = ist_alloc(IST_PF);
    uint64_t gp_top = ist_alloc(IST_GP);

    for (int i = 0; i < 32; i++) {
        uint8_t ist = 0;
        if (i == 8)  ist = IST_DF;   /* double fault */
        else if (i == 14) ist = IST_PF;  /* page fault */
        else if (i == 13) ist = IST_GP;  /* general protection */
        idt_set_gate(i, (uint64_t)exception_stubs[i], GDT_SEL_KCODE, 0x8E, ist);
    }

    /* Publish the IST tops in the TSS so the CPU switches to them. */
    extern tss64_t tss_value;
    tss_value.ist1 = df_top;
    tss_value.ist2 = pf_top;
    tss_value.ist3 = gp_top;

    /*
     * Register IRQ handlers (vectors 32-47 after PIC remapping).
     * PIC is remapped to interrupt vectors 32-47 in pic.c.
     */
    extern void isr_32(void); extern void isr_33(void); extern void isr_34(void);
    extern void isr_35(void); extern void isr_36(void); extern void isr_37(void);
    extern void isr_38(void); extern void isr_39(void); extern void isr_40(void);
    extern void isr_41(void); extern void isr_42(void); extern void isr_43(void);
    extern void isr_44(void); extern void isr_45(void); extern void isr_46(void);
    extern void isr_47(void);

    void* irq_stubs[16] = {
        isr_32, isr_33, isr_34, isr_35, isr_36, isr_37, isr_38, isr_39,
        isr_40, isr_41, isr_42, isr_43, isr_44, isr_45, isr_46, isr_47,
    };

    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, (uint64_t)irq_stubs[i], GDT_SEL_KCODE, 0x8E, 0);
    }

    /*
     * Load the IDT register.
     */
    asm volatile("lidt %0" : : "m"(idt_register));

    kprint("IDT: 256 entries loaded\n");
}
