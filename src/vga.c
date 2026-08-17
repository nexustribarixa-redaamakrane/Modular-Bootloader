/*
 * vga.c - 80x25 text-mode VGA output (black & white, GRUB style).
 * Attribute 0x07 = white-on-black text; 0x70 = reverse video highlight.
 */

#include "mbl.h"

#define VGA_BASE   ((volatile uint16_t *)0x000B8000u)
#define VGA_ROWS   25u
#define VGA_COLS   80u

#define ATTR_NORMAL 0x07u
#define ATTR_HILITE 0x70u
#define ATTR_BRIGHT 0x0Fu
#define ATTR_DIM    0x07u

static uint8_t g_row;
static uint8_t g_col;

void vga_clear(void) {
    volatile uint16_t *p = VGA_BASE;
    int i;
    for (i = 0; i < (int)(VGA_ROWS * VGA_COLS); i++) {
        p[i] = (uint16_t)(0x0720u);
    }
    g_row = 0;
    g_col = 0;
}

void vga_goto(uint8_t row, uint8_t col) {
    if (row >= VGA_ROWS) row = VGA_ROWS - 1;
    if (col >= VGA_COLS) col = VGA_COLS - 1;
    g_row = row;
    g_col = col;
}

static void vga_advance(void) {
    g_col++;
    if (g_col >= VGA_COLS) {
        g_col = 0;
        if (g_row + 1 < VGA_ROWS) {
            g_row++;
        }
    }
}

void vga_putc(char c) {
    volatile uint16_t *p = VGA_BASE + (uint16_t)g_row * VGA_COLS + g_col;
    if (c == '\n') {
        g_col = 0;
        if (g_row + 1 < VGA_ROWS) g_row++;
        return;
    }
    *p = (uint16_t)((uint16_t)ATTR_NORMAL << 8) | (uint8_t)c;
    vga_advance();
}

void vga_puts(const char *s) {
    while (*s) {
        vga_putc(*s);
        s++;
    }
}

void vga_write(uint8_t row, uint8_t col, const char *s, uint8_t attr) {
    volatile uint16_t *p;
    if (row >= VGA_ROWS || col >= VGA_COLS) return;
    p = VGA_BASE + (uint16_t)row * VGA_COLS + col;
    while (*s && col < VGA_COLS) {
        *p = (uint16_t)((uint16_t)attr << 8) | (uint8_t)*s;
        p++;
        s++;
        col++;
    }
}

void vga_fill(uint8_t row, uint8_t col, uint8_t ch, uint8_t n, uint8_t attr) {
    volatile uint16_t *p;
    uint8_t i;
    if (row >= VGA_ROWS || col >= VGA_COLS) return;
    p = VGA_BASE + (uint16_t)row * VGA_COLS + col;
    for (i = 0; i < n; i++) {
        *p = (uint16_t)((uint16_t)attr << 8) | ch;
        p++;
    }
}
