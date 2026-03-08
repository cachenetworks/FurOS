/*
 * FurOS Installer — Arch-style TUI wizard
 * Runs when booted via GRUB/Multiboot2 (installer ISO mode).
 */
#include "installer.h"

#include "ata.h"
#include "disk.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "vga.h"

#include <stddef.h>
#include <stdint.h>

/* ── Screen layout ───────────────────────────────────────────────────────── */
#define SCR_W          80
#define HDR_ROW         0   /* title bar */
#define CONT_TOP        2   /* content area top */
#define CONT_BOT       20   /* content area bottom */
#define SEP_ROW        21
#define HINT_ROW       22
#define STAT_ROW       24

/* ── Colour theme (FurOS Plasma purple) ──────────────────────────────────── */
#define CH_FG  VGA_COLOR_WHITE
#define CH_BG  VGA_COLOR_MAGENTA
#define CC_FG  VGA_COLOR_LIGHT_GREY
#define CC_BG  VGA_COLOR_BLACK
#define CB_FG  VGA_COLOR_WHITE
#define CB_BG  VGA_COLOR_DARK_GREY
#define CS_FG  VGA_COLOR_WHITE
#define CS_BG  VGA_COLOR_LIGHT_MAGENTA
#define CI_FG  VGA_COLOR_WHITE
#define CI_BG  VGA_COLOR_DARK_GREY
#define CW_FG  VGA_COLOR_LIGHT_RED
#define CG_FG  VGA_COLOR_LIGHT_GREEN
#define CX_FG  VGA_COLOR_LIGHT_CYAN

/* ── Wizard state ────────────────────────────────────────────────────────── */
struct wiz {
    int  disk_index;
    char hostname[32];
    char timezone[32];
    char root_pw[64];
    char username[32];
    char user_pw[64];
};

/* ── Utility ─────────────────────────────────────────────────────────────── */

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) { __asm__ volatile("hlt"); }
}

static int slen(const char *s) {
    int n = 0;
    while (s[n]) { n++; }
    return n;
}

static int is_print(char c) { return (unsigned char)c >= 32 && (unsigned char)c < 127; }


