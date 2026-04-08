#ifndef FUR_KEYBOARD_H
#define FUR_KEYBOARD_H

/* Special key codes returned by keyboard_get_key() (above 127) */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_F1    0x201
#define KEY_F2    0x202
#define KEY_F3    0x203
#define KEY_F4    0x204
#define KEY_F5    0x205

void keyboard_init(void);

/* Returns an int: 0-127 for ASCII chars, KEY_* for special keys */
int  keyboard_get_key(void);

/* Legacy: blocks until an ASCII char arrives (skips special keys) */
char keyboard_getchar(void);

#endif
