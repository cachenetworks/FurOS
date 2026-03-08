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
/*
 * Kickoff popup: rows 8-23 (16 rows), cols 0-31
 *  8:  header
 *  9:  subtitle
 * 10:  separator
 * 11:  Konsole        (item 0)
 * 12:  Dolphin        (item 1)
 * 13:  KSysGuard      (item 2)
 * 14:  separator
 * 15:  Kate           (item 3)
 * 16:  System Settings(item 4)
 * 17:  separator
 * 18:  About FurOS    (item 5)
 * 19:  Shut Down      (item 6)
 * 20:  separator
 * 21:  (blank)
 * 22:  ESC hint
 * 23:  bottom border
 */
#define KO_X      0
#define KO_Y      8
#define KO_W     32
#define KO_H     16   /* rows 8-23 */
#define KO_NITEMS 7

static const char *ko_item_labels[KO_NITEMS] = {
    "  \x10 Konsole (Terminal)   ",
    "  \x10 Dolphin (Files)      ",
    "  \x10 KSysGuard            ",
    "  \x10 Kate (Text Editor)   ",
    "  \x10 System Settings      ",
    "  \x10 About FurOS          ",
    "  \x10 Shut Down...         ",
};
/* Absolute row for each item (within the popup) */
static const int ko_item_rows[KO_NITEMS] = { 11, 12, 13, 15, 16, 18, 19 };

static void draw_kickoff(int sel) {
    int i;
    char ibuf[KO_W - 1];

    /* Box + clear interior */
    bbox(KO_X, KO_Y, KO_W, KO_H, CKP_FG, CKP_BG);

    /* Header bar */
    vga_fill_rect(KO_X + 1, KO_Y, KO_W - 2, 1, ' ', CKH_FG, CKH_BG);
    vga_write_str_at(KO_X + 2, KO_Y, " \x10 FurOS Applications ", CKH_FG, CKH_BG);

    /* Subtitle row */
    vga_write_str_at(KO_X + 2, KO_Y + 1, "FurOS 1.0 Plasma Edition", LGR, CKP_BG);

    /* Separator after subtitle (row KO_Y+2 = row 10) */
    vga_put_at(KO_X,        KO_Y + 2, CLT_, CKP_FG, CKP_BG);
    hline(KO_X + 1,         KO_Y + 2, KO_W - 2, CKD_FG, CKD_BG);
    vga_put_at(KO_X + KO_W - 1, KO_Y + 2, CRT_, CKP_FG, CKP_BG);

    /* Items */
    for (i = 0; i < KO_NITEMS; i++) {
        int row = ko_item_rows[i];
        int act = (sel == i);
        uint8_t fg = act ? CKS_FG : CKP_FG;
        uint8_t bg = act ? CKS_BG : CKP_BG;
        spad(ibuf, ko_item_labels[i], KO_W - 2);
        vga_write_str_at(KO_X + 1, row, ibuf, fg, bg);

        /* Separator after items 2, 4, 6 */
        if (i == 2 || i == 4 || i == 6) {
            vga_put_at(KO_X,            row + 1, CLT_, CKP_FG, CKP_BG);
            hline(KO_X + 1,             row + 1, KO_W - 2, CKD_FG, CKD_BG);
            vga_put_at(KO_X + KO_W - 1, row + 1, CRT_, CKP_FG, CKP_BG);
        }
    }

    /* ESC hint two rows above bottom border */
    vga_write_str_at(KO_X + 2, KO_Y + KO_H - 2, "  ESC: Close launcher   ", CKD_FG, CKP_BG);
}

/* Returns: 0=Konsole 1=Dolphin 2=KSysGuard 3=Kate 4=Settings 5=About 6=Shutdown -1=Esc */
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

/* ── Kate Text Editor ────────────────────────────────────────────────────── */
/*
 * Layout inside app window (rows CTOP..CBOT = 1..22):
 *   Row  CTOP+0  tab bar (filename, modified star)
 *   Rows CTOP+1  to CTOP+18  text content  (18 visible lines)
 *   Row  CTOP+19 separator
 *   Rows CTOP+20 to CBOT     status bar + hints
 */
