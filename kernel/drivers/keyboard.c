/*
 * kernel/drivers/keyboard.c — PS/2 keyboard driver
 *
 * Handles IRQ1 from the 8042 keyboard controller.
 * Reads scan codes from port 0x60, translates to ASCII via a scancode
 * table (US QWERTY layout, scan code set 1), and stores characters in
 * a circular ring buffer.
 *
 * Supports: letters, numbers, space, enter, backspace.
 * Does NOT support: modifier combinations (ctrl/alt), function keys, etc.
 */

#include "keyboard.h"
#include "../arch/io.h"
#include "../arch/irq.h"
#include "../kernel.h"

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

#define KBD_BUF_SIZE 256

/* Circular ring buffer */
static char kbd_buffer[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0; /* Write pointer */
static volatile uint32_t kbd_tail = 0; /* Read pointer */
static volatile uint8_t shift_held = 0;

/* US QWERTY scancode set 1 → ASCII (index = scancode, value = ASCII) */
static const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
    /* F1..F10, Num Lock, Scroll Lock, Home, Up, PgUp, '-', Left, 5, Right, '+'
       ... */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0};

static const char scancode_to_ascii_shift[128] = {
    0,   27,   '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0};

/* -------------------------------------------------------------------------
 * IRQ1 handler
 * ---------------------------------------------------------------------- */
static void keyboard_irq_handler(registers_t *regs) {
  (void)regs;
  uint8_t scancode = inb(KBD_DATA_PORT);

  /* Handle shift key press/release */
  if (scancode == 0x2A || scancode == 0x36) { /* Left/Right Shift press */
    shift_held = 1;
    return;
  }
  if (scancode == 0xAA || scancode == 0xB6) { /* Left/Right Shift release */
    shift_held = 0;
    return;
  }

  /* Ignore key releases (bit 7 set) */
  if (scancode & 0x80)
    return;

  /* Convert scancode to ASCII */
  char c;
  if (shift_held)
    c = scancode_to_ascii_shift[scancode];
  else
    c = scancode_to_ascii[scancode];

  if (c == 0)
    return; /* Unmapped key */

  /* Push into ring buffer (drop if full) */
  uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
  if (next != kbd_tail) {
    kbd_buffer[kbd_head] = c;
    kbd_head = next;
  }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void keyboard_init(void) {
  kbd_head = 0;
  kbd_tail = 0;
  shift_held = 0;
  irq_register(IRQ_KEYBOARD, keyboard_irq_handler);
  kprintf("[KBD] PS/2 keyboard driver initialised\n");
}

char keyboard_getchar(void) {
  /* Busy-wait with interrupts enabled until a character arrives */
  while (kbd_tail == kbd_head) {
    __asm__ volatile("sti; hlt");
  }
  char c = kbd_buffer[kbd_tail];
  kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
  return c;
}

int keyboard_has_data(void) { return kbd_tail != kbd_head; }
