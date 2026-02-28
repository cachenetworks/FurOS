#include "disk.h"

#include "ata.h"
#include "fs.h"
#include "kstring.h"

#include <stddef.h>
#include <stdint.h>

#define DISK_START_LBA 2048
#define DISK_TOTAL_SECTORS 4096
#define SECTOR_SIZE 512
#define DISK_PAYLOAD_MAX ((DISK_TOTAL_SECTORS - 1) * SECTOR_SIZE)
#define BOOT_INFO_LBA 1
#define KERNEL_LBA_START 2
#define BOOT_INFO_MAGIC "FURBOOT1"
#define BOOTSECT_SIZE 512
#define MAX_BOOT_KERNEL_SECTORS 1920

struct disk_header {
    char magic[8];
    uint32_t version;
    uint32_t payload_size;
    uint32_t checksum;
} __attribute__((packed));

struct boot_info_sector {
    char magic[8];
    uint32_t kernel_size_bytes;
    uint32_t kernel_sectors;
    uint32_t entry_offset;
    uint32_t reserved;
    uint8_t pad[SECTOR_SIZE - 24];
} __attribute__((packed));

static int g_disk_count = 0;
static int g_selected_disk = -1;
static const char *g_last_error = "ok";
static uint8_t g_sector[SECTOR_SIZE];
static uint8_t g_payload[DISK_PAYLOAD_MAX];

extern const uint8_t __kernel_image_start[];
extern const uint8_t __kernel_image_end[];
extern const uint8_t _start32[];
extern const uint8_t _binary_build_bootsect_bin_start[];
extern const uint8_t _binary_build_bootsect_bin_end[];