#define KATE_MAXBUF  4096
#define KATE_VROWS   18    /* visible text rows */
#define KATE_GUTTER   4    /* line-number gutter width */
#define KATE_VCOLS   (W - KATE_GUTTER)   /* usable text columns = 76 */

static char kate_buf[KATE_MAXBUF];
static int  kate_len;
static int  kate_pos;      /* cursor byte position */
static int  kate_vscroll;  /* first visible line number */
static int  kate_mod;      /* modified flag */
static char kate_fname[48];
static int  kate_node;     /* fs node  (-1 = unsaved new file) */
static int  kate_cwd_nd;   /* cwd fs node for save */

/* Line number of byte pos (count \n before it) */
static int kate_lineof(int pos) {
    int n = 0, i;
    for (i = 0; i < pos; i++) { if (kate_buf[i] == '\n') { n++; } }
    return n;
}
/* Column of byte pos within its line */
static int kate_colof(int pos) {
    int c = 0, i;
    for (i = 0; i < pos; i++) {
        if (kate_buf[i] == '\n') { c = 0; } else { c++; }
    }
    return c;
}
/* Byte offset of start of line ln */
static int kate_lnstart(int ln) {
    int cur = 0, i;
    if (ln <= 0) { return 0; }
    for (i = 0; i < kate_len; i++) {
        if (kate_buf[i] == '\n' && ++cur == ln) { return i + 1; }
    }
    return kate_len;
}
/* Byte offset of end of line ln (position of \n, or end-of-buf) */
static int kate_lnend(int ln) {
    int s = kate_lnstart(ln), i;
    for (i = s; i < kate_len; i++) { if (kate_buf[i] == '\n') { return i; } }
    return kate_len;
}

static void kate_draw(void) {
    int ln  = kate_lineof(kate_pos);
    int col = kate_colof(kate_pos);
    int byte, row;
    char lbuf[KATE_VCOLS + 1];
    char nb[16];

    /* Scroll adjustment */
    if (ln < kate_vscroll) { kate_vscroll = ln; }
    if (ln >= kate_vscroll + KATE_VROWS) { kate_vscroll = ln - KATE_VROWS + 1; }

    /* Tab bar */
    vga_fill_rect(0, CTOP, W, 1, ' ', WHT, DGR);
    vga_write_str_at(1, CTOP, " [", WHT, BLK);
    vga_write_str_at(3, CTOP, kate_fname, LCN, BLK);
    if (kate_mod) { vga_put_at(3 + slen(kate_fname), CTOP, '*', LRD, BLK); }
    vga_write_str_at(3 + slen(kate_fname) + (kate_mod ? 1 : 0), CTOP, "] ", WHT, BLK);
    vga_write_str_at(W - 16, CTOP, " Kate \xb3 FurOS  ", LGR, DGR);

    /* Clear content area */
    vga_fill_rect(0, CTOP + 1, W, KATE_VROWS, ' ', LGR, BLK);

    /* Render lines starting at kate_vscroll */
    byte = kate_lnstart(kate_vscroll);
    for (row = 0; row < KATE_VROWS; row++) {
        int y = CTOP + 1 + row;
        int lnnum = kate_vscroll + row + 1;
        int nc = 0, bi = byte;
        const char *p;

        /* Gutter: right-aligned line number */
        vga_fill_rect(0, y, KATE_GUTTER, 1, ' ', DGR, BLK);
        p = u2s((unsigned)lnnum, nb);
        {
            int pl = slen(p);
            int gx = KATE_GUTTER - 1 - pl;
            if (gx < 0) { gx = 0; }
            vga_write_str_at(gx, y, p, DGR, BLK);
        }

        /* Line text */
        kmemset(lbuf, ' ', KATE_VCOLS);
        lbuf[KATE_VCOLS] = '\0';
        while (bi < kate_len && kate_buf[bi] != '\n' && nc < KATE_VCOLS) {
            lbuf[nc++] = kate_buf[bi++];
        }
        while (bi < kate_len && kate_buf[bi] != '\n') { bi++; }
        if (bi < kate_len) { bi++; }
        byte = bi;
        vga_write_str_at(KATE_GUTTER, y, lbuf, LGR, BLK);
    }

    /* Separator */
    hline(0, CTOP + 1 + KATE_VROWS, W, DGR, BLK);

    /* Status line (CTOP+20 = row 21) */
    {
        char sb[40];
        char lnb[16], clb[16];
        int pi = 0;
        const char *ls = u2s((unsigned)(ln + 1), lnb);
        const char *cs = u2s((unsigned)(col + 1), clb);
        int i;
        sb[pi++] = ' '; sb[pi++] = 'L'; sb[pi++] = 'n'; sb[pi++] = ' ';
        for (i = 0; ls[i]; i++) { sb[pi++] = ls[i]; }
        sb[pi++] = ','; sb[pi++] = ' ';
        sb[pi++] = 'C'; sb[pi++] = 'o'; sb[pi++] = 'l'; sb[pi++] = ' ';
        for (i = 0; cs[i]; i++) { sb[pi++] = cs[i]; }
        sb[pi] = '\0';

        vga_fill_rect(0, CTOP + 1 + KATE_VROWS + 1, W, 1, ' ', CWS_FG, CWS_BG);
        vga_write_str_at(1, CTOP + 1 + KATE_VROWS + 1, sb, CWS_FG, CWS_BG);
        vga_write_str_at(W - 32, CTOP + 1 + KATE_VROWS + 1,
            " F2: Save | Del key: Del | ESC: Close ", CWS_FG, CWS_BG);
    }

    /* Hardware cursor */
    {
        int cx = KATE_GUTTER + col;
        int cy = CTOP + 1 + (ln - kate_vscroll);
        if (cx >= W) { cx = W - 1; }
        if (cy < CTOP + 1)              { cy = CTOP + 1; }
        if (cy > CTOP + KATE_VROWS)     { cy = CTOP + KATE_VROWS; }
        vga_set_cursor_pos(cx, cy);
    }
}

