/*
 * kernel/drivers/vga.c — VGA text mode driver (80x25)
 *
 * The VGA text buffer lives at physical address 0xB8000.
 * Each cell is 2 bytes: low byte = ASCII character, high byte = color attribute.
 *
 * Color attribute format (8 bits):
 *   bits [3:0] = foreground color (one of 16 colors)
 *   bits [6:4] = background color (one of 8 colors)
 *   bit  [7]   = blink / bright background (we leave it 0)
 *
 * The hardware cursor position is updated via I/O ports 0x3D4/0x3D5.
 */

#include "vga.h"
#include "../arch/io.h"

/* VGA I/O ports for cursor control */
#define VGA_CTRL_PORT  0x3D4
#define VGA_DATA_PORT  0x3D5
#define VGA_CURSOR_HI  14
#define VGA_CURSOR_LO  15

/* Driver state */
static uint16_t *vga_buf  = (uint16_t *)VGA_ADDRESS;
static uint8_t   vga_col  = 0;
static uint8_t   vga_row  = 0;
static uint8_t   vga_color;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_COLS + vga_col);
    outb(VGA_CTRL_PORT, VGA_CURSOR_HI);
    outb(VGA_DATA_PORT, (uint8_t)(pos >> 8));
    outb(VGA_CTRL_PORT, VGA_CURSOR_LO);
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
}

/* Scroll the screen up by one row */
static void vga_scroll(void) {
    /* Move every row up by one */
    for (int row = 1; row < VGA_ROWS; row++) {
        for (int col = 0; col < VGA_COLS; col++) {
            vga_buf[(row - 1) * VGA_COLS + col] = vga_buf[row * VGA_COLS + col];
        }
    }
    /* Blank the last row */
    for (int col = 0; col < VGA_COLS; col++) {
        vga_buf[(VGA_ROWS - 1) * VGA_COLS + col] = vga_entry(' ', vga_color);
    }
    vga_row = VGA_ROWS - 1;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void vga_init(void) {
    vga_color = vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
}

void vga_set_color(uint8_t color) {
    vga_color = color;
}

uint8_t vga_get_default_color(void) {
    return vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void vga_clear(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++) {
        vga_buf[i] = vga_entry(' ', vga_color);
    }
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        /* Advance to next 8-column tab stop */
        vga_col = (uint8_t)((vga_col + 8) & ~7);
        if (vga_col >= VGA_COLS) {
            vga_col = 0;
            vga_row++;
        }
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga_buf[vga_row * VGA_COLS + vga_col] = vga_entry(' ', vga_color);
        }
    } else {
        vga_buf[vga_row * VGA_COLS + vga_col] = vga_entry(c, vga_color);
        vga_col++;
        if (vga_col >= VGA_COLS) {
            vga_col = 0;
            vga_row++;
        }
    }

    if (vga_row >= VGA_ROWS) {
        vga_scroll();
    }

    vga_update_cursor();
}

void vga_puts(const char *str) {
    while (*str) {
        vga_putchar(*str++);
    }
}
