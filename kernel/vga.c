#include "vga.h"

#include <stddef.h>
#include <stdint.h>

static volatile uint16_t *const VGA_MEMORY = (volatile uint16_t *)0xB8000;
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

static size_t terminal_row = 0;
static size_t terminal_column = 0;
static uint8_t terminal_color = 0x07;

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
    size_t y;
    size_t x;

    for (y = 1; y < VGA_HEIGHT; y++) {
        for (x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
}

void vga_clear(void) {
    size_t y;
    size_t x;

    for (y = 0; y < VGA_HEIGHT; y++) {
        for (x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }

    terminal_row = 0;
    terminal_column = 0;
}

static void vga_newline(void) {
    terminal_column = 0;
    terminal_row++;

    if (terminal_row >= VGA_HEIGHT) {
        vga_scroll();
        terminal_row = VGA_HEIGHT - 1;
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

    VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] = vga_entry((unsigned char)c, terminal_color);
    terminal_column++;

    if (terminal_column >= VGA_WIDTH) {
        vga_newline();
    }
}

void vga_backspace(void) {
    if (terminal_row == 0 && terminal_column == 0) {
        return;
    }

    if (terminal_column == 0) {
        terminal_row--;
        terminal_column = VGA_WIDTH - 1;
    } else {
        terminal_column--;
    }

    VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] = vga_entry(' ', terminal_color);
}

void vga_write_string(const char *str) {
    if (str == NULL) {
        return;
    }

    while (*str != '\0') {
        vga_putchar(*str);
        str++;
    }
}
