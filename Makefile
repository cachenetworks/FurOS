CC ?= gcc
LD ?= ld
NM ?= nm
OBJCOPY ?= objcopy
GRUB_MKRESCUE ?= grub-mkrescue
QEMU_IMG ?= qemu-img

BUILD_DIR := build
ISO_ROOT := iso_root
KERNEL_DIR := kernel
DISK_IMAGE := $(BUILD_DIR)/furos_disk.img
VDI_IMAGE := $(BUILD_DIR)/furos_disk.vdi
BOOTSECT_OBJ := $(BUILD_DIR)/bootsect.o
BOOTSECT_BIN := $(BUILD_DIR)/bootsect.bin
BOOTSECT_BLOB_OBJ := $(BUILD_DIR)/bootsect_blob.o
BOOTINFO_BIN := $(BUILD_DIR)/bootinfo.bin
KERNEL_BIN := $(BUILD_DIR)/kernel.bin

CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic \
	-ffreestanding -fno-stack-protector -nostdlib -fno-pic -fno-pie \
	-m64 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -I$(KERNEL_DIR)

ASFLAGS := -ffreestanding -fno-pic -fno-pie -m64

LDFLAGS := -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T $(KERNEL_DIR)/linker.ld

KERNEL_OBJS := \
	$(BUILD_DIR)/boot.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/vga.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/kstring.o \
	$(BUILD_DIR)/fs.o \
	$(BUILD_DIR)/ata.o \
	$(BUILD_DIR)/disk.o \
	$(BUILD_DIR)/installer.o \
	$(BUILD_DIR)/shell.o \
	$(BOOTSECT_BLOB_OBJ)
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ISO_IMAGE := $(BUILD_DIR)/fursos.iso

.PHONY: all run run_iso run_disk vdi clean stage bootstrap_disk

all: $(ISO_IMAGE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_DIR)/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF) | $(BUILD_DIR)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

$(BOOTSECT_BIN): boot/bootsect.S | $(BUILD_DIR)
	$(CC) -m16 -x assembler-with-cpp -c boot/bootsect.S -o $(BOOTSECT_OBJ)
	$(LD) -m elf_i386 -Ttext 0x7c00 --oformat binary -nostdlib -N -o $(BOOTSECT_BIN) $(BOOTSECT_OBJ)

$(BOOTSECT_BLOB_OBJ): $(BOOTSECT_BIN) | $(BUILD_DIR)
	$(LD) -r -b binary -o $(BOOTSECT_BLOB_OBJ) $(BOOTSECT_BIN)

stage: $(KERNEL_ELF) boot/grub.cfg
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp boot/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg

$(ISO_IMAGE): stage | $(BUILD_DIR)
	$(GRUB_MKRESCUE) -o $(ISO_IMAGE) $(ISO_ROOT)

$(DISK_IMAGE): | $(BUILD_DIR)
	@if [ ! -f $(DISK_IMAGE) ]; then $(QEMU_IMG) create -f raw $(DISK_IMAGE) 64M; fi

$(BOOTINFO_BIN): $(KERNEL_BIN) $(KERNEL_ELF) | $(BUILD_DIR)
	@KERNEL_SIZE=$$(stat -c%s $(KERNEL_BIN)); \
	KERNEL_SECTORS=$$((($$KERNEL_SIZE + 511) / 512)); \
	KERNEL_ENTRY=$$($(NM) -n $(KERNEL_ELF) | awk '/ _start32$$/ { print "0x" $$1; exit }'); \
	KERNEL_BASE=$$($(NM) -n $(KERNEL_ELF) | awk '/ __kernel_image_start$$/ { print "0x" $$1; exit }'); \
	test -n "$$KERNEL_ENTRY"; \
	test -n "$$KERNEL_BASE"; \
	ENTRY_OFFSET=$$(( $$KERNEL_ENTRY - $$KERNEL_BASE )); \
	truncate -s 512 $(BOOTINFO_BIN); \
	printf 'FURBOOT1' | dd of=$(BOOTINFO_BIN) bs=1 seek=0 conv=notrunc status=none; \
	printf "\\x%02x\\x%02x\\x%02x\\x%02x" \
		$$((KERNEL_SIZE & 0xFF)) \
		$$(((KERNEL_SIZE >> 8) & 0xFF)) \
		$$(((KERNEL_SIZE >> 16) & 0xFF)) \
		$$(((KERNEL_SIZE >> 24) & 0xFF)) \
		| dd of=$(BOOTINFO_BIN) bs=1 seek=8 conv=notrunc status=none; \
	printf "\\x%02x\\x%02x\\x%02x\\x%02x" \
		$$((KERNEL_SECTORS & 0xFF)) \
		$$(((KERNEL_SECTORS >> 8) & 0xFF)) \
		$$(((KERNEL_SECTORS >> 16) & 0xFF)) \
		$$(((KERNEL_SECTORS >> 24) & 0xFF)) \
		| dd of=$(BOOTINFO_BIN) bs=1 seek=12 conv=notrunc status=none; \
	printf "\\x%02x\\x%02x\\x%02x\\x%02x" \
		$$((ENTRY_OFFSET & 0xFF)) \
		$$(((ENTRY_OFFSET >> 8) & 0xFF)) \
		$$(((ENTRY_OFFSET >> 16) & 0xFF)) \
		$$(((ENTRY_OFFSET >> 24) & 0xFF)) \
		| dd of=$(BOOTINFO_BIN) bs=1 seek=16 conv=notrunc status=none

bootstrap_disk: $(DISK_IMAGE) $(BOOTSECT_BIN) $(BOOTINFO_BIN) $(KERNEL_BIN)
	dd if=$(BOOTSECT_BIN) of=$(DISK_IMAGE) bs=512 count=1 conv=notrunc
	dd if=$(BOOTINFO_BIN) of=$(DISK_IMAGE) bs=512 seek=1 count=1 conv=notrunc
	dd if=$(KERNEL_BIN) of=$(DISK_IMAGE) bs=512 seek=2 conv=notrunc

run: run_iso

run_disk: $(DISK_IMAGE)
	qemu-system-x86_64 -m 512M -serial stdio -drive file=$(DISK_IMAGE),format=raw,if=ide

run_iso: all $(DISK_IMAGE)
	qemu-system-x86_64 -m 512M -cdrom $(ISO_IMAGE) -serial stdio -drive file=$(DISK_IMAGE),format=raw,if=ide

vdi: $(DISK_IMAGE)
	$(QEMU_IMG) convert -f raw -O vdi $(DISK_IMAGE) $(VDI_IMAGE)

clean:
	rm -rf $(BUILD_DIR) $(ISO_ROOT)