static void u32_to_str(uint32_t v, char *buf, int bufsz) {
    char tmp[12];
    int ti = 0, p = 0;
    if (v == 0) { buf[p++] = '0'; }
    while (v > 0 && ti < 11) { tmp[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0 && p < bufsz - 1) { buf[p++] = tmp[--ti]; }
    buf[p] = '\0';
}

static void hex16_to_str(uint16_t v, char *buf) {
    static const char h[] = "0123456789ABCDEF";
    buf[0]='0'; buf[1]='x';
    buf[2]=h[(v>>12)&0xF]; buf[3]=h[(v>>8)&0xF];
    buf[4]=h[(v>>4)&0xF];  buf[5]=h[v&0xF];
    buf[6]='\0';
}

static void hex32_to_str(uint32_t v, char *buf) {
    static const char h[] = "0123456789ABCDEF";
    int i;
    buf[0]='0'; buf[1]='x';
    for (i = 0; i < 8; i++) {
        buf[2+i] = h[(v >> (28 - i*4)) & 0xF];
    }
    buf[10] = '\0';
}

static void str_cat(char *dst, const char *src, int dstsz) {
    int dlen = slen(dst);
    int i = 0;
    while (src[i] && dlen + i < dstsz - 1) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
}

/* ── Drawing helpers ─────────────────────────────────────────────────────── */

/* CP437 single-line box chars */
#define CH_TL  '\xDA'   /* ┌ */
#define CH_TR  '\xBF'   /* ┐ */
#define CH_BL  '\xC0'   /* └ */
#define CH_BR  '\xD9'   /* ┘ */
#define CH_H   '\xC4'   /* ─ */
#define CH_V   '\xB3'   /* │ */
#define CH_BLK '\xDB'   /* █ */
#define CH_SHD '\xB2'   /* ▓ */
#define CH_TRI '\x10'   /* ► */

static void draw_header(int step, int total, const char *name) {
    char right[48];
    int rlen, i;

    vga_fill_rect(0, HDR_ROW, SCR_W, 1, ' ', CH_FG, CH_BG);
    vga_fill_rect(0, HDR_ROW + 1, SCR_W, 1, ' ', CB_FG, CB_BG);

    vga_write_str_at(2, HDR_ROW, " FurOS 1.0 Installer ", CH_FG, CH_BG);

    right[0] = '\0';
    str_cat(right, "Step ", sizeof(right));
    {
        char n[4]; u32_to_str((uint32_t)step, n, sizeof(n));
        str_cat(right, n, sizeof(right));
    }
    str_cat(right, "/", sizeof(right));
    {
        char n[4]; u32_to_str((uint32_t)total, n, sizeof(n));
        str_cat(right, n, sizeof(right));
    }
    str_cat(right, ": ", sizeof(right));
    str_cat(right, name, sizeof(right));
    rlen = slen(right);
    i = SCR_W - rlen - 2;
    if (i < 25) { i = 25; }
    vga_write_str_at(i, HDR_ROW, right, CH_FG, CH_BG);
}

static void clear_content(void) {
    int h = CONT_BOT - CONT_TOP + 1;
    vga_fill_rect(0, CONT_TOP, SCR_W, h, ' ', CC_FG, CC_BG);
}

static void draw_bottom(const char *hint) {
    vga_fill_rect(0, SEP_ROW,  SCR_W, 1, CH_H,  CB_FG, CB_BG);
    vga_fill_rect(0, HINT_ROW, SCR_W, 1, ' ',   CB_FG, CB_BG);
    vga_fill_rect(0, STAT_ROW, SCR_W, 1, ' ',   CB_FG, CB_BG);
    if (hint) {
        vga_write_str_at(2, HINT_ROW, hint, CB_FG, CB_BG);
    }
    vga_write_str_at(2, STAT_ROW,
        "FurOS Installer 1.0  \xB3  Based on Debian GNU/Linux 12 Bookworm",
        CB_FG, CB_BG);
}

/* Box top: ┌───────────────┐ at (x, y) width w */
static void box_top(int x, int y, int w) {
    int i;
    vga_put_at(x, y, CH_TL, CB_FG, CB_BG);
    for (i = 1; i < w - 1; i++) { vga_put_at(x+i, y, CH_H, CB_FG, CB_BG); }
    vga_put_at(x+w-1, y, CH_TR, CB_FG, CB_BG);
}

/* Box content row: │ <content padded> │ */
static void box_row(int x, int y, int w, const char *txt,
                    uint8_t fg, uint8_t bg) {
    int inner = w - 2, i, tlen;
    vga_put_at(x, y, CH_V, CB_FG, CB_BG);
    vga_fill_rect(x+1, y, inner, 1, ' ', fg, bg);
    if (txt) {
        tlen = slen(txt);
        if (tlen > inner) { tlen = inner; }
        for (i = 0; i < tlen; i++) {
            vga_put_at(x+1+i, y, (unsigned char)txt[i], fg, bg);
        }
    }
    vga_put_at(x+w-1, y, CH_V, CB_FG, CB_BG);
}

/* Box bottom: └───────────────┘ */
static void box_bot(int x, int y, int w) {
    int i;
    vga_put_at(x, y, CH_BL, CB_FG, CB_BG);
    for (i = 1; i < w - 1; i++) { vga_put_at(x+i, y, CH_H, CB_FG, CB_BG); }
    vga_put_at(x+w-1, y, CH_BR, CB_FG, CB_BG);
}

/* Progress bar at row y, 0-100 */
static void draw_progress(int y, int pct) {
    int bar_w = SCR_W - 18;
    int filled = (pct * bar_w) / 100;
    int i;
    char pstr[8];

    vga_write_str_at(2, y, "Progress [", CC_FG, CC_BG);
    for (i = 0; i < bar_w; i++) {
        if (i < filled) {
            vga_put_at(12+i, y, CH_BLK, CG_FG, CC_BG);
        } else {
            vga_put_at(12+i, y, CH_SHD, CB_FG, CC_BG);
        }
    }
    vga_put_at(12+bar_w, y, ']', CC_FG, CC_BG);
    u32_to_str((uint32_t)pct, pstr, sizeof(pstr));
    str_cat(pstr, "%", sizeof(pstr));
    vga_write_str_at(13+bar_w+1, y, pstr, CG_FG, CC_BG);
}

/* ── Menu (arrow-key driven, returns item index or -1 = ESC/cancel) ──────── */
static int run_menu(int x, int y_top, int w,
                    const char **items, int count, int init_sel) {
    int sel = init_sel;
    int key, i, iy;
    char buf[SCR_W + 1];

    if (sel < 0) { sel = 0; }
    if (sel >= count) { sel = count - 1; }

    for (;;) {
        box_top(x, y_top, w);
        for (i = 0; i < count; i++) {
            iy = y_top + 1 + i;
            if (i == sel) {
                int p = 0;
                buf[p++] = ' '; buf[p++] = CH_TRI; buf[p++] = ' ';
                int j = 0;
                while (items[i][j] && p < w - 2) { buf[p++] = items[i][j++]; }
                buf[p] = '\0';
                box_row(x, iy, w, buf, CS_FG, CS_BG);
            } else {
                int p = 0;
                buf[p++] = ' '; buf[p++] = ' '; buf[p++] = ' ';
                int j = 0;
                while (items[i][j] && p < w - 2) { buf[p++] = items[i][j++]; }
                buf[p] = '\0';
                box_row(x, iy, w, buf, CB_FG, CB_BG);
            }
        }
        box_bot(x, y_top + count + 1, w);
        vga_set_cursor_pos(x + 3, y_top + 1 + sel);

        key = keyboard_get_key();
        if (key == KEY_UP   && sel > 0)          { sel--; }
        else if (key == KEY_DOWN && sel < count-1) { sel++; }
        else if (key == '\n' || key == '\r')        { return sel; }
        else if (key == 27 || key == 'q' || key == 'Q') { return -1; }
    }
}

/* ── Text input (masked optional, returns 0=OK, -1=ESC) ─────────────────── */
static int run_input(int x, int y, int w,
                     char *buf, int max_len, int masked) {
    int len = slen(buf);
    int key, i;

    for (;;) {
        int inner = w - 2;
        box_top(x, y-1, w);
        /* Content row */
        vga_put_at(x, y, CH_V, CB_FG, CB_BG);
        vga_fill_rect(x+1, y, inner, 1, ' ', CI_FG, CI_BG);
        for (i = 0; i < inner; i++) {
            if (i < len) {
                unsigned char disp = masked ? '*' : (unsigned char)buf[i];
                vga_put_at(x+1+i, y, disp, CI_FG, CI_BG);
            } else if (i == len) {
                vga_put_at(x+1+i, y, '_', CI_FG, CI_BG);
            }
        }
        vga_put_at(x+w-1, y, CH_V, CB_FG, CB_BG);
        box_bot(x, y+1, w);
        vga_set_cursor_pos(x+1+len, y);

        key = keyboard_get_key();
        if (key == '\n' || key == '\r') { buf[len] = '\0'; return 0; }
        if (key == 27) { return -1; }
        if (key == '\b' && len > 0) { len--; buf[len] = '\0'; }
        else if (is_print((char)key) && len < max_len - 1) {
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
    }
}

/* ── Stage 0: Welcome ────────────────────────────────────────────────────── */
static int stage_welcome(void) {
    int n = disk_count();
    char nbuf[8];

    draw_header(1, 7, "Welcome");
    clear_content();

    /* ASCII logo (centred at col 22) */
    vga_write_str_at(22, 4, "  ______           ____  ____", CX_FG, CC_BG);
    vga_write_str_at(22, 5, " |  ____|         / __ \\/ ___|", CX_FG, CC_BG);
    vga_write_str_at(22, 6, " | |__ _   _ _ __| |  | \\___ \\", CX_FG, CC_BG);
    vga_write_str_at(22, 7, " |  __| | | | '__| |  | |___) |", CX_FG, CC_BG);
    vga_write_str_at(22, 8, " | |  | |_| | |  | |__| |____/", CX_FG, CC_BG);
    vga_write_str_at(22, 9, " |_|   \\__,_|_|   \\____/", CX_FG, CC_BG);

    vga_write_str_at(2, 11,
        "Welcome to the FurOS 1.0 Installation Wizard", CC_FG, CC_BG);
    vga_write_str_at(2, 12,
        "Based on Debian GNU/Linux 12 (Bookworm)", CB_FG, CC_BG);
    vga_write_str_at(2, 14,
        "This wizard will install FurOS to a disk and set up your system.", CB_FG, CC_BG);
    vga_write_str_at(2, 15,
        "All data on the chosen disk will be permanently erased.", CW_FG, CC_BG);

    vga_write_str_at(2, 17, "Disk interface support: IDE (legacy) and AHCI SATA", CB_FG, CC_BG);
    vga_write_str_at(2, 18, "Disks detected: ", CC_FG, CC_BG);
    u32_to_str((uint32_t)n, nbuf, sizeof(nbuf));
    vga_write_str_at(18, 18, nbuf, n > 0 ? CG_FG : CW_FG, CC_BG);

    if (n == 0) {
        vga_write_str_at(2, 19,
            "No disks found! Cannot install. Check disk connection.", CW_FG, CC_BG);
    }

    draw_bottom(n > 0
        ? "  ENTER: Start Wizard    Q/ESC: Quit Installer"
        : "  No disks detected. Q/ESC: Quit");

    for (;;) {
        int k = keyboard_get_key();
        if ((k == '\n' || k == '\r') && n > 0) { return 1; }
        if (k == 'q' || k == 'Q' || k == 27)  { return -1; }
    }
}

/* ── Stage 1: Disk selection ─────────────────────────────────────────────── */
static int stage_disk(struct wiz *w) {
    int cnt = disk_count();
    int i, sel;
    const char *items[16];
    char disk_lines[16][64];
    struct disk_info di;

    if (cnt <= 0 || cnt > 16) { return -1; }

    for (i = 0; i < cnt; i++) {
        char *ln = disk_lines[i];
        char sbuf[12], hbuf[8];
        int p = 0;

        if (disk_get_info(i, &di) != 0 || !di.present) {
            ln[0] = '?'; ln[1] = '\0';
            items[i] = ln;
            continue;
        }
        /* "[0]  IDE Master  io=0x1F0  32768 sectors  QEMU HARDDISK" */
        ln[p++] = '[';
        u32_to_str((uint32_t)i, sbuf, sizeof(sbuf));
        { int j = 0; while (sbuf[j] && p < 62) { ln[p++] = sbuf[j++]; } }
        ln[p++] = ']'; ln[p++] = ' '; ln[p++] = ' ';
        if (di.interface_type == ATA_IFACE_AHCI) {
            const char *a = "AHCI  "; int j = 0;
            while (a[j] && p < 62) { ln[p++] = a[j++]; }
        } else {
            const char *ide = di.slave ? "IDE Slave  io=" : "IDE Master io=";
            int j = 0;
            while (ide[j] && p < 62) { ln[p++] = ide[j++]; }
            hex16_to_str(di.io_base, hbuf);
            j = 0; while (hbuf[j] && p < 62) { ln[p++] = hbuf[j++]; }
            ln[p++] = ' '; ln[p++] = ' ';
        }
        u32_to_str(di.sectors28, sbuf, sizeof(sbuf));
        { int j = 0; while (sbuf[j] && p < 62) { ln[p++] = sbuf[j++]; } }
        { const char *s = " sectors  "; int j = 0; while (s[j] && p < 62) { ln[p++] = s[j++]; } }
        if (di.model[0]) {
            int j = 0;
            while (di.model[j] && p < 62) { ln[p++] = di.model[j++]; }
        }
        ln[p] = '\0';
        items[i] = ln;
    }

    draw_header(2, 7, "Select Disk");
    clear_content();
    vga_write_str_at(2, CONT_TOP, "Select the disk to install FurOS on:", CC_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+1,
        "\xfe WARNING: All data on the chosen disk will be erased!", CW_FG, CC_BG);
    draw_bottom("  \x18\x19: Move   ENTER: Select   ESC/Q: Back");

    sel = (w->disk_index >= 0 && w->disk_index < cnt) ? w->disk_index : 0;
    sel = run_menu(2, CONT_TOP+3, SCR_W-4, items, cnt, sel);
    if (sel < 0) { return 0; } /* back */

    if (disk_select(sel) != 0) {
        vga_write_str_at(2, CONT_TOP+3+cnt+3,
            "Cannot select disk! Press any key.", CW_FG, CC_BG);
        keyboard_get_key();
        return 0;
    }
    w->disk_index = sel;
    return 1;
}

/* ── Stage 2: Hostname ───────────────────────────────────────────────────── */
static int stage_hostname(struct wiz *w) {
    int rc;

    draw_header(3, 7, "Hostname");
    clear_content();
    vga_write_str_at(2, CONT_TOP,   "Enter the hostname for this computer:", CC_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+1, "Use lowercase letters, digits, and hyphens only.", CB_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+2, "Examples: furos-pc  my-laptop  workstation", CB_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+4, "Hostname:", CC_FG, CC_BG);
    draw_bottom("  ENTER: Confirm   ESC/Q: Back");

    if (w->hostname[0] == '\0') { kstrcpy(w->hostname, "furos"); }

    rc = run_input(2, CONT_TOP+6, 40, w->hostname, sizeof(w->hostname), 0);
    if (rc < 0) {
        /* ESC pressed: treat as Back but keep whatever was typed */
        return 0;
    }
    if (w->hostname[0] == '\0') { kstrcpy(w->hostname, "furos"); }
    return 1;
}

/* ── Stage 3: Timezone ───────────────────────────────────────────────────── */
static int stage_timezone(struct wiz *w) {
    static const char *tzones[] = {
        "UTC",
        "America/New_York",
        "America/Chicago",
        "America/Denver",
        "America/Los_Angeles",
        "America/Sao_Paulo",
        "Europe/London",
        "Europe/Paris",
        "Europe/Berlin",
        "Europe/Moscow",
        "Asia/Dubai",
        "Asia/Kolkata",
        "Asia/Tokyo",
        "Asia/Shanghai",
        "Australia/Sydney"
    };
    static const int TZ_COUNT = 15;
    int init = 0, sel, i;

    /* Find current selection */
    for (i = 0; i < TZ_COUNT; i++) {
        if (kstrcmp(w->timezone, tzones[i]) == 0) { init = i; break; }
    }

    draw_header(4, 7, "Timezone");
    clear_content();
    vga_write_str_at(2, CONT_TOP, "Select your timezone:", CC_FG, CC_BG);
    draw_bottom("  \x18\x19: Move   ENTER: Select   ESC/Q: Back");

    sel = run_menu(2, CONT_TOP+2, SCR_W-4, tzones, TZ_COUNT, init);
    if (sel < 0) { return 0; }
    kstrcpy(w->timezone, tzones[sel]);
    return 1;
}

/* ── Stage 4: Root password ──────────────────────────────────────────────── */
static int stage_root_pw(struct wiz *w) {
    char confirm[64];
    int rc;

    draw_header(5, 7, "Root Password");
    clear_content();
    vga_write_str_at(2, CONT_TOP,
        "Set the root (administrator) password.", CC_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+1,
        "Root has full control over the system.", CB_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+3, "Password:", CC_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+7, "Confirm password:", CC_FG, CC_BG);
    draw_bottom("  ENTER: Confirm   ESC/Q: Back");

    if (w->root_pw[0] == '\0') { kstrcpy(w->root_pw, ""); }
    rc = run_input(2, CONT_TOP+5, 44, w->root_pw, sizeof(w->root_pw), 1);
    if (rc < 0) { return 0; }

    confirm[0] = '\0';
    rc = run_input(2, CONT_TOP+9, 44, confirm, sizeof(confirm), 1);
    if (rc < 0) { return 0; }

    if (kstrcmp(w->root_pw, confirm) != 0) {
        vga_fill_rect(2, CONT_TOP+11, 60, 1, ' ', CW_FG, CC_BG);
        vga_write_str_at(2, CONT_TOP+11,
            "Passwords do not match! Press any key to try again.", CW_FG, CC_BG);
        keyboard_get_key();
        w->root_pw[0] = '\0';
        return stage_root_pw(w); /* retry */
    }
    return 1;
}

/* ── Stage 5: User account ───────────────────────────────────────────────── */
static int stage_user(struct wiz *w) {
    char confirm[64];
    int rc;

    draw_header(6, 7, "User Account");
    clear_content();
    vga_write_str_at(2, CONT_TOP,
        "Create a regular user account.", CC_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+1,
        "Use lowercase letters and digits only. Minimum 2 characters.", CB_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+3, "Username:", CC_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+7, "Password:", CC_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+11, "Confirm password:", CC_FG, CC_BG);
    draw_bottom("  ENTER: Confirm   ESC/Q: Back");

    if (w->username[0] == '\0') { kstrcpy(w->username, "debian"); }
    rc = run_input(2, CONT_TOP+5, 30, w->username, sizeof(w->username), 0);
    if (rc < 0) { return 0; }
    if (slen(w->username) < 2) { kstrcpy(w->username, "debian"); }

    if (w->user_pw[0] == '\0') { kstrcpy(w->user_pw, ""); }
    rc = run_input(2, CONT_TOP+9, 44, w->user_pw, sizeof(w->user_pw), 1);
    if (rc < 0) { return 0; }

    confirm[0] = '\0';
    rc = run_input(2, CONT_TOP+13, 44, confirm, sizeof(confirm), 1);
    if (rc < 0) { return 0; }

    if (kstrcmp(w->user_pw, confirm) != 0) {
        vga_fill_rect(2, CONT_TOP+15, 60, 1, ' ', CW_FG, CC_BG);
        vga_write_str_at(2, CONT_TOP+15,
            "Passwords do not match! Press any key to try again.", CW_FG, CC_BG);
        keyboard_get_key();
        w->user_pw[0] = '\0';
        return stage_user(w);
    }
    return 1;
}

/* ── Stage 6: Summary + confirm ─────────────────────────────────────────── */
static int stage_summary(const struct wiz *w) {
    char nbuf[12];
    struct disk_info di;
    char confirm[8];
    int rc;

    draw_header(7, 7, "Summary");
    clear_content();
    vga_write_str_at(2, CONT_TOP, "Installation Summary", CC_FG, CC_BG);
    vga_fill_rect(2, CONT_TOP+1, 40, 1, CH_H, CB_FG, CC_BG);

    /* Disk */
    vga_write_str_at(4, CONT_TOP+3, "Target disk :", CB_FG, CC_BG);
    if (disk_get_info(w->disk_index, &di) == 0 && di.present) {
        char dline[48];
        int p = 0;
        char sec[12];
        u32_to_str((uint32_t)w->disk_index, nbuf, sizeof(nbuf));
        dline[p++] = '['; { int j=0; while(nbuf[j]&&p<46){dline[p++]=nbuf[j++];} } dline[p++]=']'; dline[p++]=' ';
        u32_to_str(di.sectors28, sec, sizeof(sec));
        { int j=0; while(sec[j]&&p<46){dline[p++]=sec[j++];} }
        { const char *s=" sectors"; int j=0; while(s[j]&&p<46){dline[p++]=s[j++];} }
        if (di.model[0]) { dline[p++]=' '; int j=0; while(di.model[j]&&p<46){dline[p++]=di.model[j++];} }
        dline[p]='\0';
        vga_write_str_at(20, CONT_TOP+3, dline, CG_FG, CC_BG);
    }

    vga_write_str_at(4, CONT_TOP+4, "Hostname    :", CB_FG, CC_BG);
    vga_write_str_at(20, CONT_TOP+4, w->hostname, CG_FG, CC_BG);

    vga_write_str_at(4, CONT_TOP+5, "Timezone    :", CB_FG, CC_BG);
    vga_write_str_at(20, CONT_TOP+5, w->timezone, CG_FG, CC_BG);

    vga_write_str_at(4, CONT_TOP+6, "Root pass   :", CB_FG, CC_BG);
    vga_write_str_at(20, CONT_TOP+6, w->root_pw[0] ? "[set]" : "[empty]",
        w->root_pw[0] ? CG_FG : CW_FG, CC_BG);

    vga_write_str_at(4, CONT_TOP+7, "Username    :", CB_FG, CC_BG);
    vga_write_str_at(20, CONT_TOP+7, w->username, CG_FG, CC_BG);

    vga_write_str_at(4, CONT_TOP+8, "User pass   :", CB_FG, CC_BG);
    vga_write_str_at(20, CONT_TOP+8, w->user_pw[0] ? "[set]" : "[empty]",
        w->user_pw[0] ? CG_FG : CW_FG, CC_BG);

    vga_fill_rect(2, CONT_TOP+10, 40, 1, CH_H, CB_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+11,
        "\xfe Type YES and press ENTER to begin installation:", CW_FG, CC_BG);

    draw_bottom("  Type YES to confirm   ESC/Q: Go back");

    confirm[0] = '\0';
    rc = run_input(2, CONT_TOP+13, 12, confirm, sizeof(confirm), 0);
    if (rc < 0) { return 0; }
    if (kstrcmp(confirm, "YES") != 0 && kstrcmp(confirm, "yes") != 0) {
        return 0; /* back if not confirmed */
    }
    return 1;
}

/* ── Stage 7: Install ────────────────────────────────────────────────────── */

struct step_desc { const char *label; int pct_before; int pct_after; };

static void mark_step(int y, const char *label, uint8_t col) {
    char line[SCR_W + 1];
    int p = 0;
    line[p++] = ' ';
    line[p++] = '[';
    line[p++] = col == CG_FG ? '*' : (col == CX_FG ? '~' : ' ');
    line[p++] = ']';
    line[p++] = ' ';
    { int j = 0; while (label[j] && p < SCR_W - 1) { line[p++] = label[j++]; } }
    line[p] = '\0';
    vga_fill_rect(0, y, SCR_W, 1, ' ', CC_FG, CC_BG);
    vga_write_str_at(2, y, line, col, CC_BG);
}

static int stage_install(const struct wiz *w) {
    static const char *step_labels[] = {
        "Writing bootloader and kernel to disk...",
        "Formatting filesystem area...",
        "Creating directory structure...",
        "Writing configuration files...",
        "Saving filesystem to disk..."
    };
    static const int step_pcts[] = { 0, 20, 40, 65, 85 };
    int root, home_node, uid1000;
    int i;
    char passwd_line[128];
    char group_line[128];
    char home_path[48];

    draw_header(7, 7, "Installing");
    clear_content();
    vga_write_str_at(2, CONT_TOP, "Installing FurOS 1.0...", CC_FG, CC_BG);
    draw_bottom("  Please wait — do not power off.");

    /* Draw all steps as pending */
    for (i = 0; i < 5; i++) {
        mark_step(CONT_TOP + 2 + i, step_labels[i], CB_FG);
    }
    draw_progress(CONT_TOP + 8, 0);

#define DO_STEP(idx, fn_expr) \
    mark_step(CONT_TOP + 2 + (idx), step_labels[idx], CX_FG); \
    draw_progress(CONT_TOP + 8, step_pcts[idx]); \
    if ((fn_expr) != 0) { \
        mark_step(CONT_TOP + 2 + (idx), step_labels[idx], CW_FG); \
        vga_write_str_at(2, CONT_TOP + 10, "Installation failed!", CW_FG, CC_BG); \
        vga_write_str_at(2, CONT_TOP + 11, disk_last_error(), CW_FG, CC_BG); \
        draw_bottom("  Press any key to halt."); \
        keyboard_get_key(); \
        halt_forever(); \
    } \
    mark_step(CONT_TOP + 2 + (idx), step_labels[idx], CG_FG);

    DO_STEP(0, disk_install_system());
    DO_STEP(1, disk_format_fs());

    /* Step 2: directories */
    mark_step(CONT_TOP + 4, step_labels[2], CX_FG);
    draw_progress(CONT_TOP + 8, 40);
    root = fs_get_root();
    fs_mkdir_as(root, "bin",  0, 0755);
    fs_mkdir_as(root, "etc",  0, 0755);
    fs_mkdir_as(root, "home", 0, 0755);
    fs_mkdir_as(root, "var",  0, 0755);
    fs_mkdir_as(root, "tmp",  0, 01777);
    fs_mkdir_as(root, "usr",  0, 0755);
    fs_mkdir_as(root, "run",  0, 0755);
    home_node = fs_resolve(root, "home");
    uid1000 = 1000;
    kstrcpy(home_path, w->username);
    if (home_node >= 0) {
        fs_mkdir_as(home_node, home_path, (uint16_t)uid1000, 0755);
    }
    mark_step(CONT_TOP + 4, step_labels[2], CG_FG);

    /* Step 3: config files */
    mark_step(CONT_TOP + 5, step_labels[3], CX_FG);
    draw_progress(CONT_TOP + 8, 65);

    /* /etc/hostname */
    {
        char hn[34];
        kstrcpy(hn, w->hostname);
        hn[slen(hn)] = '\n'; hn[slen(hn)+1] = '\0';
        /* Actually need proper approach: */
        char hn2[34]; int p = 0;
        int j = 0;
        while (w->hostname[j]) { hn2[p++] = w->hostname[j++]; }
        hn2[p++] = '\n'; hn2[p] = '\0';
        fs_write_file(root, "etc/hostname", hn2, (size_t)p);
    }

    /* /etc/timezone */
    {
        char tz[34]; int p = 0;
        int j = 0;
        while (w->timezone[j]) { tz[p++] = w->timezone[j++]; }
        tz[p++] = '\n'; tz[p] = '\0';
        fs_write_file(root, "etc/timezone", tz, (size_t)p);
    }

    /* /etc/os-release */
    {
        const char *osr =
            "PRETTY_NAME=\"FurOS 1.0 (Bookworm)\"\n"
            "NAME=\"FurOS\"\n"
            "VERSION_ID=\"1.0\"\n"
            "VERSION=\"1.0 (Bookworm)\"\n"
            "VERSION_CODENAME=bookworm\n"
            "ID=furos\n"
            "ID_LIKE=debian\n"
            "HOME_URL=\"https://furos.org/\"\n";
        fs_write_file(root, "etc/os-release", osr, kstrlen(osr));
    }

    /* /etc/passwd  (root + chosen user) */
    {
        int p = 0;
        const char *rline = "root:x:0:0:root:/root:/bin/sh\n";
        while (rline[p]) { passwd_line[p] = rline[p]; p++; }
        /* user line */
        int j = 0;
        while (w->username[j] && p < 120) { passwd_line[p++] = w->username[j++]; }
        const char *rest = ":x:1000:1000::/home/";
        j = 0; while (rest[j] && p < 120) { passwd_line[p++] = rest[j++]; }
        j = 0; while (w->username[j] && p < 120) { passwd_line[p++] = w->username[j++]; }
        const char *tail = ":/bin/sh\n";
        j = 0; while (tail[j] && p < 120) { passwd_line[p++] = tail[j++]; }
        passwd_line[p] = '\0';
        fs_write_file(root, "etc/passwd", passwd_line, (size_t)p);
    }

    /* /etc/group */
    {
        int p = 0;
        const char *head = "root:x:0:\nsudo:x:27:";
        int j = 0; while (head[j] && p < 120) { group_line[p++] = head[j++]; }
        j = 0; while (w->username[j] && p < 120) { group_line[p++] = w->username[j++]; }
        group_line[p++] = '\n';
        j = 0; while (w->username[j] && p < 120) { group_line[p++] = w->username[j++]; }
        const char *tail = ":x:1000:\n";
        j = 0; while (tail[j] && p < 120) { group_line[p++] = tail[j++]; }
        group_line[p] = '\0';
        fs_write_file(root, "etc/group", group_line, (size_t)p);
    }

    /* /etc/motd */
    {
        const char *motd =
            "\n"
            "  ______           ____  ____\n"
            " |  ____|         / __ \\/ ___|\n"
            " | |__ _   _ _ __| |  | \\___ \\\n"
            " |  __| | | | '__| |  | |___) |\n"
            " | |  | |_| | |  | |__| |____/\n"
            " |_|   \\__,_|_|   \\____/\n"
            "\n"
            " FurOS 1.0  \xb3  Debian GNU/Linux 12 Bookworm\n"
            " Type 'help' for available commands.\n"
            " Type 'logout' to return to the desktop.\n"
            "\n";
        fs_write_file(root, "etc/motd", motd, kstrlen(motd));
    }

    /* /home/<user>/.bashrc */
    {
        home_node = fs_resolve(root, "home");
        if (home_node >= 0) {
            int hn = fs_resolve(home_node, w->username);
            if (hn >= 0) {
                const char *bashrc =
                    "# FurOS ~/.bashrc\n"
                    "alias ls='ls'\n"
                    "alias ll='ls'\n";
                fs_write_file(hn, ".bashrc", bashrc, kstrlen(bashrc));
            }
        }
    }
    mark_step(CONT_TOP + 5, step_labels[3], CG_FG);

    DO_STEP(4, disk_save_fs());

#undef DO_STEP

    draw_progress(CONT_TOP + 8, 100);
    return 1;
}

/* ── Stage 8: Complete ───────────────────────────────────────────────────── */
static void stage_complete(const struct wiz *w) {
    draw_header(7, 7, "Complete");
    clear_content();

    vga_fill_rect(2, CONT_TOP, 76, 1, CH_BLK, CG_FG, CC_BG);
    vga_write_str_at(4, CONT_TOP, "  Installation Complete!  ", CG_FG, CC_BG);
    vga_fill_rect(2, CONT_TOP+1, 76, 1, CH_BLK, CG_FG, CC_BG);

    vga_write_str_at(2, CONT_TOP+3, "FurOS 1.0 has been installed successfully.", CG_FG, CC_BG);

    vga_write_str_at(2, CONT_TOP+5, "To start using FurOS:", CC_FG, CC_BG);
    vga_write_str_at(4, CONT_TOP+6, "1. Power off this computer", CB_FG, CC_BG);
    vga_write_str_at(4, CONT_TOP+7, "2. Remove the installer ISO or USB drive", CB_FG, CC_BG);
    vga_write_str_at(4, CONT_TOP+8, "3. Power on — FurOS will boot from disk", CB_FG, CC_BG);

    vga_write_str_at(2, CONT_TOP+10, "Installed as:", CC_FG, CC_BG);
    vga_write_str_at(4, CONT_TOP+11, "Hostname : ", CB_FG, CC_BG);
    vga_write_str_at(15, CONT_TOP+11, w->hostname, CG_FG, CC_BG);
    vga_write_str_at(4, CONT_TOP+12, "Username : ", CB_FG, CC_BG);
    vga_write_str_at(15, CONT_TOP+12, w->username, CG_FG, CC_BG);
    vga_write_str_at(4, CONT_TOP+13, "Timezone : ", CB_FG, CC_BG);
    vga_write_str_at(15, CONT_TOP+13, w->timezone, CG_FG, CC_BG);

    draw_bottom("  ENTER: Halt system");
    keyboard_get_key();
}

/* ── Disk not found diagnostic ───────────────────────────────────────────── */
static void show_no_disk(void) {
    struct ata_debug_state dbg;
    char h[12];

    ata_debug_state(&dbg);
    draw_header(0, 0, "Error");
    clear_content();

    vga_write_str_at(2, CONT_TOP, "No supported disk found!", CW_FG, CC_BG);
    vga_write_str_at(2, CONT_TOP+1, disk_last_error(), CB_FG, CC_BG);

    vga_write_str_at(2, CONT_TOP+3, "PCI host bridge :", CB_FG, CC_BG);
    hex32_to_str(dbg.pci_host_id, h);
    vga_write_str_at(21, CONT_TOP+3, h, CX_FG, CC_BG);

    vga_write_str_at(2, CONT_TOP+4, "PCI 00:13.0 id  :", CB_FG, CC_BG);
    hex32_to_str(dbg.pci_ahci_slot_id, h);
    vga_write_str_at(21, CONT_TOP+4, h, CX_FG, CC_BG);

    vga_write_str_at(2, CONT_TOP+5, "AHCI seen/ports :", CB_FG, CC_BG);
    {
        char tmp[24]; int p = 0;
        char n1[8], n2[8];
        u32_to_str((uint32_t)dbg.ahci_controllers_seen, n1, sizeof(n1));
        u32_to_str((uint32_t)dbg.ahci_ports_considered, n2, sizeof(n2));
        int j = 0;
        while (n1[j] && p < 22) { tmp[p++] = n1[j++]; }
        tmp[p++] = '/';
        j = 0;
        while (n2[j] && p < 22) { tmp[p++] = n2[j++]; }
        tmp[p] = '\0';
        vga_write_str_at(21, CONT_TOP+5, tmp, CX_FG, CC_BG);
    }

    vga_write_str_at(2, CONT_TOP+7,
        "Attach an ATA/SATA disk and restart the installer.", CB_FG, CC_BG);
    draw_bottom("  Press any key to halt.");
    keyboard_get_key();
    halt_forever();
}

/* ── Main entry point ────────────────────────────────────────────────────── */
__attribute__((noreturn))
void installer_run(void) {
    struct wiz ws;
    int stage = 0;
    int result;

    keyboard_init();
    fs_init();
    disk_init();

    /* Full-screen init */
    vga_reset_viewport();
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();

    /* No disks at all → diagnostic page */
    if (disk_count() <= 0) {
        show_no_disk();
    }

    /* Init wizard state */
    kmemset(&ws, 0, sizeof(ws));
    ws.disk_index = -1;
    kstrcpy(ws.hostname, "furos");
    kstrcpy(ws.timezone, "UTC");
    kstrcpy(ws.username, "debian");

    while (stage >= 0 && stage <= 7) {
        switch (stage) {
            case 0: result = stage_welcome();     break;
            case 1: result = stage_disk(&ws);     break;
            case 2: result = stage_hostname(&ws); break;
            case 3: result = stage_timezone(&ws); break;
            case 4: result = stage_root_pw(&ws);  break;
            case 5: result = stage_user(&ws);     break;
            case 6: result = stage_summary(&ws);  break;
            case 7: result = stage_install(&ws);  break;
            default: result = -1; break;
        }

        if (result > 0) {
            stage++;
        } else if (result == 0) {
            /* Back: go to previous stage (but not before 0) */
            if (stage > 0) { stage--; }
        } else {
            /* Cancel/quit */
            vga_reset_viewport();
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            vga_clear();
            vga_write_string("Installer cancelled.\n");
            vga_write_string("Press any key to halt.\n");
            keyboard_getchar();
            halt_forever();
        }
    }

    /* Stage 8: complete */
    stage_complete(&ws);
    halt_forever();
}
