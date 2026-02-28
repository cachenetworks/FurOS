#include "installer.h"

#include "ata.h"
#include "disk.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "vga.h"

#include <stddef.h>
#include <stdint.h>

#define INSTALLER_LINE_MAX 64

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void print_u32(uint32_t value) {
    char buf[16];
    int i = 0;
    int j;

    if (value == 0) {
        vga_putchar('0');
        return;
    }

    while (value > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    for (j = i - 1; j >= 0; j--) {
        vga_putchar(buf[j]);
    }
}

static void print_hex16(uint16_t value) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    vga_write_string("0x");
    for (shift = 12; shift >= 0; shift -= 4) {
        vga_putchar(hex[(value >> shift) & 0x0F]);
    }
}

static void print_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    vga_write_string("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        vga_putchar(hex[(value >> shift) & 0x0F]);
    }
}

static int is_printable(char c) {
    return c >= 32 && c <= 126;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int parse_u32(const char *text, uint32_t *out) {
    size_t i = 0;
    uint32_t value = 0;

    if (text == 0 || out == 0 || text[0] == '\0') {
        return -1;
    }

    while (text[i] != '\0') {
        if (!is_digit(text[i])) {
            return -1;
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
        i++;
    }

    *out = value;
    return 0;
}

static void read_line(char *buf, size_t max_len) {
    size_t len = 0;

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            vga_putchar('\n');
            return;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                vga_backspace();
            }
            continue;
        }

        if (is_printable(c) && len + 1 < max_len) {
            buf[len++] = c;
            vga_putchar(c);
        }
    }
}

static void print_disk_table(void) {
    int i;
    struct disk_info info;

    vga_write_string("Detected disks:\n");
    for (i = 0; i < disk_count(); i++) {
        if (disk_get_info(i, &info) != 0 || !info.present) {
            continue;
        }

        vga_write_string("  [");
        print_u32((uint32_t)info.index);
        vga_write_string("] ");
        if (info.interface_type == ATA_IFACE_AHCI) {
            vga_write_string("ahci ");
        } else {
            vga_write_string("ide ");
            vga_write_string(info.slave ? "slave " : "master ");
            vga_write_string("io=");
            print_hex16(info.io_base);
            vga_write_string(" ");
        }
        vga_write_string(" sectors=");
        print_u32(info.sectors28);
        if (info.model[0] != '\0') {
            vga_write_string(" model=\"");
            vga_write_string(info.model);
            vga_write_string("\"");
        }
        vga_write_string("\n");
    }
}

static int choose_target_disk(void) {
    char line[INSTALLER_LINE_MAX];
    uint32_t index = 0;

    while (1) {
        vga_write_string("Target disk index (or q to cancel): ");
        read_line(line, sizeof(line));

        if (line[0] == 'q' || line[0] == 'Q') {
            return -1;
        }

        if (parse_u32(line, &index) != 0) {
            vga_write_string("Invalid number.\n");
            continue;
        }

        if (disk_select((int)index) != 0) {
            vga_write_string("Cannot select disk: ");
            vga_write_string(disk_last_error());
            vga_write_string("\n");
            continue;
        }

        return (int)index;
    }
}

static int confirm_install(void) {
    char line[INSTALLER_LINE_MAX];

    vga_write_string("Type INSTALL to continue: ");
    read_line(line, sizeof(line));

    if (kstrcmp(line, "INSTALL") != 0 && kstrcmp(line, "install") != 0) {
        return -1;
    }
    return 0;
}

static int perform_install(void) {
    const char *release_text = "FurOS installed system\n";

    if (disk_install_system() != 0) {
        return -1;
    }

    if (disk_format_fs() != 0) {
        return -1;
    }

    fs_mkdir(fs_get_root(), "bin");
    fs_mkdir(fs_get_root(), "etc");
    fs_mkdir(fs_get_root(), "home");
    fs_write_file(fs_get_root(), "etc/release", release_text, kstrlen(release_text));

    if (disk_save_fs() != 0) {
        return -1;
    }

    return 0;
}

__attribute__((noreturn))
static void wait_and_halt(void) {
    vga_write_string("Press any key to halt.\n");
    (void)keyboard_getchar();
    halt_forever();
}

__attribute__((noreturn))
void installer_run(void) {
    int selected;

    keyboard_init();
    fs_init();
    disk_init();

    vga_clear();
    vga_write_string("FurOS Installer\n");
    vga_write_string("----------------\n");
    vga_write_string("This mode installs FurOS to disk.\n");
    vga_write_string("No command shell is available here.\n\n");

    if (disk_count() <= 0) {
        struct ata_debug_state dbg;

        ata_debug_state(&dbg);
        vga_write_string("No supported disk controllers detected.\n");
        vga_write_string("Reason: ");
        vga_write_string(disk_last_error());
        vga_write_string("\n");
        vga_write_string("PCI host id: ");
        print_hex32(dbg.pci_host_id);
        vga_write_string("\n");
        vga_write_string("PCI 00:13.0 id/class/bar5: ");
        print_hex32(dbg.pci_ahci_slot_id);
        vga_write_string(" ");
        print_hex32(dbg.pci_ahci_slot_class);
        vga_write_string(" ");
        print_hex32(dbg.pci_ahci_slot_bar5);
        vga_write_string("\n");
        vga_write_string("AHCI seen/ports/devices: ");
        print_u32((uint32_t)dbg.ahci_controllers_seen);
        vga_write_string("/");
        print_u32((uint32_t)dbg.ahci_ports_considered);
        vga_write_string("/");
        print_u32((uint32_t)dbg.ahci_devices_found);
        vga_write_string("\n");
        vga_write_string("Supported now: IDE and AHCI SATA.\n");
        wait_and_halt();
    }

    print_disk_table();
    vga_write_string("\n");

    selected = choose_target_disk();
    if (selected < 0) {
        vga_write_string("Install cancelled.\n");
        wait_and_halt();
    }

    vga_write_string("Selected disk ");
    print_u32((uint32_t)selected);
    vga_write_string(".\n");
    if (confirm_install() != 0) {
        vga_write_string("Confirmation mismatch. Install cancelled.\n");
        wait_and_halt();
    }

    vga_write_string("Installing...\n");
    if (perform_install() != 0) {
        vga_write_string("Install failed: ");
        vga_write_string(disk_last_error());
        vga_write_string("\n");
        wait_and_halt();
    }

    vga_write_string("\nInstall complete.\n");
    vga_write_string("Power off, remove installer ISO, then boot from disk.\n");
    wait_and_halt();
}
