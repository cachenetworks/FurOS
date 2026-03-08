#ifndef FUR_ATA_H
#define FUR_ATA_H

#include <stdint.h>

#define ATA_MAX_DEVICES 16

#define ATA_IFACE_IDE 0
#define ATA_IFACE_AHCI 1

struct ata_device_info {
    int present;
    int index;
    uint8_t interface_type;
    uint16_t io_base;
    uint8_t slave;
    uint32_t sectors28;
    char model[41];
};

struct ata_debug_state {
    uint32_t pci_host_id;
    uint32_t pci_ahci_slot_id;
    uint32_t pci_ahci_slot_class;
    uint32_t pci_ahci_slot_bar5;
    int ahci_controllers_seen;
    int ahci_ports_considered;
    int ahci_devices_found;
};

int ata_scan(void);
int ata_get_info(int index, struct ata_device_info *out);
int ata_select_device(int index);
int ata_selected_device(void);
int ata_read_sector(uint32_t lba, uint8_t *buffer512);
int ata_write_sector(uint32_t lba, const uint8_t *buffer512);
int ata_flush(void);
const char *ata_last_error(void);
uint8_t ata_last_status(void);
uint8_t ata_last_error_reg(void);
void ata_debug_state(struct ata_debug_state *out);

#endif
