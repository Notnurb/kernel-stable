/*
 * pic.c - Programmable Interrupt Controller (PIC) setup
 *
 * Remaps the 8259A PIC so hardware IRQs map to interrupt vectors 32-47,
 * avoiding conflicts with CPU exception vectors 0-31.
 */
#include "pic.h"
#include "../kernel/prism.h"

/*
 * kinit_pic - Remap the PIC to interrupt vectors 32-47.
 *
 * The 8259A PIC has two chips: master (IRQs 0-7) and slave (IRQs 8-15).
 * By default they use vectors 0-15, conflicting with CPU exceptions.
 *
 * Remapping shifts the master to vectors 32-39 and slave to 40-47.
 */
void kinit_pic(void) {
    uint8_t master_offset = PIC_MASTER_OFFSET;
    uint8_t slave_offset  = PIC_SLAVE_OFFSET;

    /* ICW1: Initialization command (bit 0=IC4 needed, bit 4=level triggered edge) */
    outb(PIC_MASTER_CMD, 0x11);
    outb(PIC_SLAVE_CMD,  0x11);

    /* ICW2: Interrupt vector offset */
    outb(PIC_MASTER_DATA, master_offset);
    outb(PIC_SLAVE_DATA,  slave_offset);

    /* ICW3: Master IR2 has slave, slave is cascade ID 2 */
    outb(PIC_MASTER_DATA, 0x04);
    outb(PIC_SLAVE_DATA,  0x02);

    /* ICW4: 8086/88 mode, normal EOI */
    outb(PIC_MASTER_DATA, 0x01);
    outb(PIC_SLAVE_DATA,  0x01);

    /* Restore masks (0x00 = all IRQs enabled for now; handlers will manage) */
    outb(PIC_MASTER_DATA, 0x00);  /* Enable all master IRQs temporarily */
    outb(PIC_SLAVE_DATA,  0x00);  /* Enable all slave IRQs temporarily */

    kprint("PIC: Master=0x20, Slave=0x28, remapped\n");
}

/*
 * pic_send_eoi - Send End-of-Interrupt signal to PIC(s).
 * Must be called at the end of each hardware ISR.
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC_SLAVE_CMD, 0x20);
    }
    outb(PIC_MASTER_CMD, 0x20);
}
