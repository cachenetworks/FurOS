#include "vga.h"

#include <stddef.h>
#include <stdint.h>

static volatile uint16_t *const VGA_MEMORY = (volatile uint16_t *)0xB8000;
static const int VGA_WIDTH  = 80;
static const int VGA_HEIGHT = 25;

static int terminal_row    = 0;
static int terminal_column = 0;
static uint8_t terminal_color = 0x07;

/* Viewport: scrolling and vga_clear() are limited to [vga_vp_top..vga_vp_bot] */
static int vga_vp_top = 0;
static int vga_vp_bot = 24; /* inclusive */

static uint8_t vga_entry_color(uint8_t fg, uint8_t bg) {
    return (uint8_t)(fg | (uint8_t)(bg << 4));
}

static uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | (uint16_t)((uint16_t)color << 8);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    terminal_color = vga_entry_color(fg, bg);
}

static void vga_scroll(void) {
    int y, x;

    for (y = vga_vp_top + 1; y <= vga_vp_bot; y++) {
        for (x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[vga_vp_bot * VGA_WIDTH + x] =
            vga_entry(' ', terminal_color);
    }
}

void vga_clear(void) {
    int y, x;

    for (y = vga_vp_top; y <= vga_vp_bot; y++) {
        for (x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }

    terminal_row    = vga_vp_top;
    terminal_column = 0;
}

static void vga_newline(void) {
    terminal_column = 0;
    terminal_row++;

    if (terminal_row > vga_vp_bot) {
        vga_scroll();
        terminal_row = vga_vp_bot;
    }
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_newline();
        return;
    }

    if (c == '\r') {
        terminal_column = 0;
        return;
    }

    VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
        vga_entry((unsigned char)c, terminal_color);
    terminal_column++;

    if (terminal_column >= VGA_WIDTH) {
        vga_newline();
    }
}

void vga_backspace(void) {
    if (terminal_row == vga_vp_top && terminal_column == 0) {
        return;
    }

    if (terminal_column == 0) {
        terminal_row--;
        terminal_column = VGA_WIDTH - 1;
    } else {
        terminal_column--;
    }

    VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
        vga_entry(' ', terminal_color);
}

void vga_write_string(const char *str) {
    if (str == 0) {
        return;
    }

    while (*str != '\0') {
        vga_putchar(*str);
        str++;
    }
}

/* ── Direct-position helpers ─────────────────────────────────────────────── */

void vga_put_at(int x, int y, unsigned char c, uint8_t fg, uint8_t bg) {
    if (x < 0 || y < 0 || x >= VGA_WIDTH || y >= VGA_HEIGHT) {
        return;
    }
    VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(c, vga_entry_color(fg, bg));
}

void vga_write_str_at(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    uint8_t color = vga_entry_color(fg, bg);
    if (s == 0) {
        return;
    }
    while (*s != '\0' && x < VGA_WIDTH) {
        if (x >= 0 && y >= 0 && y < VGA_HEIGHT) {
            VGA_MEMORY[y * VGA_WIDTH + x] =
                vga_entry((unsigned char)*s, color);
        }
        x++;
        s++;
    }
}

void vga_fill_rect(int x, int y, int w, int h,
                   unsigned char c, uint8_t fg, uint8_t bg) {
    uint8_t color = vga_entry_color(fg, bg);
    uint16_t cell = vga_entry(c, color);
    int row, col;

    for (row = y; row < y + h; row++) {
        if (row < 0 || row >= VGA_HEIGHT) {
            continue;
        }
        for (col = x; col < x + w; col++) {
            if (col < 0 || col >= VGA_WIDTH) {
                continue;
            }
            VGA_MEMORY[row * VGA_WIDTH + col] = cell;
        }
    }
}

/* ── Cursor helpers ──────────────────────────────────────────────────────── */

void vga_set_cursor_pos(int x, int y) {
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    if (x >= VGA_WIDTH)  { x = VGA_WIDTH  - 1; }
    if (y >= VGA_HEIGHT) { y = VGA_HEIGHT - 1; }
    terminal_column = x;
    terminal_row    = y;
}

int vga_get_row(void) { return terminal_row; }
int vga_get_col(void) { return terminal_column; }

/* ── Viewport ────────────────────────────────────────────────────────────── */

void vga_set_viewport(int top, int bottom) {
    if (top    <  0)          { top    = 0; }
    if (bottom >= VGA_HEIGHT) { bottom = VGA_HEIGHT - 1; }
    if (top    >  bottom)     { top    = bottom; }
    vga_vp_top = top;
    vga_vp_bot = bottom;
    if (terminal_row < vga_vp_top) { terminal_row = vga_vp_top; }
    if (terminal_row > vga_vp_bot) { terminal_row = vga_vp_bot; }
}

void vga_reset_viewport(void) {
    vga_vp_top = 0;
    vga_vp_bot = VGA_HEIGHT - 1;
}
