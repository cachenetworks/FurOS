/*
 * FurOS Desktop — Full KDE Plasma Edition (80×25 VGA text mode)
 *
 * Layout (app open):
 *   Row  0     Window title bar  (MAGENTA bg, Plasma purple)
 *   Rows 1-22  Application content viewport
 *   Row  23    Window status bar (DARK_GREY bg)
 *   Row  24    Always-visible taskbar/panel
 *
 * Layout (desktop):
 *   Rows 0-22  Wallpaper + logo + widgets
 *   Row  23    Desktop hints bar (DARK_GREY bg)
 *   Row  24    Taskbar/panel
 *
 * Panel (80 chars):
 *   0-8   [► FurOS]   kickoff  (9)
 *   9     │
 *  10-42  [Konsole ][Dolphin ][KSysGrd]  task buttons (33, 3×11)
 *  43     │
 *  44-55  [1][2][3][4]  pager (12)
 *  56     │
 *  57-73  FurOS 1.0 ◆         systray (17)
 *  74     │
 *  75-79  [OFF]               power   (5)
 */
#include "desktop.h"
#include "disk.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "shell.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define W          80
#define TITLEROW    0   /* window title bar */
#define CTOP        1   /* content area top */
#define CBOT       22   /* content area bottom */
#define STATROW    23   /* window status / desktop hints */
#define PANROW     24   /* always-visible panel */

/* ── VGA color shortcuts ─────────────────────────────────────────────────── */
#define BLK  VGA_COLOR_BLACK
#define BLU  VGA_COLOR_BLUE
#define GRN  VGA_COLOR_GREEN
#define CYN  VGA_COLOR_CYAN
#define RED  VGA_COLOR_RED
#define MAG  VGA_COLOR_MAGENTA
#define DGR  VGA_COLOR_DARK_GREY
#define LGR  VGA_COLOR_LIGHT_GREY
#define LBL  VGA_COLOR_LIGHT_BLUE
#define LGN  VGA_COLOR_LIGHT_GREEN
#define LCN  VGA_COLOR_LIGHT_CYAN
#define LRD  VGA_COLOR_LIGHT_RED
#define LMG  VGA_COLOR_LIGHT_MAGENTA
#define WHT  VGA_COLOR_WHITE

/* ── Theme colors ────────────────────────────────────────────────────────── */
/* Desktop wallpaper */
#define CD_FG  LGR
#define CD_BG  BLU
/* Logo */
#define CL_FG  LCN
#define CL_BG  BLU
/* Desktop subtitle/hints */
#define CH_FG  DGR
#define CH_BG  BLU
/* Panel background */
#define CP_FG  WHT
#define CP_BG  DGR
/* Kickoff launcher button */
#define CK_FG  WHT
#define CK_BG  MAG
/* Task button inactive */
#define CTi_FG LGR
#define CTi_BG DGR
/* Task button active */
#define CTa_FG WHT
#define CTa_BG BLU
/* Pager inactive */
#define CPi_FG DGR
#define CPi_BG DGR
/* Pager active */
#define CPa_FG WHT
#define CPa_BG LGR
/* System tray */
#define CS_FG  LCN
#define CS_BG  DGR
/* Power button */
#define CW_FG  LRD
#define CW_BG  DGR
/* Window title bar */
#define CWT_FG WHT
#define CWT_BG MAG
/* Window [^][O] buttons */
#define CWB_FG LGR
#define CWB_BG DGR
/* Window [X] button */
#define CWX_FG WHT
#define CWX_BG RED
/* Window status bar */
#define CWS_FG LGR
#define CWS_BG DGR
/* App content area */
#define CAP_FG LGR
#define CAP_BG BLK
/* Kickoff popup header */
#define CKH_FG WHT
#define CKH_BG MAG
/* Kickoff popup body */
#define CKP_FG LGR
#define CKP_BG BLK
/* Kickoff item selected */
#define CKS_FG WHT
#define CKS_BG BLU
/* Kickoff separator */
#define CKD_FG DGR
#define CKD_BG BLK
/* File manager header bar */
#define CFH_FG WHT
#define CFH_BG DGR
/* File manager dir entries */
#define CFD_FG LCN
#define CFD_BG BLK
/* File manager file entries */
#define CFF_FG LGR
#define CFF_BG BLK
/* File manager selected */
#define CFS_FG WHT
#define CFS_BG BLU
/* Sysmon bar filled */
#define CMF_FG LCN
#define CMF_BG BLK
/* Sysmon bar empty */
#define CME_FG DGR
#define CME_BG BLK
/* Dialog box */
#define CDL_FG LGR
#define CDL_BG BLK
/* Dialog button normal */
#define CDB_FG WHT
#define CDB_BG DGR
/* Dialog button selected */
#define CDBS_FG WHT
#define CDBS_BG BLU

