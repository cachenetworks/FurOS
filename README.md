# FurOS

FurOS is a minimal x86_64 freestanding kernel that boots with GRUB (BIOS/Legacy) and runs a terminal-only shell using VGA text mode at `0xB8000`.

Current shell commands:

- `help`, `clear`, `pwd`, `ls`, `cd`, `mkdir`, `touch`, `cat`, `write`, `rm`
- `nano` (simple in-terminal editor: `Ctrl+S` save, `Ctrl+X` save+exit)
- `disk list|select <n>|info|load|save|format`, `sync`
- `install [disk_index]` (writes a persistent FurOS filesystem image to selected ATA disk)
- `apt list|install|remove` (minimal simulated package manager)

## Dependencies

### Arch Linux

```sh
sudo pacman -S --needed qemu-system-x86 grub xorriso clang gcc lld binutils make git
```

### Ubuntu

```sh
sudo apt update
sudo apt install -y qemu-system-x86 grub-pc-bin grub-common xorriso clang gcc lld binutils make git
```

## Build And Run

```sh
make && make run
```

`make run` starts the installer ISO and attaches `build/furos_disk.img` as the install target.
Install FurOS in the installer, power off, then boot from disk:

```sh
make run_disk
```

Installer ISO boot (only when needed):

```sh
make run_iso
```

VirtualBox disk image:

```sh
make vdi
```

Then attach `build/furos_disk.vdi` to a VM configured for BIOS boot.
Supported and tested controllers: IDE (PIIX) and SATA/AHCI (Intel AHCI) in VirtualBox.

Install flow:

1. Boot installer ISO with `make run_iso`
2. In installer mode, choose target disk and type `INSTALL`
3. Shutdown VM
4. Remove ISO and boot from disk with `make run_disk`

After reboot, FurOS auto-loads the installed filesystem from disk.

## Troubleshooting

- Blank screen:
  - Ensure BIOS/Legacy boot is being used (no OVMF/UEFI firmware).
  - If needed, set `#define FUR_OS_SERIAL_DEBUG 1` in `kernel/serial.h` to mirror boot messages to COM1.
- No disks in installer:
  - In VirtualBox, use BIOS firmware and attach storage on IDE or SATA/AHCI controllers.
  - NVMe and VirtIO controllers are not implemented yet.
