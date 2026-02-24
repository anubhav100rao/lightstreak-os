#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

/*
 * kernel/drivers/keyboard.h — PS/2 keyboard driver
 *
 * IRQ1 handler reads scancodes from port 0x60, converts to ASCII,
 * and places them into a circular ring buffer. keyboard_getchar()
 * blocks (spin-waits) until a character is available.
 */

#include "../../include/types.h"

/* Initialise the keyboard driver (registers IRQ1) */
void keyboard_init(void);

/* Get next character from the input buffer.
 * Blocks (busy-waits with HLT) if the buffer is empty. */
char keyboard_getchar(void);

/* Non-blocking: returns 1 if there's data in the input buffer */
int keyboard_has_data(void);

#endif /* DRIVERS_KEYBOARD_H */