/* ── CP437 box-drawing chars ─────────────────────────────────────────────── */
#define CH_   '\xC4'   /* ─ */
#define CV_   '\xB3'   /* │ */
#define CTL_  '\xDA'   /* ┌ */
#define CTR_  '\xBF'   /* ┐ */
#define CBL_  '\xC0'   /* └ */
#define CBR_  '\xD9'   /* ┘ */
#define CLT_  '\xC3'   /* ├ */
#define CRT_  '\xB4'   /* ┤ */
#define CDIA_ '\x04'   /* ♦ */
#define CTRI_ '\x10'   /* ► */
#define CSQR_ '\xFE'   /* ■ */
#define CBLK_ '\xDB'   /* █ */
#define CLBK_ '\xB0'   /* ░ */
#define CDOT_ '\xFA'   /* · */

/* ── Global state ────────────────────────────────────────────────────────── */
static int g_ws  = 0;   /* current virtual desktop 0-3 */
static int g_app = -1;  /* active app: -1=desktop, 0=terminal, 1=fileman, 2=sysmon */

#define NAPP 3
static const char *g_appnames[NAPP] = { "Konsole", "Dolphin", "KSysGuard" };

/* ── String utilities ────────────────────────────────────────────────────── */
static int slen(const char *s) { int n = 0; while (s[n]) { n++; } return n; }

static void spad(char *b, const char *s, int w) {
    int i, l = slen(s);
    for (i = 0; i < w; i++) { b[i] = (i < l) ? s[i] : ' '; }
    b[w] = '\0';
}

static void scenter(int y, const char *s, uint8_t fg, uint8_t bg) {
    int x = (W - slen(s)) / 2;
    if (x < 0) { x = 0; }
    vga_write_str_at(x, y, s, fg, bg);
}

/* Unsigned int → decimal string into caller-supplied buf[16] */
static const char *u2s(unsigned v, char *buf) {
    int i = 14;
    buf[15] = '\0';
    if (!v) { buf[14] = '0'; return &buf[14]; }
    while (v && i > 0) { buf[--i] = '0' + (char)(v % 10u); v /= 10u; }
    return &buf[i];
}

/* ── Low-level drawing helpers ───────────────────────────────────────────── */
static void hline(int x, int y, int w, uint8_t fg, uint8_t bg) {
    int i;
    for (i = 0; i < w; i++) { vga_put_at(x + i, y, CH_, fg, bg); }
}

/* Single-line box with cleared interior */
static void bbox(int x, int y, int w, int h, uint8_t fg, uint8_t bg) {
    int i, j;
    vga_put_at(x,     y,     CTL_, fg, bg);
    vga_put_at(x+w-1, y,     CTR_, fg, bg);
    vga_put_at(x,     y+h-1, CBL_, fg, bg);
    vga_put_at(x+w-1, y+h-1, CBR_, fg, bg);
    for (i = 1; i < w-1; i++) {
        vga_put_at(x+i, y,     CH_, fg, bg);
        vga_put_at(x+i, y+h-1, CH_, fg, bg);
    }
    for (j = 1; j < h-1; j++) {
        vga_put_at(x,     y+j, CV_, fg, bg);
        vga_put_at(x+w-1, y+j, CV_, fg, bg);
        vga_fill_rect(x+1, y+j, w-2, 1, ' ', fg, bg);
    }
}

/* Titled section box:  ┌─┤ Title ├───┐ */
static void titbox(int x, int y, int w, int h,
                   const char *title, uint8_t fg, uint8_t bg) {
    int tl = slen(title);
    int tx = x + 2;
    bbox(x, y, w, h, fg, bg);
    vga_put_at(tx,      y, CLT_, fg, bg);   /* ├ */
    vga_write_str_at(tx+1, y, title, fg, bg);
    vga_put_at(tx+1+tl, y, CRT_, fg, bg);  /* ┤ */
}

/* Progress bar: ████░░░░░ pct% */
static void draw_bar(int x, int y, int barw, int pct,
                     uint8_t ffg, uint8_t fbg, uint8_t efg, uint8_t ebg) {
    int filled = (barw * pct) / 100;
    int i;
    for (i = 0; i < barw; i++) {
        if (i < filled) { vga_put_at(x+i, y, CBLK_, ffg, fbg); }
        else            { vga_put_at(x+i, y, CLBK_, efg, ebg); }
    }
}

