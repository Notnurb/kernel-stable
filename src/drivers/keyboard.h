/*
 * keyboard.h - PS/2 Keyboard driver
 *
 * Reads scan codes from IRQ1, translates to ASCII using scan code set 1.
 * Provides a simple ring buffer for character input.
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize the keyboard driver (registers IRQ1 handler) */
void kinit_keyboard(void);

/* Get the next character from the input buffer. Returns 0 if empty. */
char keyboard_getchar(void);

/* Check if a character is available */
bool keyboard_has_key(void);

/* Modifier key state */
bool keyboard_lshift(void);
bool keyboard_rshift(void);
bool keyboard_ctrl(void);

#endif /* KEYBOARD_H */
