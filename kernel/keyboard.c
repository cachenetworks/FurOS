#include "keyboard.h"

#include <stdbool.h>
#include <stdint.h>

static bool shift_pressed = false;
static bool ctrl_pressed  = false;
static bool caps_lock     = false;
static int  g_ext         = 0; /* non-zero after 0xE0 extended prefix */

static const char scancode_set1_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_set1_shift_map[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

void keyboard_init(void) {
    shift_pressed = false;
    ctrl_pressed  = false;
    caps_lock     = false;
    g_ext         = 0;
}

int keyboard_get_key(void) {
    for (;;) {
        uint8_t status   = inb(0x64);
        uint8_t scancode;
        char c;
        bool shift_effective;

        if ((status & 0x01) == 0) {
            continue;
        }

        scancode = inb(0x60);

        /* Extended key prefix */
        if (scancode == 0xE0) {
            g_ext = 1;
            continue;
        }

        /* Key release */
        if (scancode & 0x80) {
            uint8_t released = (uint8_t)(scancode & 0x7Fu);
            if (released == 0x2A || released == 0x36) { shift_pressed = false; }
            else if (released == 0x1D) { ctrl_pressed = false; }
            g_ext = 0;
            continue;
        }

        /* Extended (cursor/function) keys */
        if (g_ext) {
            int ext_key = -1;
            g_ext = 0;
            switch (scancode) {
                case 0x48: ext_key = KEY_UP;    break;
                case 0x50: ext_key = KEY_DOWN;  break;
                case 0x4B: ext_key = KEY_LEFT;  break;
                case 0x4D: ext_key = KEY_RIGHT; break;
                default: break;
            }
            if (ext_key >= 0) {
                return ext_key;
            }
            continue;
        }

        /* Modifier keys */
        if (scancode == 0x2A || scancode == 0x36) { shift_pressed = true;  continue; }
        if (scancode == 0x1D)                      { ctrl_pressed  = true;  continue; }
        if (scancode == 0x3A)                      { caps_lock = !caps_lock; continue; }

        /* Function keys F1-F5 */
        switch (scancode) {
            case 0x3B: return KEY_F1;
            case 0x3C: return KEY_F2;
            case 0x3D: return KEY_F3;
            case 0x3E: return KEY_F4;
            case 0x3F: return KEY_F5;
            default: break;
        }

        c = scancode_set1_map[scancode];
        if (c == 0) {
            continue;
        }

        shift_effective = shift_pressed;
        if (is_alpha(c)) {
            if (shift_effective ^ caps_lock) {
                c = to_upper(c);
            } else {
                c = to_lower(c);
            }
        } else if (shift_effective && scancode_set1_shift_map[scancode] != 0) {
            c = scancode_set1_shift_map[scancode];
        }

        if (ctrl_pressed && is_alpha(c)) {
            c = (char)(to_lower(c) - 'a' + 1);
        }

        return (int)(unsigned char)c;
    }
}

char keyboard_getchar(void) {
    for (;;) {
        int k = keyboard_get_key();
        if (k >= 0 && k <= 127) {
            return (char)k;
        }
        /* Skip special keys (arrows, function keys) */
    }
}