/* ── Panel ────────────────────────────────────────────────────────────────── */
static void draw_panel(void) {
    char tb[10];
    int i;
    vga_fill_rect(0, PANROW, W, 1, ' ', CP_FG, CP_BG);

    /* Kickoff button (9 chars, 0-8) */
    vga_write_str_at(0, PANROW, " \x10 FurOS ", CK_FG, CK_BG);
    vga_put_at(9, PANROW, CV_, CP_FG, CP_BG);

    /* Task buttons 3×11 (10-42) */
    for (i = 0; i < NAPP; i++) {
        int bx = 10 + i * 11;
        int act = (g_app == i);
        uint8_t fg = act ? CTa_FG : CTi_FG;
        uint8_t bg = act ? CTa_BG : CTi_BG;
        spad(tb, g_appnames[i], 9);
        vga_put_at(bx,    PANROW, '[', fg, bg);
        vga_write_str_at(bx+1, PANROW, tb, fg, bg);
        vga_put_at(bx+10, PANROW, ']', fg, bg);
    }
    vga_put_at(43, PANROW, CV_, CP_FG, CP_BG);

    /* Virtual desktop pager [1][2][3][4] (44-55) */
    for (i = 0; i < 4; i++) {
        int px = 44 + i * 3;
        int act = (g_ws == i);
        uint8_t fg = act ? CPa_FG : CPi_FG;
        uint8_t bg = act ? CPa_BG : CPi_BG;
        vga_put_at(px,   PANROW, '[',          fg, bg);
        vga_put_at(px+1, PANROW, '1'+(char)i,  fg, bg);
        vga_put_at(px+2, PANROW, ']',          fg, bg);
    }
    vga_put_at(56, PANROW, CV_, CP_FG, CP_BG);

    /* System tray: FurOS 1.0 ◆ (57-73) */
    vga_write_str_at(57, PANROW, " FurOS 1.0 ", CS_FG, CS_BG);
    vga_put_at(68, PANROW, CDIA_, CS_FG, CS_BG);
    vga_write_str_at(69, PANROW, "     ", CS_FG, CS_BG);
    vga_put_at(74, PANROW, CV_, CP_FG, CP_BG);

    /* Power button [OFF] (75-79) */
    vga_write_str_at(75, PANROW, "[OFF]", CW_FG, CW_BG);
}

/* ── Window chrome ───────────────────────────────────────────────────────── */
/*
 * Title bar row 0:
 *   0-67  (68) : " ◆ AppName — subtitle" on MAGENTA
 *   68         : │  on DARK_GREY (separator before buttons)
 *   69-71 (3)  : [^] minimize
 *   72-74 (3)  : [O] maximize
 *   75         : │  separator
 *   76-78 (3)  : [X] on RED
 *   79         : ' ' on RED
 */
static void open_window(const char *appname, const char *sub) {
    char title[72];
    int  pos = 0, i;

    /* Build title string into title[] (max 67 chars) */
    title[pos++] = ' ';
    title[pos++] = CDIA_;
    title[pos++] = ' ';
    for (i = 0; appname[i] && pos < 66; i++) { title[pos++] = appname[i]; }
    if (sub && sub[0]) {
        if (pos < 65) { title[pos++] = ' '; title[pos++] = CV_; title[pos++] = ' '; }
        for (i = 0; sub[i] && pos < 66; i++) { title[pos++] = sub[i]; }
    }
    while (pos < 68) { title[pos++] = ' '; }
    title[68] = '\0';

    vga_fill_rect(0, TITLEROW, 68, 1, ' ', CWT_FG, CWT_BG);
    vga_write_str_at(0, TITLEROW, title, CWT_FG, CWT_BG);

    /* Button area */
    vga_put_at(68, TITLEROW, CV_, CWB_FG, CWB_BG);
    vga_write_str_at(69, TITLEROW, "[^]", CWB_FG, CWB_BG);
    vga_write_str_at(72, TITLEROW, "[O]", CWB_FG, CWB_BG);
    vga_put_at(75, TITLEROW, CV_, CWB_FG, CWB_BG);
    vga_write_str_at(76, TITLEROW, "[X]", CWX_FG, CWX_BG);
    vga_put_at(79, TITLEROW, ' ', CWX_FG, CWX_BG);

    /* Status bar */
    vga_fill_rect(0, STATROW, W, 1, ' ', CWS_FG, CWS_BG);
    vga_write_str_at(1, STATROW, " FurOS Plasma Edition", CWS_FG, CWS_BG);
    vga_write_str_at(W - 13, STATROW, " ESC: Close ", CWS_FG, CWS_BG);
}

/* Update status bar text (left side) */
static void set_statusbar(const char *msg) {
    vga_fill_rect(1, STATROW, W - 14, 1, ' ', CWS_FG, CWS_BG);
    vga_write_str_at(1, STATROW, msg, CWS_FG, CWS_BG);
}

