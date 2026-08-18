/*
 * keyboard.c - PS/2 Keyboard driver (IRQ1, Scan Code Set 1)
 *
 * PS/2 keyboard sends scan codes via IRQ 1 (vector 33 after PIC remapping).
 * Scan code set 1: bit 7 clear = make (press), set = break (release).
 *
 * Registers a handler via the ISR dispatch table.
 * Translates make codes to ASCII using lookup tables.
 * Buffers characters in a ring buffer for polling.
 */
#include "keyboard.h"
#include "../isr/isr.h"
#include "../cpu/pic.h"
#include "../drivers/serial.h"
#include "../kernel/prism.h"

#define KBD_DATA_PORT   0x60
#define KBD_BUFFER_SIZE 256

/* Ring buffer for decoded characters */
static char     kbd_buffer[KBD_BUFFER_SIZE];
static uint8_t  kbd_head = 0;
static uint8_t  kbd_tail = 0;

/* Modifier key state */
static volatile bool kbd_lshift = false;
static volatile bool kbd_rshift = false;
static volatile bool kbd_ctrl   = false;
static volatile bool kbd_alt    = false;

/*
 * Scan code set 1 → ASCII lookup tables.
 * Index = scan code (0-127), value = ASCII char (0 = no mapping).
 */
static const char scancode_ascii[128] = {
    0,  0, '1', '2', '3', '4', '5', '6',       /* 0x00-0x07 */
  '7', '8', '9', '0', '-', '=',  0,  0,         /* 0x08-0x0F: 0x0E=backspace, 0x0F=tab */
  'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',      /* 0x10-0x17 */
  'o', 'p', '[', ']',  0,  0, 'a', 's',         /* 0x18-0x1F: 0x1C=enter, 0x1D=lctrl */
  'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',      /* 0x20-0x27 */
  '\'', '`',  0, '\\', 'z', 'x', 'c', 'v',     /* 0x28-0x2F: 0x2A=lshift */
  'b', 'n', 'm', ',', '.', '/',  0, '*',        /* 0x30-0x37: 0x36=rshift, 0x37=kp* */
  0, ' ',  0,  0,  0,  0,  0,  0,               /* 0x38-0x3F: 0x38=lalt, 0x39=space, 0x3A=caps */
};

static const char scancode_shift[128] = {
    0,  0, '!', '@', '#', '$', '%', '^',         /* 0x00-0x07 */
  '&', '*', '(', ')', '_', '+',  0,  0,          /* 0x08-0x0F */
  'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',      /* 0x10-0x17 */
  'O', 'P', '{', '}',  0,  0, 'A', 'S',         /* 0x18-0x1F */
  'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',      /* 0x20-0x27 */
  '"', '~',  0, '|', 'Z', 'X', 'C', 'V',       /* 0x28-0x2F */
  'B', 'N', 'M', '<', '>', '?',  0, '*',        /* 0x30-0x37 */
  0, ' ',  0,  0,  0,  0,  0,  0,                /* 0x38-0x3F */
};

static inline void kbd_buffer_push(char c) {
    uint8_t next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {        /* drop if full */
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

/*
 * keyboard_irq_handler - Called from ISR dispatch on IRQ1.
 * Reads the scan code from the data port and processes it.
 */
static void keyboard_irq_handler(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);

    /* Ignore scan codes >= 0x80 (break codes) for now,
     * except to update modifier state */
    if (scancode & 0x80) {
        uint8_t break_code = scancode & 0x7F;
        switch (break_code) {
            case 0x2A: kbd_lshift = false; break;  /* left shift release */
            case 0x36: kbd_rshift = false; break;  /* right shift release */
            case 0x1D: kbd_ctrl   = false; break;  /* left ctrl release */
            case 0x38: kbd_alt    = false; break;  /* left alt release */
        }
        return;
    }

    /* Update modifier state on press */
    switch (scancode) {
        case 0x2A: kbd_lshift = true;  return;  /* left shift press */
        case 0x36: kbd_rshift = true;  return;  /* right shift press */
        case 0x1D: kbd_ctrl   = true;  return;  /* left ctrl press */
        case 0x38: kbd_alt    = true;  return;  /* left alt press */
    }

    /* Translate scan code to ASCII */
    bool shift = kbd_lshift || kbd_rshift;
    char c = shift ? scancode_shift[scancode] : scancode_ascii[scancode];

    /* Handle special keys that aren't in the ASCII table */
    if (scancode == 0x0E) c = '\b';              /* backspace */
    if (scancode == 0x1C) c = '\n';              /* enter */
    if (scancode == 0x0F) c = '\t';              /* tab */

    if (c) {
        kbd_buffer_push(c);
    }
}

void kinit_keyboard(void) {
    isr_register_irq(1, keyboard_irq_handler);
    kprint("Keyboard: PS/2 driver installed (IRQ1)\n");
}

char keyboard_getchar(void) {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

bool keyboard_has_key(void) {
    return kbd_head != kbd_tail;
}

bool keyboard_lshift(void) { return kbd_lshift; }
bool keyboard_rshift(void) { return kbd_rshift; }
bool keyboard_ctrl(void)   { return kbd_ctrl; }