/* Shift buf[pos..] right by 1 to make room for insert */
static void kate_shiftright(int pos) {
    int i;
    for (i = kate_len; i > pos; i--) { kate_buf[i] = kate_buf[i - 1]; }
}

static void kate_insert(char c) {
    if (kate_len >= KATE_MAXBUF - 1) { return; }
    kate_shiftright(kate_pos);
    kate_buf[kate_pos] = c;
    kate_pos++;
    kate_len++;
    kate_mod = 1;
}
static void kate_backspace(void) {
    if (kate_pos == 0) { return; }
    kate_pos--;
    kmemcpy(&kate_buf[kate_pos], &kate_buf[kate_pos + 1],
            (size_t)(kate_len - kate_pos));
    kate_len--;
    kate_mod = 1;
}
static void kate_delkey(void) {
    if (kate_pos >= kate_len) { return; }
    kmemcpy(&kate_buf[kate_pos], &kate_buf[kate_pos + 1],
            (size_t)(kate_len - kate_pos - 1));
    kate_len--;
    kate_mod = 1;
}
static void kate_move_up(void) {
    int ln  = kate_lineof(kate_pos);
    int col = kate_colof(kate_pos);
    int ps, ep, len;
    if (ln == 0) { return; }
    ps  = kate_lnstart(ln - 1);
    ep  = kate_lnend(ln - 1);
    len = ep - ps;
    kate_pos = ps + (col < len ? col : len);
}
static void kate_move_down(void) {
    int ln   = kate_lineof(kate_pos);
    int col  = kate_colof(kate_pos);
    int ns   = kate_lnstart(ln + 1);
    int ne, nlen;
    if (ns >= kate_len && (kate_len == 0 || kate_buf[kate_len - 1] != '\n')) { return; }
    ne   = kate_lnend(ln + 1);
    nlen = ne - ns;
    if (nlen < 0) { nlen = 0; }
    kate_pos = ns + (col < nlen ? col : nlen);
}
static void kate_home(void) {
    kate_pos = kate_lnstart(kate_lineof(kate_pos));
}
static void kate_end(void) {
    kate_pos = kate_lnend(kate_lineof(kate_pos));
}