/* ── Desktop background + widgets ────────────────────────────────────────── */
static void draw_wallpaper(void) {
    int x, y;
    /* Dark blue with subtle dot grid for Plasma-like texture */
    for (y = 0; y < STATROW; y++) {
        for (x = 0; x < W; x++) {
            unsigned char c = ' ';
            uint8_t fg = CD_FG;
            if ((x & 3) == 0 && (y & 1) == 0) {
                c  = CDOT_;   /* · every 4 cols, every 2 rows */
                fg = DGR;     /* very subtle on blue */
            }
            vga_put_at(x, y, c, fg, CD_BG);
        }
    }
}

static void draw_logo(void) {
    scenter(2, "  ______           ____  ____",    CL_FG, CL_BG);
    scenter(3, " |  ____|         / __ \\/ ___|",   CL_FG, CL_BG);
    scenter(4, " | |__ _   _ _ __| |  | \\___ \\",  CL_FG, CL_BG);
    scenter(5, " |  __| | | | '__| |  | |___) |",  CL_FG, CL_BG);
    scenter(6, " | |  | |_| | |  | |__| |____/",   CL_FG, CL_BG);
    scenter(7, " |_|   \\__,_|_|   \\____/",          CL_FG, CL_BG);
    scenter(9, "FurOS 1.0  \xb3  Plasma Edition",   WHT,   CL_BG);
    scenter(10,"Based on Debian GNU/Linux 12 Bookworm", CH_FG, CH_BG);
}

static void draw_syswidget(void) {
    /* Small system info widget, top-right, rows 1-5, cols 54-77 (24 wide) */
    struct disk_info di;
    int have_disk = (disk_count() > 0 && disk_get_info(0, &di) == 0 && di.present);
    titbox(54, 1, 25, 6, " System ", LGR, DGR);
    vga_write_str_at(56, 2, "FurOS 1.0 Plasma   ", LCN, DGR);
    vga_write_str_at(56, 3, "Arch: x86_64       ", LGR, DGR);
    vga_write_str_at(56, 4, "VGA:  80\xd7" "25 16col ", LGR, DGR);
    if (have_disk) {
        vga_write_str_at(56, 5, "Disk: Available    ", LGN, DGR);
    } else {
        vga_write_str_at(56, 5, "Disk: None         ", LRD, DGR);
    }
}

static void draw_desk_hints(void) {
    /* Hint bar at STATROW */
    vga_fill_rect(0, STATROW, W, 1, ' ', CWS_FG, CWS_BG);
    vga_write_str_at(1, STATROW,
        "[ \x10 FurOS ]: Apps   [1-4]: Workspaces   [OFF]: Shutdown",
        CWS_FG, CWS_BG);
}

static void draw_desktop(void) {
    draw_wallpaper();
    draw_logo();
    draw_syswidget();
    draw_desk_hints();
    draw_panel();
}

/* ── Kickoff Application Launcher ────────────────────────────────────────── */
/*
 * Popup: rows 11-23 (13 rows), cols 0-31 (32 wide)
 *  Row 11: header bar
 *  Row 12: "  FurOS 1.0 Plasma Edition"  (subtitle)
 *  Row 13: ├────────────────────────────┤
 *  Rows 14-16: app items 0-2
 *  Row 17: separator
 *  Rows 18-19: items 3-4  (about, shutdown)
 *  Row 20: separator
 *  Rows 21-22: blank
 *  Row 23: "  ESC: Close launcher"
 *
 * Selectable items: 0=Konsole, 1=Dolphin, 2=KSysGuard, 3=About, 4=Shutdown
 */
#define KO_X     0
#define KO_Y    11
#define KO_W    32
#define KO_H    13   /* rows 11-23 */
#define KO_NITEMS 5

static const char *ko_item_labels[KO_NITEMS] = {
    "  \x10 Konsole (Terminal)   ",
    "  \x10 Dolphin (Files)      ",
    "  \x10 KSysGuard            ",
    "  \x10 About FurOS          ",
    "  \x10 Shut Down...         ",
};
/* Which row (absolute) each item is drawn on */
static const int ko_item_rows[KO_NITEMS] = { 14, 15, 16, 18, 19 };

