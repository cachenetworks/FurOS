/*
 * FurOS Desktop — KDE Plasma-style text-mode desktop (80×25 VGA)
 *
 * Layout
 *   Rows 0-22  Desktop area (wallpaper + icons) or terminal content
 *   Row  23    Window title bar (desktop mode) / terminal title (term mode)
 *   Row  24    Taskbar / panel (always visible)
 *
 * Colors follow the KDE Breeze Dark palette adapted to VGA 16 colors:
 *   Desktop bg   : BLUE (dark, like Breeze Dark desktop)
 *   Panel bg     : DARK_GREY
 *   Accent       : LIGHT_MAGENTA (plasma purple)
 *   Active btn   : MAGENTA
 *   Window title : MAGENTA bg / WHITE fg
 *   Text         : WHITE / LIGHT_GREY
 */
#include "desktop.h"

#include "keyboard.h"
#include "kstring.h"
#include "shell.h"
#include "vga.h"

#include <stddef.h>
#include <stdint.h>

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define DT_W         80
#define DT_DESK_TOP   0
#define DT_DESK_BOT  22   /* inclusive, desktop/terminal content rows */
#define DT_TITLE_ROW 23
#define DT_PANEL_ROW 24

/* ── Colors ──────────────────────────────────────────────────────────────── */
#define C_DESK_FG   VGA_COLOR_LIGHT_GREY
#define C_DESK_BG   VGA_COLOR_BLUE
#define C_LOGO_FG   VGA_COLOR_LIGHT_CYAN
#define C_LOGO_BG   VGA_COLOR_BLUE
#define C_SUB_FG    VGA_COLOR_WHITE
#define C_PANEL_FG  VGA_COLOR_WHITE
#define C_PANEL_BG  VGA_COLOR_DARK_GREY
#define C_APPL_FG   VGA_COLOR_WHITE
#define C_APPL_BG   VGA_COLOR_MAGENTA          /* launcher button */
#define C_BTN_FG    VGA_COLOR_LIGHT_GREY
#define C_BTN_BG    VGA_COLOR_DARK_GREY
#define C_ABTN_FG   VGA_COLOR_WHITE
#define C_ABTN_BG   VGA_COLOR_BLUE             /* active app button */
#define C_SYS_FG    VGA_COLOR_LIGHT_CYAN
#define C_SYS_BG    VGA_COLOR_DARK_GREY
#define C_PWR_FG    VGA_COLOR_WHITE
#define C_PWR_BG    VGA_COLOR_DARK_GREY
#define C_WIN_FG    VGA_COLOR_WHITE
#define C_WIN_BG    VGA_COLOR_MAGENTA          /* window title bar */
#define C_ICON_FG   VGA_COLOR_WHITE
#define C_ICON_BG   VGA_COLOR_DARK_GREY
#define C_ISEL_FG   VGA_COLOR_WHITE
#define C_ISEL_BG   VGA_COLOR_LIGHT_MAGENTA   /* selected icon */
#define C_TIP_FG    VGA_COLOR_LIGHT_GREY
#define C_TIP_BG    VGA_COLOR_BLUE

/* CP437 chars used for drawing */
#define CP_BLK  '\xDB'   /* █ full block */
#define CP_TRI  '\x10'   /* ► right-pointing triangle */
#define CP_DIA  '\x04'   /* ♦ diamond */
#define CP_SQR  '\xFE'   /* ■ small square */
#define CP_H    '\xC4'   /* ─ */
#define CP_V    '\xB3'   /* │ */
#define CP_TL   '\xDA'   /* ┌ */
#define CP_TR   '\xBF'   /* ┐ */
#define CP_BL   '\xC0'   /* └ */
#define CP_BR   '\xD9'   /* ┘ */

/* ── App icon definitions ────────────────────────────────────────────────── */
#define ICON_COUNT 3
static const char *icon_labels[ICON_COUNT] = {
    "  Terminal  ",
    "   Files    ",
    " Settings   "
};
/* Icon box: 14 wide × 3 tall, placed at left side */
#define ICON_X     3
#define ICON_W    14
#define ICON_ROW0  4   /* first icon top */
#define ICON_STEP  5   /* row gap between icons */

/* ── Internal helpers ────────────────────────────────────────────────────── */

static int dt_slen(const char *s) {
    int n = 0; while (s[n]) { n++; } return n;
}

/* Pad/truncate s to exactly w chars into buf (null-terminated) */
static void dt_pad(char *buf, const char *s, int w) {
    int i, len = dt_slen(s);
    for (i = 0; i < w; i++) {
        buf[i] = (i < len) ? s[i] : ' ';
    }
    buf[w] = '\0';
}