static int kate_save(void) {
    int r;
    if (kate_node < 0) {
        r = fs_touch(kate_cwd_nd, kate_fname);
        if (r < 0) { return -1; }
    }
    r = fs_write_file(kate_cwd_nd, kate_fname, kate_buf, (size_t)kate_len);
    if (r == 0) {
        kate_mod = 0;
        if (disk_available()) { (void)disk_save_fs(); }
        return 0;
    }
    return -1;
}

/* Show "unsaved changes" inline bar; returns 1=quit-anyway, 0=stay */
static int kate_unsaved_prompt(void) {
    vga_fill_rect(0, CBOT - 1, W, 1, ' ', WHT, RED);
    vga_write_str_at(1, CBOT - 1,
        " Unsaved changes!  F2=Save & Quit   D=Discard & Quit   ESC=Cancel", WHT, RED);
    for (;;) {
        int k = keyboard_get_key();
        if (k == KEY_F2) { kate_save(); return 1; }
        if (k == 'd' || k == 'D') { return 1; }
        if (k == 0x1B) { return 0; }
    }
}

/* Open Kate; fnode=-1 for a new blank file, cwd_nd = cwd for saving */
static void run_kate(int fnode, int cwd_nd) {
    const char *fdata = 0;
    size_t fsz = 0;

    kate_len     = 0;
    kate_pos     = 0;
    kate_vscroll = 0;
    kate_mod     = 0;
    kate_cwd_nd  = cwd_nd;
    kate_node    = fnode;

    if (fnode >= 0) {
        char fpath[80];
        kstrncpy(kate_fname, fs_get_name(fnode), 47);
        kate_fname[47] = '\0';
        fs_make_path(fnode, fpath, sizeof(fpath));
        if (fs_read_file(fs_get_root(), fpath, &fdata, &fsz) == 0 && fdata) {
            if (fsz > (size_t)(KATE_MAXBUF - 1)) { fsz = (size_t)(KATE_MAXBUF - 1); }
            kmemcpy(kate_buf, fdata, fsz);
            kate_len = (int)fsz;
        }
    } else {
        kstrcpy(kate_fname, "untitled.txt");
    }
    kate_buf[kate_len] = '\0';

    open_window("Kate", kate_fname);
    draw_panel();
    vga_fill_rect(0, CTOP, W, CBOT - CTOP + 1, ' ', LGR, BLK);

    for (;;) {
        kate_draw();

        int k = keyboard_get_key();

        if (k == 0x1B) {
            if (!kate_mod || kate_unsaved_prompt()) { break; }
            open_window("Kate", kate_fname); draw_panel();
            continue;
        }
        if (k == KEY_F2)    { kate_save();       continue; }
        if (k == KEY_UP)    { kate_move_up();    continue; }
        if (k == KEY_DOWN)  { kate_move_down();  continue; }
        if (k == KEY_LEFT)  { if (kate_pos > 0)  { kate_pos--; }  continue; }
        if (k == KEY_RIGHT) { if (kate_pos < kate_len) { kate_pos++; } continue; }
        if (k == KEY_F1)    { kate_home();       continue; }  /* repurpose F1=Home */
        if (k == KEY_F3)    { kate_end();        continue; }  /* F3=End */
        if (k == '\b' || k == 127) { kate_backspace(); continue; }
        if (k == '\x7f' || k == KEY_F4) { kate_delkey(); continue; } /* Del */
        if (k == '\n' || k == '\r') { kate_insert('\n'); continue; }
        if (k >= 32 && k < 127)    { kate_insert((char)k); continue; }
    }
}

/* ── System Settings ─────────────────────────────────────────────────────── */
/*
 * Three tabs: [ System ] [ Users ] [ Display ]
 * Tab bar at row CTOP, separator at CTOP+1, content CTOP+2..CBOT-1
 * Status/save hint at CBOT.
 */
#define SET_NPAGES 3
static const char *set_pnames[SET_NPAGES] = { " System ", " Users  ", " Display" };