static void draw_kickoff(int sel) {
    int i;
    char ibuf[KO_W - 1];

    /* Background */
    bbox(KO_X, KO_Y, KO_W, KO_H, CKP_FG, CKP_BG);

    /* Header */
    vga_fill_rect(KO_X+1, KO_Y, KO_W-2, 1, ' ', CKH_FG, CKH_BG);
    vga_write_str_at(KO_X+2, KO_Y, " \x10 FurOS Applications ", CKH_FG, CKH_BG);

    /* Subtitle */
    vga_write_str_at(KO_X+2, KO_Y+1, "FurOS 1.0 Plasma Edition", LGR, CKP_BG);

    /* First separator (row 13) */
    vga_put_at(KO_X, KO_Y+2, CLT_, CKP_FG, CKP_BG);
    hline(KO_X+1, KO_Y+2, KO_W-2, CKD_FG, CKD_BG);
    vga_put_at(KO_X+KO_W-1, KO_Y+2, CRT_, CKP_FG, CKP_BG);

    /* App items 0-2 (rows 14-16) */
    for (i = 0; i < KO_NITEMS; i++) {
        int row = ko_item_rows[i];
        int act = (sel == i);
        uint8_t fg = act ? CKS_FG : CKP_FG;
        uint8_t bg = act ? CKS_BG : CKP_BG;
        spad(ibuf, ko_item_labels[i], KO_W - 2);
        vga_write_str_at(KO_X+1, row, ibuf, fg, bg);

        /* Draw separator after item 2 and after item 3 */
        if (i == 2) {
            vga_put_at(KO_X, row+1, CLT_, CKP_FG, CKP_BG);
            hline(KO_X+1, row+1, KO_W-2, CKD_FG, CKD_BG);
            vga_put_at(KO_X+KO_W-1, row+1, CRT_, CKP_FG, CKP_BG);
        }
        if (i == 3) {
            vga_put_at(KO_X, row+1, CLT_, CKP_FG, CKP_BG);
            hline(KO_X+1, row+1, KO_W-2, CKD_FG, CKD_BG);
            vga_put_at(KO_X+KO_W-1, row+1, CRT_, CKP_FG, CKP_BG);
        }
    }

    /* Footer hint (row 23, inside box at offset KO_H-1 from KO_Y) */
    vga_write_str_at(KO_X+2, KO_Y+KO_H-1, "  ESC: Close launcher   ", CKD_FG, CKP_BG);
}

/* Returns app index (0-2), 3=About, 4=Shutdown, -1=Escape */
static int run_kickoff(void) {
    int sel = 0;
    draw_kickoff(sel);
    for (;;) {
        int k = keyboard_get_key();
        if (k == KEY_UP) {
            if (sel > 0) { sel--; }
            draw_kickoff(sel);
        } else if (k == KEY_DOWN) {
            if (sel < KO_NITEMS - 1) { sel++; }
            draw_kickoff(sel);
        } else if (k == '\n' || k == '\r' || k == ' ') {
            return sel;
        } else if (k == 0x1B) {  /* Escape */
            return -1;
        }
    }
}

/* ── About FurOS dialog ───────────────────────────────────────────────────── */
/* Centered box: 60×14 at x=10, y=5 */
static void run_about(void) {
    int bx = 10, by = 5, bw = 60, bh = 14;
    titbox(bx, by, bw, bh, " About FurOS ", LGR, BLK);

    vga_write_str_at(bx+2, by+1,  "  ______           ____  ____",           LCN, BLK);
    vga_write_str_at(bx+2, by+2,  " |  ____|         / __ \\/ ___|",          LCN, BLK);
    vga_write_str_at(bx+2, by+3,  " | |__ _   _ _ __| |  | \\___ \\",         LCN, BLK);
    vga_write_str_at(bx+2, by+4,  " |  __| | | | '__| |  | |___) |",         LCN, BLK);
    vga_write_str_at(bx+2, by+5,  " | |  | |_| | |  | |__| |____/",          LCN, BLK);
    vga_write_str_at(bx+2, by+6,  " |_|   \\__,_|_|   \\____/",               LCN, BLK);
    scenter(by+7, "FurOS 1.0  Plasma Edition",                                  WHT, BLK);
    scenter(by+8, "Based on Debian GNU/Linux 12 Bookworm",                      LGR, BLK);
    scenter(by+9, "Kernel: FurOS 1.0  |  VGA: 80\xd7" "25  |  x86_64",        LGR, BLK);
    hline(bx+1, by+10, bw-2, DGR, BLK);
    scenter(by+11, "[ Close ]",                                                  WHT, DGR);
    scenter(by+12, "Press any key to close",                                     DGR, BLK);

    /* Highlight the [ Close ] button background */
    vga_fill_rect(bx + (bw - 11) / 2, by+11, 9, 1, ' ', WHT, DGR);
    vga_write_str_at(bx + (bw - 11) / 2, by+11, "[ Close ]",                   WHT, DGR);

    keyboard_get_key();
}