static void dt_write_centered(int y, const char *s, uint8_t fg, uint8_t bg) {
    int len = dt_slen(s);
    int x = (DT_W - len) / 2;
    if (x < 0) { x = 0; }
    vga_write_str_at(x, y, s, fg, bg);
}

/* ── Desktop drawing ─────────────────────────────────────────────────────── */

static void dt_draw_background(void) {
    /* Fill desktop area with blue */
    vga_fill_rect(0, DT_DESK_TOP, DT_W, DT_DESK_BOT - DT_DESK_TOP + 1,
                  ' ', C_DESK_FG, C_DESK_BG);
}

static void dt_draw_logo(void) {
    /* FurOS ASCII art centred horizontally at rows 2-7 */
    dt_write_centered(2, "  ______           ____  ____",  C_LOGO_FG, C_LOGO_BG);
    dt_write_centered(3, " |  ____|         / __ \\/ ___|", C_LOGO_FG, C_LOGO_BG);
    dt_write_centered(4, " | |__ _   _ _ __| |  | \\___ \\",C_LOGO_FG, C_LOGO_BG);
    dt_write_centered(5, " |  __| | | | '__| |  | |___) |",C_LOGO_FG, C_LOGO_BG);
    dt_write_centered(6, " | |  | |_| | |  | |__| |____/", C_LOGO_FG, C_LOGO_BG);
    dt_write_centered(7, " |_|   \\__,_|_|   \\____/",       C_LOGO_FG, C_LOGO_BG);
    dt_write_centered(9, "FurOS 1.0  \xb3  Plasma Edition",    C_SUB_FG,  C_LOGO_BG);
    dt_write_centered(10,"Based on Debian GNU/Linux 12 Bookworm", C_TIP_FG, C_LOGO_BG);
}

/* Draw a single icon box at (x, y_top) with given label, selected or not */
static void dt_draw_icon(int idx, int sel) {
    int y = ICON_ROW0 + idx * ICON_STEP;
    uint8_t fg = sel ? C_ISEL_FG : C_ICON_FG;
    uint8_t bg = sel ? C_ISEL_BG : C_ICON_BG;
    char lbuf[ICON_W + 1];
    int i;

    /* Box borders */
    vga_put_at(ICON_X, y,   CP_TL, fg, bg);
    vga_put_at(ICON_X, y+1, CP_V,  fg, bg);
    vga_put_at(ICON_X, y+2, CP_BL, fg, bg);
    for (i = 1; i < ICON_W - 1; i++) {
        vga_put_at(ICON_X+i, y,   CP_H, fg, bg);
        vga_put_at(ICON_X+i, y+2, CP_H, fg, bg);
    }
    vga_put_at(ICON_X+ICON_W-1, y,   CP_TR, fg, bg);
    vga_put_at(ICON_X+ICON_W-1, y+1, CP_V,  fg, bg);
    vga_put_at(ICON_X+ICON_W-1, y+2, CP_BR, fg, bg);

    /* Label row (inner width = ICON_W - 2) */
    dt_pad(lbuf, icon_labels[idx], ICON_W - 2);
    vga_write_str_at(ICON_X+1, y+1, lbuf, fg, bg);
}

static void dt_draw_icons(int sel) {
    int i;
    for (i = 0; i < ICON_COUNT; i++) {
        dt_draw_icon(i, i == sel);
    }
}

static void dt_draw_tips(void) {
    dt_write_centered(20,
        "\x18\x19: Select app   ENTER: Launch   F5: Refresh",
        C_TIP_FG, C_DESK_BG);
}

static void dt_draw_title_bar(const char *title, int is_terminal) {
    vga_fill_rect(0, DT_TITLE_ROW, DT_W, 1, ' ', C_WIN_FG, C_WIN_BG);
    if (title) {
        char buf[DT_W + 1];
        dt_pad(buf, title, DT_W);
        vga_write_str_at(0, DT_TITLE_ROW, buf, C_WIN_FG, C_WIN_BG);
    }
    if (is_terminal) {
        vga_write_str_at(DT_W-10, DT_TITLE_ROW,
            "[logout]", C_WIN_FG, C_WIN_BG);
    }
    (void)is_terminal;
}