static int  set_page;     /* 0-2 */
static int  set_field;    /* selected field index within current page */
static int  set_editing;  /* 1 = typing into a field */
static char set_input[64];
static int  set_ilen;
static char set_hostname[32];
static char set_timezone[32];
static char set_username[32];
static int  set_changed;

static void set_load(void) {
    const char *d = 0;
    size_t sz     = 0;
    int root      = fs_get_root();
    int etc       = fs_resolve(root, "etc");

    kstrcpy(set_hostname, "furos");
    kstrcpy(set_timezone, "UTC");
    kstrcpy(set_username, "user");

    if (etc >= 0) {
        if (fs_read_file(etc, "hostname", &d, &sz) == 0 && d && sz > 0) {
            int i;
            kstrncpy(set_hostname, d, 31);
            set_hostname[31] = '\0';
            for (i = 0; set_hostname[i]; i++) {
                if (set_hostname[i] == '\n' || set_hostname[i] == '\r') {
                    set_hostname[i] = '\0'; break;
                }
            }
        }
        if (fs_read_file(etc, "timezone", &d, &sz) == 0 && d && sz > 0) {
            int i;
            kstrncpy(set_timezone, d, 31);
            set_timezone[31] = '\0';
            for (i = 0; set_timezone[i]; i++) {
                if (set_timezone[i] == '\n' || set_timezone[i] == '\r') {
                    set_timezone[i] = '\0'; break;
                }
            }
        }
    }
}
static void set_save(void) {
    int root = fs_get_root();
    int etc  = fs_resolve(root, "etc");
    if (etc < 0) {
        fs_mkdir(root, "etc");
        etc = fs_resolve(root, "etc");
    }
    if (etc >= 0) {
        fs_write_file(etc, "hostname", set_hostname, (size_t)slen(set_hostname));
        fs_write_file(etc, "timezone", set_timezone, (size_t)slen(set_timezone));
    }
    if (disk_available()) { (void)disk_save_fs(); }
    set_changed = 0;
}