static uint32_t checksum_bytes(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

void disk_init(void) {
    g_disk_count = ata_scan();
    g_selected_disk = ata_selected_device();
    if (g_disk_count > 0 && g_selected_disk >= 0) {
        g_last_error = "ok";
    } else {
        g_last_error = ata_last_error();
    }
}

int disk_available(void) {
    return g_selected_disk >= 0;
}

int disk_count(void) {
    return g_disk_count;
}

int disk_get_info(int index, struct disk_info *out) {
    struct ata_device_info info;

    if (out == 0) {
        return -1;
    }
    if (ata_get_info(index, &info) != 0) {
        return -1;
    }

    out->present = info.present;
    out->index = info.index;
    out->interface_type = info.interface_type;
    out->sectors28 = info.sectors28;
    out->io_base = info.io_base;
    out->slave = info.slave;
    kmemcpy(out->model, info.model, sizeof(out->model));
    return 0;
}

int disk_select(int index) {
    if (ata_select_device(index) != 0) {
        g_last_error = "invalid or missing disk index";
        return -1;
    }
    g_selected_disk = index;
    g_last_error = "ok";
    return 0;
}

int disk_selected(void) {
    return g_selected_disk;
}

int disk_install_system(void) {
    struct boot_info_sector info;
    size_t kernel_size;
    size_t entry_offset;
    uint32_t kernel_sectors;
    size_t i;
    const uint8_t *bootsect_start = _binary_build_bootsect_bin_start;
    const uint8_t *bootsect_end = _binary_build_bootsect_bin_end;

    if (!disk_available()) {
        g_last_error = "disk unavailable";
        return -1;
    }

    if ((size_t)(bootsect_end - bootsect_start) < BOOTSECT_SIZE) {
        g_last_error = "boot sector blob missing";
        return -1;
    }

    kernel_size = (size_t)(__kernel_image_end - __kernel_image_start);
    entry_offset = (size_t)(_start32 - __kernel_image_start);
    if (kernel_size == 0 || entry_offset >= kernel_size) {
        g_last_error = "invalid kernel image bounds";
        return -1;
    }

    kernel_sectors = (uint32_t)((kernel_size + SECTOR_SIZE - 1u) / SECTOR_SIZE);
    if (kernel_sectors == 0 || kernel_sectors > MAX_BOOT_KERNEL_SECTORS) {
        g_last_error = "kernel too large for bootloader";
        return -1;
    }

    kmemcpy(g_sector, bootsect_start, BOOTSECT_SIZE);
    if (ata_write_sector(0, g_sector) != 0) {
        g_last_error = ata_last_error();
        return -1;
    }

    kmemset(&info, 0, sizeof(info));
    kmemcpy(info.magic, BOOT_INFO_MAGIC, 8);
    info.kernel_size_bytes = (uint32_t)kernel_size;
    info.kernel_sectors = kernel_sectors;
    info.entry_offset = (uint32_t)entry_offset;

    kmemcpy(g_sector, &info, sizeof(info));
    if (ata_write_sector(BOOT_INFO_LBA, g_sector) != 0) {
        g_last_error = ata_last_error();
        return -1;
    }

    for (i = 0; i < kernel_sectors; i++) {
        size_t offset = i * SECTOR_SIZE;
        size_t remain = kernel_size - offset;
        size_t chunk = (remain >= SECTOR_SIZE) ? SECTOR_SIZE : remain;

        kmemset(g_sector, 0, sizeof(g_sector));
        kmemcpy(g_sector, __kernel_image_start + offset, chunk);

        if (ata_write_sector(KERNEL_LBA_START + (uint32_t)i, g_sector) != 0) {
            g_last_error = ata_last_error();
            return -1;
        }
    }

    g_last_error = "ok";
    return 0;
}

const char *disk_last_error(void) {
    return g_last_error;
}

int disk_load_fs(void) {
    struct disk_header header;
    size_t sectors;
    size_t i;
    uint32_t sum;

    if (!disk_available()) {
        g_last_error = "disk unavailable";
        return -1;
    }

    if (ata_read_sector(DISK_START_LBA, g_sector) != 0) {
        g_last_error = ata_last_error();
        return -1;
    }

    kmemcpy(&header, g_sector, sizeof(header));
    if (kstrncmp(header.magic, "FURDSK1", 7) != 0 || header.version != 1) {
        g_last_error = "not installed";
        return -1;
    }

    if (header.payload_size == 0 || header.payload_size > DISK_PAYLOAD_MAX) {
        g_last_error = "invalid payload size";
        return -1;
    }

    sectors = (header.payload_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for (i = 0; i < sectors; i++) {
        if (ata_read_sector(DISK_START_LBA + 1 + (uint32_t)i, g_payload + (i * SECTOR_SIZE)) != 0) {
            g_last_error = ata_last_error();
            return -1;
        }
    }

    sum = checksum_bytes(g_payload, header.payload_size);
    if (sum != header.checksum) {
        g_last_error = "checksum mismatch";
        return -1;
    }

    if (fs_deserialize(g_payload, header.payload_size) != 0) {
        g_last_error = "filesystem decode failed";
        return -1;
    }

    g_last_error = "ok";
    return 0;
}

int disk_save_fs(void) {
    struct disk_header header;
    size_t payload_size;
    size_t sectors;
    size_t i;

    if (!disk_available()) {
        g_last_error = "disk unavailable";
        return -1;
    }

    payload_size = fs_serialize(g_payload, sizeof(g_payload));
    if (payload_size == 0) {
        g_last_error = "filesystem encode failed";
        return -1;
    }

    kstrcpy(header.magic, "FURDSK1");
    header.version = 1;
    header.payload_size = (uint32_t)payload_size;
    header.checksum = checksum_bytes(g_payload, payload_size);

    kmemset(g_sector, 0, sizeof(g_sector));
    kmemcpy(g_sector, &header, sizeof(header));

    if (ata_write_sector(DISK_START_LBA, g_sector) != 0) {
        g_last_error = ata_last_error();
        return -1;
    }

    sectors = (payload_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for (i = 0; i < sectors; i++) {
        if (ata_write_sector(DISK_START_LBA + 1 + (uint32_t)i, g_payload + (i * SECTOR_SIZE)) != 0) {
            g_last_error = ata_last_error();
            return -1;
        }
    }

    g_last_error = "ok";
    return 0;
}

int disk_format_fs(void) {
    fs_init();
    if (disk_save_fs() != 0) {
        return -1;
    }
    g_last_error = "ok";
    return 0;
}
