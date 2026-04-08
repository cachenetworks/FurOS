#ifndef FUR_VGA_H
#define FUR_VGA_H

#include <stdint.h>

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15
};

void vga_set_color(uint8_t fg, uint8_t bg);
void vga_clear(void);
void vga_putchar(char c);
void vga_backspace(void);
void vga_write_string(const char *str);

/* Direct-position writes with explicit fg/bg (do not move the cursor) */
void vga_put_at(int x, int y, unsigned char c, uint8_t fg, uint8_t bg);
void vga_write_str_at(int x, int y, const char *s, uint8_t fg, uint8_t bg);

/* Fill a rectangle with a character and color */
void vga_fill_rect(int x, int y, int w, int h,
                   unsigned char c, uint8_t fg, uint8_t bg);

/* Scrolling cursor position */
void vga_set_cursor_pos(int x, int y);
int  vga_get_row(void);
int  vga_get_col(void);

/* Viewport: vga_clear() and scrolling are confined to [top..bottom] rows.
 * Rows outside the viewport are never touched by normal text output. */
void vga_set_viewport(int top, int bottom);
void vga_reset_viewport(void);

#endif
