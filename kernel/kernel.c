#include <stdint.h>

#include "installer.h"
#include "serial.h"
#include "shell.h"
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

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_write_string("FurOS");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_write_string(" 1.0 (based on Debian GNU/Linux 12 Bookworm)\n");
    vga_write_string("[  OK  ] Kernel initialized\n");

#if FUR_OS_SERIAL_DEBUG
    serial_init();
    serial_write_string("FurOS 1.0 kernel initialized\n");
#endif

    if (fur_boot_magic == MULTIBOOT2_BOOT_MAGIC) {
        vga_write_string("[  OK  ] Installer mode\n");
        installer_run();
    } else {
        vga_write_string("[  OK  ] Starting shell\n");
        shell_run();
    }

    halt_forever();
}