static void dt_draw_panel(int in_terminal) {
    int x;

    vga_fill_rect(0, DT_PANEL_ROW, DT_W, 1, ' ', C_PANEL_FG, C_PANEL_BG);

    /* App launcher button */
    vga_write_str_at(0, DT_PANEL_ROW, " \x10 FurOS ", C_APPL_FG, C_APPL_BG);
    x = 8;

    /* Separator */
    vga_put_at(x, DT_PANEL_ROW, CP_V, C_PANEL_FG, C_PANEL_BG);
    x++;

    /* App buttons */
    {
        uint8_t fg = in_terminal ? C_ABTN_FG : C_BTN_FG;
        uint8_t bg = in_terminal ? C_ABTN_BG : C_BTN_BG;
        vga_write_str_at(x, DT_PANEL_ROW, " Terminal ", fg, bg);
        x += 10;
    }

    /* Separator */
    vga_put_at(x, DT_PANEL_ROW, CP_V, C_PANEL_FG, C_PANEL_BG);

    /* System tray (right side) */
    vga_write_str_at(55, DT_PANEL_ROW, " debian@furos ", C_SYS_FG, C_SYS_BG);
    vga_put_at(69, DT_PANEL_ROW, CP_V, C_PANEL_FG, C_PANEL_BG);
    vga_write_str_at(70, DT_PANEL_ROW, " FurOS 1.0 ", C_PANEL_FG, C_PANEL_BG);
    /* Power icon on far right */
    vga_put_at(DT_W-4, DT_PANEL_ROW, CP_V, C_PANEL_FG, C_PANEL_BG);
    vga_write_str_at(DT_W-3, DT_PANEL_ROW, " \xFE ", C_PWR_FG, C_PANEL_BG);
}

static void dt_draw_desktop(int icon_sel) {
    dt_draw_background();
    dt_draw_logo();
    dt_draw_icons(icon_sel);
    dt_draw_tips();
    dt_draw_title_bar(" FurOS 1.0 Plasma Edition", 0);
    dt_draw_panel(0);
}

/* ── Launch terminal ─────────────────────────────────────────────────────── */

static void dt_launch_terminal(void) {
    /* Set up the viewport so the shell writes into rows 0-22.
     * Row 23 (title bar) and row 24 (panel) are drawn by us and never
     * touched by the shell because vga_scroll() and vga_clear() are
     * constrained to the viewport. */
    vga_set_viewport(DT_DESK_TOP, DT_DESK_BOT);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear(); /* clear only the viewport */

    /* Draw chrome outside the viewport manually */
    dt_draw_title_bar(" Terminal \xb3 FurOS 1.0 Plasma Edition"
                      "                [type 'logout' to close]", 1);
    dt_draw_panel(1);

    /* Run the shell — it returns when the user types 'logout' */
    shell_run();

    /* Restore full viewport before redrawing desktop */
    vga_reset_viewport();
}

/* ── Desktop event loop ──────────────────────────────────────────────────── */

__attribute__((noreturn))
void desktop_run(void) {
    int sel = 0;  /* currently highlighted icon */

    for (;;) {
        dt_draw_desktop(sel);
        vga_set_cursor_pos(ICON_X + 1, ICON_ROW0 + sel * ICON_STEP + 1);

        for (;;) {
            int k = keyboard_get_key();

            if (k == KEY_UP) {
                if (sel > 0) { sel--; }
                dt_draw_icons(sel);
                dt_draw_tips();
                vga_set_cursor_pos(ICON_X + 1, ICON_ROW0 + sel * ICON_STEP + 1);
                continue;
            }

            if (k == KEY_DOWN) {
                if (sel < ICON_COUNT - 1) { sel++; }
                dt_draw_icons(sel);
                dt_draw_tips();
                vga_set_cursor_pos(ICON_X + 1, ICON_ROW0 + sel * ICON_STEP + 1);
                continue;
            }

            if (k == '\n' || k == '\r' || k == ' ') {
                /* Launch selected app */
                if (sel == 0) { /* Terminal */
                    dt_launch_terminal();
                    break; /* break inner loop → redraw desktop */
                }
                /* Files and Settings are not yet implemented */
                dt_write_centered(19,
                    "This app is not yet available in this release.",
                    VGA_COLOR_LIGHT_RED, C_DESK_BG);
                continue;
            }

            if (k == KEY_F5) {
                break; /* force desktop redraw */
            }

            /* Typing 't' or 'T' is a shortcut for Terminal */
            if (k == 't' || k == 'T') {
                sel = 0;
                dt_launch_terminal();
                break;
            }
        }
        /* After inner loop breaks, outer loop redraws the desktop */
    }
}