static void set_draw(void) {
    int i;
    char fbuf[48];

    /* Tab bar */
    vga_fill_rect(0, CTOP, W, 1, ' ', LGR, DGR);
    {
        int tx = 1;
        for (i = 0; i < SET_NPAGES; i++) {
            int act = (i == set_page);
            uint8_t fg = act ? WHT   : LGR;
            uint8_t bg = act ? MAG   : DGR;
            int     l  = slen(set_pnames[i]);
            vga_write_str_at(tx, CTOP, set_pnames[i], fg, bg);
            tx += l;
            vga_put_at(tx, CTOP, CV_, LGR, DGR);
            tx++;
        }
    }

    /* Separator */
    hline(0, CTOP + 1, W, DGR, BLK);

    /* Content area */
    vga_fill_rect(0, CTOP + 2, W, CBOT - CTOP - 2, ' ', LGR, BLK);

    if (set_page == 0) {
        /* ── System page ── */
        titbox(1, CTOP + 2, W - 2, 7, " System ", LGR, BLK);
        vga_write_str_at(3, CTOP + 3, "Hostname :", LGR, BLK);
        {
            int fsel = (set_field == 0);
            uint8_t bg = (fsel && set_editing) ? BLU : (fsel ? DGR : BLK);
            spad(fbuf, set_editing && set_field == 0 ? set_input : set_hostname, 32);
            vga_write_str_at(15, CTOP + 3, fbuf, WHT, bg);
            if (fsel && !set_editing) {
                vga_write_str_at(48, CTOP + 3, " [Enter: edit]", DGR, BLK);
            }
        }
        vga_write_str_at(3, CTOP + 4, "Timezone :", LGR, BLK);
        {
            int fsel = (set_field == 1);
            uint8_t bg = (fsel && set_editing) ? BLU : (fsel ? DGR : BLK);
            spad(fbuf, set_editing && set_field == 1 ? set_input : set_timezone, 32);
            vga_write_str_at(15, CTOP + 4, fbuf, WHT, bg);
            if (fsel && !set_editing) {
                vga_write_str_at(48, CTOP + 4, " [Enter: edit]", DGR, BLK);
            }
        }
        vga_write_str_at(3, CTOP + 5, "OS       :", LGR, BLK);
        vga_write_str_at(15, CTOP + 5, "FurOS 1.0 Plasma Edition", LCN, BLK);
        vga_write_str_at(3, CTOP + 6, "Kernel   :", LGR, BLK);
        vga_write_str_at(15, CTOP + 6, "FurOS 1.0 (x86_64 bare-metal)", LCN, BLK);
        vga_write_str_at(3, CTOP + 7, "Arch     :", LGR, BLK);
        vga_write_str_at(15, CTOP + 7, "x86_64", LCN, BLK);

    } else if (set_page == 1) {
        /* ── Users page ── */
        titbox(1, CTOP + 2, W - 2, 7, " Users ", LGR, BLK);
        vga_write_str_at(3, CTOP + 3, "Username :", LGR, BLK);
        vga_write_str_at(15, CTOP + 3, set_username, LCN, BLK);
        vga_write_str_at(3, CTOP + 4, "Home Dir :", LGR, BLK);
        {
            char hpath[48];
            kstrcpy(hpath, "/home/");
            kstrncpy(hpath + 6, set_username, 41);
            vga_write_str_at(15, CTOP + 4, hpath, LCN, BLK);
        }
        vga_write_str_at(3, CTOP + 5, "Shell    :", LGR, BLK);
        vga_write_str_at(15, CTOP + 5, "/bin/fursh", LCN, BLK);
        vga_write_str_at(3, CTOP + 6, "UID      :", LGR, BLK);
        vga_write_str_at(15, CTOP + 6, "1000", LCN, BLK);

    } else {
        /* ── Display page ── */
        titbox(1, CTOP + 2, W - 2, 9, " Display ", LGR, BLK);
        vga_write_str_at(3, CTOP + 3, "Theme    :", LGR, BLK);
        vga_write_str_at(15, CTOP + 3, "Breeze Dark (FurOS Edition)", LCN, BLK);
        vga_write_str_at(3, CTOP + 4, "Font     :", LGR, BLK);
        vga_write_str_at(15, CTOP + 4, "VGA Text Mode (fixed 8x16)", LCN, BLK);
        vga_write_str_at(3, CTOP + 5, "Res      :", LGR, BLK);
        vga_write_str_at(15, CTOP + 5, "80\xd7" "25 characters, 16 colors", LCN, BLK);
        vga_write_str_at(3, CTOP + 6, "Renderer :", LGR, BLK);
        vga_write_str_at(15, CTOP + 6, "VGA direct framebuffer (0xB8000)", LCN, BLK);
        vga_write_str_at(3, CTOP + 7, "DE       :", LGR, BLK);
        vga_write_str_at(15, CTOP + 7, "KDE Plasma (FurOS Edition, text mode)", LCN, BLK);
        vga_write_str_at(3, CTOP + 8, "WM       :", LGR, BLK);
        vga_write_str_at(15, CTOP + 8, "FurWM 1.0 (single-window)", LCN, BLK);
    }

    /* Status bar */
    vga_fill_rect(0, CBOT, W, 1, ' ', CWS_FG, CWS_BG);
    if (set_changed) {
        vga_write_str_at(1, CBOT, " * Unsaved changes", LRD, CWS_BG);
    }
    vga_write_str_at(W - 40, CBOT,
        " \x1b\x1a: Tab   \x18\x19: Field   F2: Save   ESC: Close", CWS_FG, CWS_BG);
}

