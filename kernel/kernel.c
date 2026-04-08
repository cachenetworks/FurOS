#include <stdint.h>

#include "desktop.h"
#include "installer.h"
#include "serial.h"
#include "vga.h"

#define MULTIBOOT2_BOOT_MAGIC 0x36D76289u

extern uint32_t fur_boot_magic;

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

__attribute__((noreturn))
void kernel_main(void) {
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();

#if FUR_OS_SERIAL_DEBUG
    serial_init();
    serial_write_string("FurOS 1.0 kernel initialized\n");
#endif

    if (fur_boot_magic == MULTIBOOT2_BOOT_MAGIC) {
        installer_run();   /* Arch-style TUI installer — noreturn */
    } else {
        desktop_run();     /* KDE Plasma-style text desktop — noreturn */
    }

    halt_forever();
}