/* ── Shutdown dialog ──────────────────────────────────────────────────────── */
/* Returns 1 = shut down, 0 = cancel */
static int run_shutdown(void) {
    int bx = 18, by = 8, bw = 44, bh = 9;
    int sel = 1; /* default: Cancel */
    for (;;) {
        titbox(bx, by, bw, bh, " Shut Down FurOS ", LGR, BLK);
        vga_write_str_at(bx+4, by+2, "Do you want to shut down FurOS?", WHT, BLK);
        vga_write_str_at(bx+4, by+3, "Any unsaved data will be lost.",  LGR, BLK);

        /* Buttons */
        vga_write_str_at(bx+5,  by+5, "[ Shut Down ]",
            (sel == 0) ? CDBS_FG : CDB_FG,
            (sel == 0) ? CDBS_BG : CDB_BG);
        vga_write_str_at(bx+22, by+5, "[ Cancel ]",
            (sel == 1) ? CDBS_FG : CDB_FG,
            (sel == 1) ? CDBS_BG : CDB_BG);
        vga_write_str_at(bx+4, by+7, " \x1b/\x1a: Select   ENTER: Confirm ", DGR, BLK);

        int k = keyboard_get_key();
        if (k == KEY_LEFT  || k == KEY_RIGHT) { sel ^= 1; }
        if (k == '\t')                         { sel ^= 1; }
        if (k == '\n' || k == '\r')            { return (sel == 0) ? 1 : 0; }
        if (k == 0x1B)                         { return 0; }
    }
}

/* ── System Monitor (KSysGuard-style) ────────────────────────────────────── */
static void run_sysmon(void) {
    struct disk_info di;
    char nbuf[16];
    int  i;

    g_app = 2;
    open_window("KSysGuard", "System Monitor");
    draw_panel();
    vga_fill_rect(0, CTOP, W, CBOT - CTOP + 1, ' ', CAP_FG, CAP_BG);

    /* ── CPU ── */
    titbox(1, CTOP,     W-2, 3, " CPU ", LGR, BLK);
    vga_write_str_at(3, CTOP+1, "Processor: x86_64  FurOS Kernel  |  Usage: ", LGR, BLK);
    draw_bar(46, CTOP+1, 22, 12, CMF_FG, CMF_BG, CME_FG, CME_BG);
    vga_write_str_at(69, CTOP+1, " 12%", LCN, BLK);

    /* ── Memory ── */
    titbox(1, CTOP+3,   W-2, 3, " Memory ", LGR, BLK);
    vga_write_str_at(3, CTOP+4, "RAM: 1024 MB total  |  Used: 256 MB   ", LGR, BLK);
    draw_bar(40, CTOP+4, 28, 25, CMF_FG, CMF_BG, CME_FG, CME_BG);
    vga_write_str_at(69, CTOP+4, " 25%", LCN, BLK);

    /* ── Disks ── */
    titbox(1, CTOP+6,   W-2, 5, " Storage ", LGR, BLK);
    for (i = 0; i < 3; i++) {
        int row = CTOP + 7 + i;
        if (disk_get_info(i, &di) == 0 && di.present) {
            char sbuf[16];
            unsigned mb = di.sectors28 / 2048u;
            const char *sz = u2s(mb, sbuf);
            vga_write_str_at(3,  row, "Disk ", LGR, BLK);
            vga_put_at(8,        row, '0' + (char)i, LCN, BLK);
            vga_write_str_at(9,  row, ": ", LGR, BLK);
            vga_write_str_at(11, row, di.model, LGR, BLK);
            vga_write_str_at(52, row, sz, LCN, BLK);
            vga_write_str_at(52 + slen(sz), row, " MB  Active", LGN, BLK);
        } else {
            vga_write_str_at(3, row, "Disk ", LGR, BLK);
            vga_put_at(8, row, '0' + (char)i, DGR, BLK);
            vga_write_str_at(9, row, ": Not detected", DGR, BLK);
        }
    }

    /* ── System ── */
    titbox(1, CTOP+11,  W-2, 8, " System Information ", LGR, BLK);
    vga_write_str_at(3, CTOP+12, "Operating System :", LGR, BLK);
    vga_write_str_at(23,CTOP+12, "FurOS 1.0 Plasma Edition", LCN, BLK);
    vga_write_str_at(3, CTOP+13, "Kernel           :", LGR, BLK);
    vga_write_str_at(23,CTOP+13, "FurOS 1.0 (x86_64, bare-metal)", LCN, BLK);
    vga_write_str_at(3, CTOP+14, "Display          :", LGR, BLK);
    vga_write_str_at(23,CTOP+14, "VGA Text Mode 80\xd7" "25 (16 colors)", LCN, BLK);
    vga_write_str_at(3, CTOP+15, "Desktop          :", LGR, BLK);
    vga_write_str_at(23,CTOP+15, "KDE Plasma (FurOS Edition)", LCN, BLK);
    vga_write_str_at(3, CTOP+16, "Shell            :", LGR, BLK);
    vga_write_str_at(23,CTOP+16, "FurSH 1.0", LCN, BLK);
    vga_write_str_at(3, CTOP+17, "Disks detected   :", LGR, BLK);
    vga_write_str_at(23,CTOP+17, u2s((unsigned)disk_count(), nbuf), LCN, BLK);

    set_statusbar(" KSysGuard — System Monitor");

    /* Wait for ESC */
    for (;;) {
        int k = keyboard_get_key();
        if (k == 0x1B) { break; }
    }
    g_app = -1;
}