static void run_settings(void) {
    set_page    = 0;
    set_field   = 0;
    set_editing = 0;
    set_changed = 0;
    set_load();

    open_window("System Settings", "Configure FurOS");
    draw_panel();

    for (;;) {
        set_draw();
        set_statusbar(" System Settings");

        int k = keyboard_get_key();

        if (k == 0x1B) {
            if (set_editing) { set_editing = 0; continue; }
            break;
        }
        if (k == KEY_F2) { set_save(); set_changed = 0; continue; }

        if (!set_editing) {
            if (k == KEY_LEFT  && set_page > 0) { set_page--;  set_field = 0; continue; }
            if (k == KEY_RIGHT && set_page < SET_NPAGES - 1) { set_page++; set_field = 0; continue; }
            if (k == KEY_UP    && set_field > 0) { set_field--; continue; }
            if (k == KEY_DOWN)   { set_field++; continue; }
            if ((k == '\n' || k == '\r') && set_page == 0) {
                /* Start editing */
                set_editing = 1;
                if (set_field == 0) { kstrncpy(set_input, set_hostname, 32); }
                else if (set_field == 1) { kstrncpy(set_input, set_timezone, 32); }
                set_ilen = slen(set_input);
                continue;
            }
        } else {
            /* Editing mode */
            if (k == '\n' || k == '\r') {
                set_input[set_ilen] = '\0';
                if (set_field == 0) { kstrncpy(set_hostname, set_input, 31); set_hostname[31] = '\0'; }
                else if (set_field == 1) { kstrncpy(set_timezone, set_input, 31); set_timezone[31] = '\0'; }
                set_editing = 0;
                set_changed = 1;
                continue;
            }
            if ((k == '\b' || k == 127) && set_ilen > 0) {
                set_ilen--;
                set_input[set_ilen] = '\0';
                continue;
            }
            if (k >= 32 && k < 127 && set_ilen < 31) {
                set_input[set_ilen++] = (char)k;
                set_input[set_ilen]   = '\0';
                continue;
            }
        }
    }
}

/* ── Desktop context menu ────────────────────────────────────────────────── */
/* Returns: 0=terminal, 1=files, 2=kate, 3=settings, 4=refresh, -1=cancel */
#define CTX_X   25
#define CTX_Y   10
#define CTX_W   30
#define CTX_N    5
static const char *ctx_items[CTX_N] = {
    "  \x10 Open Terminal       ",
    "  \x10 Open Files          ",
    "  \x10 Open Text Editor    ",
    "  \x10 System Settings     ",
    "  \x10 Refresh Desktop     ",
};
static int run_context_menu(void) {
    int sel = 0;
    for (;;) {
        int i;
        char ibuf[CTX_W - 1];
        bbox(CTX_X, CTX_Y, CTX_W, CTX_N + 4, LGR, BLK);
        vga_fill_rect(CTX_X + 1, CTX_Y, CTX_W - 2, 1, ' ', WHT, MAG);
        vga_write_str_at(CTX_X + 2, CTX_Y, " \x10 Desktop Menu", WHT, MAG);
        hline(CTX_X + 1, CTX_Y + CTX_N + 2, CTX_W - 2, DGR, BLK);
        vga_write_str_at(CTX_X + 2, CTX_Y + CTX_N + 3, "ESC: Cancel", DGR, BLK);
        for (i = 0; i < CTX_N; i++) {
            int act = (i == sel);
            uint8_t fg = act ? CKS_FG : CKP_FG;
            uint8_t bg = act ? CKS_BG : CKP_BG;
            spad(ibuf, ctx_items[i], CTX_W - 2);
            vga_write_str_at(CTX_X + 1, CTX_Y + 1 + i, ibuf, fg, bg);
        }
        int k = keyboard_get_key();
        if (k == KEY_UP   && sel > 0)      { sel--; continue; }
        if (k == KEY_DOWN && sel < CTX_N-1){ sel++; continue; }
        if (k == '\n' || k == '\r')        { return sel; }
        if (k == 0x1B)                     { return -1; }
    }
}

