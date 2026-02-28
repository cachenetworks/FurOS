#include "serial.h"

#include <stdint.h>

#define COM1_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_wait_tx_ready(void) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0) {
    }
}

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void serial_write_char(char c) {
    if (c == '\n') {
        serial_write_char('\r');
    }
    serial_wait_tx_ready();
    outb(COM1_PORT, (uint8_t)c);
}

void serial_write_string(const char *str) {
    if (str == 0) {
        return;
    }
    while (*str != '\0') {
        serial_write_char(*str++);
    }
}