/* ── File Manager (Dolphin-style) ────────────────────────────────────────── */
#define FM_ROWS   19    /* visible rows: CTOP+2 to CTOP+20 */
#define FM_MAX   128

static int  fm_entries[FM_MAX];
static int  fm_nent;
static int  fm_cwd;
static int  fm_sel;
static int  fm_scroll;

static void fm_load_dir(void) {
    fm_nent   = fs_list(fm_cwd, fm_entries, FM_MAX);
    fm_sel    = 0;
    fm_scroll = 0;
}

static void fm_draw(void) {
    char pathbuf[80];
    char lbuf[W + 1];
    int  i, row;

    /* Header */
    vga_fill_rect(0, CTOP, W, 1, ' ', CFH_FG, CFH_BG);
    fs_make_path(fm_cwd, pathbuf, sizeof(pathbuf));
    vga_write_str_at(1, CTOP, " Location: ", CFH_FG, CFH_BG);
    vga_write_str_at(12, CTOP, pathbuf, LCN, CFH_BG);

    /* Separator */
    hline(0, CTOP+1, W, DGR, BLK);

    /* File entries */
    vga_fill_rect(0, CTOP+2, W, FM_ROWS, ' ', CAP_FG, CAP_BG);
    for (i = 0; i < FM_ROWS; i++) {
        int ei = fm_scroll + i;
        if (ei >= fm_nent) { break; }
        row = CTOP + 2 + i;
        int node = fm_entries[ei];
        int isdir = fs_is_dir(node);
        int act   = (ei == fm_sel);
        uint8_t fg = act ? CFS_FG : (isdir ? CFD_FG : CFF_FG);
        uint8_t bg = act ? CFS_BG : CAP_BG;
        const char *name = fs_get_name(node);
        /* Build entry line: "  [D] name/"  or "  [F] name" */
        if (isdir) {
            spad(lbuf, "  [D] ", 6);
        } else {
            spad(lbuf, "  [F] ", 6);
        }
        int nlen = slen(name);
        int j;
        for (j = 0; j < nlen && j + 6 < W - 4; j++) {
            lbuf[6 + j] = name[j];
        }
        if (isdir && j + 6 < W - 4) { lbuf[6 + j++] = '/'; }
        while (6 + j < W) { lbuf[6 + j++] = ' '; }
        lbuf[W] = '\0';
        vga_write_str_at(0, row, lbuf, fg, bg);
    }

    /* Separator before status */
    hline(0, CTOP+2+FM_ROWS, W, DGR, BLK);

    /* Status line */
    char stbuf[32];
    vga_fill_rect(0, CTOP+2+FM_ROWS+1, W, 1, ' ', CWS_FG, CWS_BG);
    u2s((unsigned)fm_nent, stbuf);
    vga_write_str_at(1, CTOP+2+FM_ROWS+1, stbuf, LCN, CWS_BG);
    vga_write_str_at(1 + slen(stbuf), CTOP+2+FM_ROWS+1,
        " items   \x18\x19: Nav   Enter: Open   Bksp: Up   ESC: Close",
        CWS_FG, CWS_BG);
}

/* View a file in a simple popup */
static void fm_view_file(int node) {
    const char *data = 0;
    size_t sz = 0;
    char bx_buf[80];
    int bx = 4, by = CTOP, bw = W - 8, bh = CBOT - CTOP + 1;
    int scrolly = 0;
    const char *name = fs_get_name(node);

    /* Read directly from root-relative path approach */
    fs_make_path(node, bx_buf, sizeof(bx_buf));
    fs_read_file(fs_get_root(), bx_buf, &data, &sz);

    for (;;) {
        int row, col, ci;
        titbox(bx, by, bw, bh, name, LGR, BLK);
        vga_fill_rect(bx+1, by+1, bw-2, bh-2, ' ', CAP_FG, CAP_BG);

        /* Render text line by line starting at scrolly */
        row = 0;
        col = 0;
        ci  = 0;
        if (data) {
            /* Skip scrolly lines */
            int ln = 0;
            while (ci < (int)sz && ln < scrolly) {
                if (data[ci++] == '\n') { ln++; }
            }
            while (ci < (int)sz && row < bh - 2) {
                char c = data[ci++];
                if (c == '\n') { row++; col = 0; continue; }
                if (col < bw - 2) {
                    vga_put_at(bx+1+col, by+1+row, (unsigned char)c, CAP_FG, CAP_BG);
                    col++;
                }
            }
        } else {
            vga_write_str_at(bx+2, by+2, "(empty file)", DGR, CAP_BG);
        }

        int k = keyboard_get_key();
        if (k == 0x1B || k == 'q' || k == 'Q') { break; }
        if (k == KEY_UP   && scrolly > 0) { scrolly--; }
        if (k == KEY_DOWN)                { scrolly++; }
    }
}