/* ── Run dialog ──────────────────────────────────────────────────────────── */
/* Returns app index (same as kickoff) or -1 */
static int run_dialog(void) {
    char input[24];
    int ilen = 0;
    static const char *run_names[] = {
        "konsole", "terminal", "dolphin", "files",
        "kate", "editor", "settings", "sysguard",
        "monitor", "about", "shutdown", 0
    };
    static const int run_ids[] = {
        0, 0, 1, 1,
        3, 3, 4, 2,
        2, 5, 6
    };
    input[0] = '\0';
    for (;;) {
        int i;
        char ibuf[23];
        bbox(20, 11, 40, 5, LGR, BLK);
        vga_fill_rect(21, 11, 38, 1, ' ', WHT, MAG);
        vga_write_str_at(22, 11, " \x10 Run Application", WHT, MAG);
        vga_fill_rect(21, 12, 38, 1, ' ', LGR, BLK);
        vga_write_str_at(22, 12, "Run: ", LGR, BLK);
        spad(ibuf, input, 22);
        vga_write_str_at(27, 12, ibuf, WHT, BLU);
        vga_fill_rect(21, 13, 38, 1, ' ', DGR, BLK);
        vga_write_str_at(22, 13,
            "konsole dolphin kate settings", DGR, BLK);
        vga_write_str_at(22, 14, "ENTER: Run   ESC: Cancel", DGR, BLK);

        int k = keyboard_get_key();
        if (k == 0x1B) { return -1; }
        if (k == '\n' || k == '\r') {
            for (i = 0; run_names[i]; i++) {
                if (kstrcmp(input, run_names[i]) == 0) { return run_ids[i]; }
            }
            /* Not found */
            vga_write_str_at(22, 14, "Unknown app. Try again. ", LRD, BLK);
            (void)keyboard_get_key();
            continue;
        }
        if ((k == '\b' || k == 127) && ilen > 0) {
            input[--ilen] = '\0'; continue;
        }
        if (k >= 32 && k < 127 && ilen < 22) {
            input[ilen++] = (char)k;
            input[ilen]   = '\0';
            continue;
        }
    }
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

/* Launch by kickoff index (centralised so kickoff + shortcuts use same table) */
static void launch(int id) {
    switch (id) {
    case 0: run_terminal(); break;
    case 1: run_fileman();  break;
    case 2: run_sysmon();   break;
    case 3: run_kate(-1, fs_get_root()); break;
    case 4: run_settings(); break;
    case 5: run_about();    break;
    case 6:
        if (run_shutdown()) {
            vga_set_color(LGR, BLK);
            vga_clear();
            scenter(12, "FurOS has shut down.  It is now safe to power off.", LGR, BLK);
            for (;;) { __asm__ volatile("hlt"); }
        }
        break;
    default: break;
    }
}

/* ── Main desktop event loop ─────────────────────────────────────────────── */
__attribute__((noreturn))
void desktop_run(void) {
    for (;;) {
        draw_desktop();

        for (;;) {
            int k = keyboard_get_key();

            if (k == KEY_F5) { break; }  /* refresh */

            /* Virtual desktop pager: F1-F4 */
            if (k >= KEY_F1 && k <= KEY_F4) {
                g_ws = k - KEY_F1;
                draw_panel();
                continue;
            }

            /* Kickoff launcher */
            if (k == 'k' || k == 'K') {
                int c = run_kickoff();
                if (c >= 0) { launch(c); break; }
                break; /* ESC */
            }

            /* Context menu */
            if (k == 'c' || k == 'C') {
                int c = run_context_menu();
                if (c == 0) { launch(0); break; }
                if (c == 1) { launch(1); break; }
                if (c == 2) { launch(3); break; } /* Kate */
                if (c == 3) { launch(4); break; } /* Settings */
                break; /* refresh */
            }

            /* Run dialog */
            if (k == 'r' || k == 'R') {
                int c = run_dialog();
                if (c >= 0) { launch(c); break; }
                break;
            }

            /* Direct shortcuts:
             *  T=terminal  F=files  M=monitor  E=editor  G=settings
             *  A=about     Q=shutdown  1-3=apps */
            if (k == 't' || k == 'T') { launch(0); break; }
            if (k == 'f' || k == 'F') { launch(1); break; }
            if (k == 'm' || k == 'M') { launch(2); break; }
            if (k == 'e' || k == 'E') { launch(3); break; }
            if (k == 'g' || k == 'G') { launch(4); break; }
            if (k == 'a' || k == 'A') { launch(5); break; }
            if (k == 'q' || k == 'Q') { launch(6); break; }

            if (k == '1') { launch(0); break; }
            if (k == '2') { launch(1); break; }
            if (k == '3') { launch(2); break; }
        }
    }
}