static void run_fileman(void) {
    g_app   = 1;
    fm_cwd  = fs_get_root();
    fm_load_dir();

    open_window("Dolphin", "File Manager");
    draw_panel();

    for (;;) {
        fm_draw();
        set_statusbar(" Dolphin — File Manager");

        int k = keyboard_get_key();

        if (k == 0x1B) { break; }

        if (k == KEY_UP) {
            if (fm_sel > 0) {
                fm_sel--;
                if (fm_sel < fm_scroll) { fm_scroll = fm_sel; }
            }
            continue;
        }
        if (k == KEY_DOWN) {
            if (fm_sel < fm_nent - 1) {
                fm_sel++;
                if (fm_sel >= fm_scroll + FM_ROWS) { fm_scroll = fm_sel - FM_ROWS + 1; }
            }
            continue;
        }

        /* Enter: open dir or view file */
        if (k == '\n' || k == '\r') {
            if (fm_nent > 0) {
                int node = fm_entries[fm_sel];
                if (fs_is_dir(node)) {
                    fm_cwd = node;
                    fm_load_dir();
                } else {
                    fm_view_file(node);
                    open_window("Dolphin", "File Manager");
                    draw_panel();
                }
            }
            continue;
        }

        /* Backspace: go up */
        if (k == '\b' || k == 127) {
            int parent = fs_get_parent(fm_cwd);
            if (parent >= 0) {
                fm_cwd = parent;
                fm_load_dir();
            }
            continue;
        }

        /* V: view selected file */
        if ((k == 'v' || k == 'V') && fm_nent > 0) {
            int node = fm_entries[fm_sel];
            if (!fs_is_dir(node)) {
                fm_view_file(node);
                open_window("Dolphin", "File Manager");
                draw_panel();
            }
            continue;
        }
    }
    g_app = -1;
}

/* ── Terminal (Konsole-style) ────────────────────────────────────────────── */
static void run_terminal(void) {
    g_app = 0;
    open_window("Konsole", "FurOS Terminal");
    draw_panel();

    /* Confine shell to rows CTOP-CBOT; title bar and status bar are outside */
    vga_set_viewport(CTOP, CBOT);
    vga_set_color(LGR, BLK);
    vga_clear();

    shell_run();   /* returns on 'logout' or 'exit' */

    vga_reset_viewport();
    g_app = -1;
}

/* ── Main desktop event loop ─────────────────────────────────────────────── */
__attribute__((noreturn))
void desktop_run(void) {
    for (;;) {
        draw_desktop();

        for (;;) {
            int k = keyboard_get_key();

            /* F5: refresh */
            if (k == KEY_F5) { break; }

            /* Virtual desktop switch: F1-F4 */
            if (k >= KEY_F1 && k <= KEY_F4) {
                g_ws = k - KEY_F1;
                draw_panel();
                continue;
            }

            /* Kickoff: Enter / Space on panel button area (K shortcut) */
            if (k == 'k' || k == 'K') {
                int choice = run_kickoff();
                if (choice == 0) { run_terminal(); break; }
                if (choice == 1) { run_fileman();  break; }
                if (choice == 2) { run_sysmon();   break; }
                if (choice == 3) { run_about();    break; }
                if (choice == 4) {
                    if (run_shutdown()) {
                        /* Halt: clear screen and stop */
                        vga_set_color(LGR, BLK);
                        vga_clear();
                        scenter(12, "FurOS has shut down. It is now safe to power off.", LGR, BLK);
                        for (;;) { __asm__ volatile("hlt"); }
                    }
                }
                /* Escape or cancelled */
                break;
            }

            /* Direct app shortcuts */
            if (k == 't' || k == 'T') { run_terminal(); break; }
            if (k == 'f' || k == 'F') { run_fileman();  break; }
            if (k == 's' || k == 'S') { run_sysmon();   break; }
            if (k == 'a' || k == 'A') { run_about();    break; }

            /* Task bar buttons: 1-3 on keyboard */
            if (k == '1') { run_terminal(); break; }
            if (k == '2') { run_fileman();  break; }
            if (k == '3') { run_sysmon();   break; }
        }
    }
}
