#include "ata.h"

#include "kstring.h"

#include <stddef.h>
#include <stdint.h>

#define ATA_MAX_IDE_DEVICES 4
#define ATA_MAX_AHCI_DEVICES 8

#define ATA_POLL_ATTEMPTS 1000000

#define IDE_REG_DATA 0
#define IDE_REG_ERROR 1
#define IDE_REG_SECCOUNT0 2
#define IDE_REG_LBA0 3
#define IDE_REG_LBA1 4
#define IDE_REG_LBA2 5
#define IDE_REG_HDDEVSEL 6
#define IDE_REG_COMMAND 7
#define IDE_REG_STATUS 7
#define IDE_REG_ALTSTATUS 0x206

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF 0x20
#define ATA_SR_DRDY 0x40
#define ATA_SR_BSY 0x80

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define AHCI_CLASS_MASS_STORAGE 0x01
#define AHCI_SUBCLASS_SATA 0x06

#define HBA_PORT_DET_PRESENT 0x3
#define HBA_PORT_IPM_ACTIVE 0x1
#define HBA_PxCMD_ST (1u << 0)
#define HBA_PxCMD_FRE (1u << 4)
#define HBA_PxCMD_FR (1u << 14)
#define HBA_PxCMD_CR (1u << 15)
#define HBA_PxIS_TFES (1u << 30)

#define SATA_SIG_ATA 0x00000101

#define FIS_TYPE_REG_H2D 0x27

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_DEV_LBA 0x40

enum device_kind {
    DEV_NONE = 0,
    DEV_IDE = 1,
    DEV_AHCI = 2
};

struct ide_device {
    int present;
    uint16_t io_base;
    uint8_t slave;
    uint16_t identify[256];
    uint32_t sectors28;
    char model[41];
};

struct hba_port {
    volatile uint32_t clb;
    volatile uint32_t clbu;
    volatile uint32_t fb;
    volatile uint32_t fbu;
    volatile uint32_t is;
    volatile uint32_t ie;
    volatile uint32_t cmd;
    volatile uint32_t rsv0;
    volatile uint32_t tfd;
    volatile uint32_t sig;
    volatile uint32_t ssts;
    volatile uint32_t sctl;
    volatile uint32_t serr;
    volatile uint32_t sact;
    volatile uint32_t ci;
    volatile uint32_t sntf;
    volatile uint32_t fbs;
    volatile uint32_t rsv1[11];
    volatile uint32_t vendor[4];
};

struct hba_mem {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
    volatile uint32_t vs;
    volatile uint32_t ccc_ctl;
    volatile uint32_t ccc_pts;
    volatile uint32_t em_loc;
    volatile uint32_t em_ctl;
    volatile uint32_t cap2;
    volatile uint32_t bohc;
    uint8_t rsv[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    struct hba_port ports[32];
};

struct hba_cmd_header {
    uint8_t flags;
    uint8_t flags2;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} __attribute__((packed));

struct hba_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc_i;
} __attribute__((packed));

struct hba_cmd_tbl {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    struct hba_prdt_entry prdt_entry[1];
} __attribute__((packed));

struct fis_reg_h2d {
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t rsv1[4];
} __attribute__((packed));

struct ahci_port_ctx {
    uint8_t cmd_list[1024] __attribute__((aligned(1024)));
    uint8_t fis[256] __attribute__((aligned(256)));
    uint8_t cmd_table[256] __attribute__((aligned(128)));
    uint8_t identify[512] __attribute__((aligned(2)));
};

struct ahci_device {
    int present;
    volatile struct hba_port *port;
    uint8_t port_no;
    uint32_t sectors28;
    char model[41];
};

struct device_map {
    int present;
    uint8_t kind;
    uint8_t source_index;
    uint16_t io_base;
    uint8_t slave;
    uint32_t sectors28;
    char model[41];
};

static struct ide_device g_ide_devices[ATA_MAX_IDE_DEVICES] = {
    {0, 0x1F0, 0, {0}, 0, {0}},
    {0, 0x1F0, 1, {0}, 0, {0}},
    {0, 0x170, 0, {0}, 0, {0}},
    {0, 0x170, 1, {0}, 0, {0}}
};

static struct ahci_device g_ahci_devices[ATA_MAX_AHCI_DEVICES];
static struct ahci_port_ctx g_ahci_ctx[ATA_MAX_AHCI_DEVICES];
static int g_ahci_count = 0;
static int g_ahci_controllers_seen = 0;
static int g_ahci_bar64_rejected = 0;
static int g_ahci_ports_considered = 0;
static uint32_t g_dbg_pci_host_id = 0xFFFFFFFFu;
static uint32_t g_dbg_slot13_id = 0xFFFFFFFFu;
static uint32_t g_dbg_slot13_class = 0xFFFFFFFFu;
static uint32_t g_dbg_slot13_bar5 = 0xFFFFFFFFu;

static struct device_map g_devices[ATA_MAX_DEVICES];
static int g_device_count = 0;
static int g_selected_device = -1;

static const char *g_last_error = "ok";
static uint8_t g_last_status = 0;
static uint8_t g_last_error_reg = 0;

static inline void io_wait(void) {
    __asm__ volatile("outb %%al, $0x80" : : "a"(0) : "memory");
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static void error_ok(void) {
    g_last_error = "ok";
    g_last_status = 0;
    g_last_error_reg = 0;
}

static int error_ide(const char *msg, uint16_t io_base, uint8_t status) {
    g_last_error = msg;
    g_last_status = status;
    g_last_error_reg = inb(io_base + IDE_REG_ERROR);
    return -1;
}

static int error_ahci(const char *msg, volatile struct hba_port *port) {
    g_last_error = msg;
    g_last_status = (uint8_t)(port->tfd & 0xFFu);
    g_last_error_reg = (uint8_t)((port->tfd >> 8) & 0xFFu);
    return -1;
}

static inline void ide_delay_400ns(uint16_t io_base) {
    (void)inb(io_base + IDE_REG_ALTSTATUS);
    (void)inb(io_base + IDE_REG_ALTSTATUS);
    (void)inb(io_base + IDE_REG_ALTSTATUS);
    (void)inb(io_base + IDE_REG_ALTSTATUS);
}

static void extract_model_from_identify(const uint16_t id[256], char out[41]) {
    int i;
    int p = 0;

    for (i = 27; i <= 46; i++) {
        char hi = (char)(id[i] >> 8);
        char lo = (char)(id[i] & 0xFF);
        out[p++] = hi;
        out[p++] = lo;
    }
    out[40] = '\0';

    for (i = 39; i >= 0; i--) {
        if (out[i] == ' ' || out[i] == '\0') {
            out[i] = '\0';
        } else {
            break;
        }
    }
}

static void add_prefixed_model(char out[41], const char *prefix, const char *model) {
    size_t p = 0;
    size_t i = 0;

    while (prefix[p] != '\0' && p + 1 < 41) {
        out[p] = prefix[p];
        p++;
    }

    while (model[i] != '\0' && p + 1 < 41) {
        out[p++] = model[i++];
    }

    out[p] = '\0';
}

static int ide_wait_ready(uint16_t io_base) {
    uint8_t status = 0;
    int i;

    ide_delay_400ns(io_base);
    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        status = inb(io_base + IDE_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
                return error_ide("IDE ready wait error", io_base, status);
            }
            return 0;
        }
        io_wait();
    }

    return error_ide("IDE ready timeout", io_base, status);
}

static int ide_wait_drq(uint16_t io_base) {
    uint8_t status = 0;
    int i;

    ide_delay_400ns(io_base);
    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        status = inb(io_base + IDE_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return error_ide("IDE DRQ error", io_base, status);
        }
        if (status & ATA_SR_DF) {
            return error_ide("IDE device fault", io_base, status);
        }
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ) != 0) {
            return 0;
        }
        io_wait();
    }

    return error_ide("IDE DRQ timeout", io_base, status);
}

static int ide_wait_not_busy(uint16_t io_base) {
    uint8_t status = 0;
    int i;

    ide_delay_400ns(io_base);
    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        status = inb(io_base + IDE_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
                return error_ide("IDE completion error", io_base, status);
            }
            return 0;
        }
        io_wait();
    }

    return error_ide("IDE completion timeout", io_base, status);
}

static int ide_probe_device(int index) {
    struct ide_device *dev;
    uint8_t status;
    uint8_t lba1;
    uint8_t lba2;
    int i;

    if (index < 0 || index >= ATA_MAX_IDE_DEVICES) {
        return -1;
    }

    dev = &g_ide_devices[index];
    dev->present = 0;
    dev->sectors28 = 0;
    kmemset(dev->model, 0, sizeof(dev->model));
    kmemset(dev->identify, 0, sizeof(dev->identify));

    outb(dev->io_base + IDE_REG_HDDEVSEL, (uint8_t)(0xA0 | (dev->slave << 4)));
    ide_delay_400ns(dev->io_base);

    outb(dev->io_base + IDE_REG_SECCOUNT0, 0);
    outb(dev->io_base + IDE_REG_LBA0, 0);
    outb(dev->io_base + IDE_REG_LBA1, 0);
    outb(dev->io_base + IDE_REG_LBA2, 0);
    outb(dev->io_base + IDE_REG_COMMAND, ATA_CMD_IDENTIFY);

    status = inb(dev->io_base + IDE_REG_STATUS);
    if (status == 0) {
        return -1;
    }

    while ((status & ATA_SR_BSY) != 0) {
        status = inb(dev->io_base + IDE_REG_STATUS);
    }

    lba1 = inb(dev->io_base + IDE_REG_LBA1);
    lba2 = inb(dev->io_base + IDE_REG_LBA2);
    if (lba1 != 0 || lba2 != 0) {
        return -1;
    }

    if (ide_wait_drq(dev->io_base) != 0) {
        return -1;
    }

    for (i = 0; i < 256; i++) {
        dev->identify[i] = inw(dev->io_base + IDE_REG_DATA);
    }

    dev->sectors28 = (uint32_t)dev->identify[60] | ((uint32_t)dev->identify[61] << 16);
    extract_model_from_identify(dev->identify, dev->model);
    dev->present = 1;
    return 0;
}

static int ide_read_sector(int index, uint32_t lba, uint8_t *buffer512) {
    struct ide_device *dev;
    int i;

    if (index < 0 || index >= ATA_MAX_IDE_DEVICES || buffer512 == 0) {
        g_last_error = "IDE invalid read request";
        return -1;
    }

    dev = &g_ide_devices[index];
    if (!dev->present) {
        g_last_error = "IDE device missing";
        return -1;
    }

    if (ide_wait_ready(dev->io_base) != 0) {
        return -1;
    }

    outb(dev->io_base + IDE_REG_HDDEVSEL, (uint8_t)(0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F)));
    ide_delay_400ns(dev->io_base);
    outb(dev->io_base + IDE_REG_SECCOUNT0, 1);
    outb(dev->io_base + IDE_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(dev->io_base + IDE_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(dev->io_base + IDE_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(dev->io_base + IDE_REG_COMMAND, 0x20);
    ide_delay_400ns(dev->io_base);

    if (ide_wait_drq(dev->io_base) != 0) {
        return -1;
    }

    for (i = 0; i < 256; i++) {
        uint16_t w = inw(dev->io_base + IDE_REG_DATA);
        buffer512[i * 2] = (uint8_t)(w & 0xFF);
        buffer512[i * 2 + 1] = (uint8_t)(w >> 8);
    }

    error_ok();
    return 0;
}

static int ide_write_sector(int index, uint32_t lba, const uint8_t *buffer512) {
    struct ide_device *dev;
    int i;

    if (index < 0 || index >= ATA_MAX_IDE_DEVICES || buffer512 == 0) {
        g_last_error = "IDE invalid write request";
        return -1;
    }

    dev = &g_ide_devices[index];
    if (!dev->present) {
        g_last_error = "IDE device missing";
        return -1;
    }

    if (ide_wait_ready(dev->io_base) != 0) {
        return -1;
    }

    outb(dev->io_base + IDE_REG_HDDEVSEL, (uint8_t)(0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F)));
    ide_delay_400ns(dev->io_base);
    outb(dev->io_base + IDE_REG_SECCOUNT0, 1);
    outb(dev->io_base + IDE_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(dev->io_base + IDE_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(dev->io_base + IDE_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(dev->io_base + IDE_REG_COMMAND, 0x30);
    ide_delay_400ns(dev->io_base);

    if (ide_wait_drq(dev->io_base) != 0) {
        return -1;
    }

    for (i = 0; i < 256; i++) {
        uint16_t w = (uint16_t)buffer512[i * 2] | (uint16_t)((uint16_t)buffer512[i * 2 + 1] << 8);
        outw(dev->io_base + IDE_REG_DATA, w);
    }

    if (ide_wait_not_busy(dev->io_base) != 0) {
        return -1;
    }

    error_ok();
    return 0;
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address =
        0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address =
        0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int ahci_stop_port(volatile struct hba_port *port) {
    int i;

    port->cmd &= ~(HBA_PxCMD_ST | HBA_PxCMD_FRE);
    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        uint32_t cmd = port->cmd;
        if ((cmd & (HBA_PxCMD_CR | HBA_PxCMD_FR)) == 0) {
            return 0;
        }
        io_wait();
    }

    return error_ahci("AHCI stop timeout", port);
}

static int ahci_start_port(volatile struct hba_port *port) {
    int i;

    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        if ((port->cmd & HBA_PxCMD_CR) == 0) {
            break;
        }
        io_wait();
    }
    if ((port->cmd & HBA_PxCMD_CR) != 0) {
        return error_ahci("AHCI CR clear timeout", port);
    }

    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
    return 0;
}

static int ahci_setup_port(int index) {
    struct ahci_device *dev = &g_ahci_devices[index];
    struct ahci_port_ctx *ctx = &g_ahci_ctx[index];
    uintptr_t clb;
    uintptr_t fb;

    if (ahci_stop_port(dev->port) != 0) {
        return -1;
    }

    kmemset(ctx->cmd_list, 0, sizeof(ctx->cmd_list));
    kmemset(ctx->fis, 0, sizeof(ctx->fis));
    kmemset(ctx->cmd_table, 0, sizeof(ctx->cmd_table));
    kmemset(ctx->identify, 0, sizeof(ctx->identify));

    clb = (uintptr_t)ctx->cmd_list;
    fb = (uintptr_t)ctx->fis;
    if ((clb >> 32) != 0 || (fb >> 32) != 0) {
        return error_ahci("AHCI high memory not supported", dev->port);
    }

    dev->port->clb = (uint32_t)clb;
    dev->port->clbu = 0;
    dev->port->fb = (uint32_t)fb;
    dev->port->fbu = 0;
    dev->port->is = 0xFFFFFFFFu;
    dev->port->serr = 0xFFFFFFFFu;

    if (ahci_start_port(dev->port) != 0) {
        return -1;
    }

    return 0;
}

static int ahci_wait_slot_free(volatile struct hba_port *port, uint32_t mask) {
    int i;
    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        if (((port->ci | port->sact) & mask) == 0) {
            return 0;
        }
        io_wait();
    }
    return error_ahci("AHCI slot busy timeout", port);
}

static int ahci_issue(int index, uint8_t command, uint32_t lba, uint16_t transfer_count, uint16_t fis_count, int write, uint8_t device_reg, uint8_t *buffer) {
    struct ahci_device *dev = &g_ahci_devices[index];
    struct ahci_port_ctx *ctx = &g_ahci_ctx[index];
    struct hba_cmd_header *cmd_header;
    struct hba_cmd_tbl *cmd_tbl;
    struct fis_reg_h2d *fis;
    uintptr_t ctba;
    uintptr_t dba;
    int i;
    const uint32_t slot_mask = 1u;

    if (!dev->present) {
        g_last_error = "AHCI device missing";
        return -1;
    }
    if (buffer == 0 || transfer_count == 0) {
        g_last_error = "AHCI invalid request";
        return -1;
    }

    if (ahci_wait_slot_free(dev->port, slot_mask) != 0) {
        return -1;
    }

    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        uint8_t tfd = (uint8_t)(dev->port->tfd & 0xFFu);
        if ((tfd & (ATA_SR_BSY | ATA_SR_DRQ)) == 0) {
            break;
        }
        io_wait();
    }
    if ((((uint8_t)(dev->port->tfd & 0xFFu)) & (ATA_SR_BSY | ATA_SR_DRQ)) != 0) {
        return error_ahci("AHCI port busy timeout", dev->port);
    }

    cmd_header = (struct hba_cmd_header *)ctx->cmd_list;
    kmemset(cmd_header, 0, sizeof(*cmd_header));
    cmd_header->flags = (uint8_t)(sizeof(struct fis_reg_h2d) / sizeof(uint32_t));
    if (write) {
        cmd_header->flags |= (1u << 6);
    }
    cmd_header->prdtl = 1;

    cmd_tbl = (struct hba_cmd_tbl *)ctx->cmd_table;
    kmemset(cmd_tbl, 0, sizeof(*cmd_tbl));

    dba = (uintptr_t)buffer;
    if ((dba >> 32) != 0) {
        return error_ahci("AHCI DMA buffer above 4G", dev->port);
    }
    cmd_tbl->prdt_entry[0].dba = (uint32_t)dba;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc_i = ((uint32_t)transfer_count * 512u) - 1u;

    ctba = (uintptr_t)cmd_tbl;
    if ((ctba >> 32) != 0) {
        return error_ahci("AHCI command table above 4G", dev->port);
    }
    cmd_header->ctba = (uint32_t)ctba;
    cmd_header->ctbau = 0;

    fis = (struct fis_reg_h2d *)cmd_tbl->cfis;
    kmemset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 1u << 7;
    fis->command = command;
    fis->device = device_reg;
    fis->lba0 = (uint8_t)(lba & 0xFFu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFFu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFFu);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFFu);
    fis->lba4 = 0;
    fis->lba5 = 0;
    fis->countl = (uint8_t)(fis_count & 0xFFu);
    fis->counth = (uint8_t)((fis_count >> 8) & 0xFFu);

    dev->port->is = 0xFFFFFFFFu;
    dev->port->ci = slot_mask;

    for (i = 0; i < ATA_POLL_ATTEMPTS; i++) {
        if ((dev->port->ci & slot_mask) == 0) {
            if ((dev->port->is & HBA_PxIS_TFES) != 0) {
                return error_ahci("AHCI task file error", dev->port);
            }
            error_ok();
            return 0;
        }
        if ((dev->port->is & HBA_PxIS_TFES) != 0) {
            return error_ahci("AHCI task file error", dev->port);
        }
        io_wait();
    }

    return error_ahci("AHCI command timeout", dev->port);
}

static int ahci_identify(int index) {
    struct ahci_device *dev = &g_ahci_devices[index];
    struct ahci_port_ctx *ctx = &g_ahci_ctx[index];
    uint16_t *id;
    char model[41];

    if (ahci_issue(index, ATA_CMD_IDENTIFY, 0, 1, 0, 0, 0, ctx->identify) != 0) {
        return -1;
    }

    id = (uint16_t *)ctx->identify;
    dev->sectors28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    extract_model_from_identify(id, model);
    if (model[0] == '\0') {
        kstrcpy(model, "SATA Disk");
    }
    add_prefixed_model(dev->model, "AHCI ", model);

    return 0;
}

static int ahci_register_port(volatile struct hba_port *port, uint8_t port_no) {
    struct ahci_device *dev;
    int index;

    if (g_ahci_count >= ATA_MAX_AHCI_DEVICES) {
        return -1;
    }

    index = g_ahci_count;
    dev = &g_ahci_devices[index];
    kmemset(dev, 0, sizeof(*dev));
    dev->port = port;
    dev->port_no = port_no;

    if (ahci_setup_port(index) != 0) {
        return -1;
    }
    if (ahci_identify(index) != 0) {
        dev->sectors28 = 0x0FFFFFFFu;
        kstrcpy(dev->model, "AHCI Disk");
    }

    dev->present = 1;
    g_ahci_count++;
    return 0;
}

static void ahci_probe_controller(uint8_t bus, uint8_t slot, uint8_t func, int probe_mode) {
    uint32_t id = pci_read32(bus, slot, func, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xFFFFu);
    uint32_t class_reg;
    uint8_t class_code;
    uint8_t subclass;
    uint32_t command_reg;
    uint32_t bar5;
    uintptr_t abar;
    volatile struct hba_mem *hba;
    uint32_t ports;
    int port_no;

    if (vendor == 0xFFFFu) {
        return;
    }

    class_reg = pci_read32(bus, slot, func, 0x08);
    class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
    subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
    if (probe_mode >= 0 && class_code != AHCI_CLASS_MASS_STORAGE) {
        return;
    }
    if (probe_mode > 0 && subclass != AHCI_SUBCLASS_SATA) {
        return;
    }

    g_ahci_controllers_seen++;

    command_reg = pci_read32(bus, slot, func, 0x04);
    command_reg |= (1u << 1) | (1u << 2);
    pci_write32(bus, slot, func, 0x04, command_reg);

    bar5 = pci_read32(bus, slot, func, 0x24);
    if (bar5 == 0 || (bar5 & 1u) != 0) {
        return;
    }
    if ((bar5 & 0x6u) == 0x4u) {
        uint32_t bar5_hi = pci_read32(bus, slot, func, 0x28);
        if (bar5_hi != 0) {
            g_ahci_bar64_rejected++;
            return;
        }
    }

    abar = (uintptr_t)(bar5 & ~0x0Fu);
    if (abar == 0) {
        return;
    }

    hba = (volatile struct hba_mem *)abar;
    hba->ghc |= (1u << 31);

    ports = hba->pi;
    for (port_no = 0; port_no < 32; port_no++) {
        volatile struct hba_port *port;
        if ((ports & (1u << port_no)) == 0) {
            continue;
        }
        if (g_ahci_count >= ATA_MAX_AHCI_DEVICES) {
            return;
        }
        port = &hba->ports[port_no];

        /* Check physical link: DET=3 (device present), IPM=1 (active) */
        {
            uint32_t ssts = port->ssts;
            uint8_t det = (uint8_t)(ssts & 0xFu);
            uint8_t ipm = (uint8_t)((ssts >> 8) & 0xFu);
            if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE) {
                continue;
            }
        }
        /* Accept ATA (0x101), uninitialised (0 or 0xFFFFFFFF), skip ATAPI etc. */
        if (port->sig != SATA_SIG_ATA &&
            port->sig != 0u &&
            port->sig != 0xFFFFFFFFu) {
            continue;
        }
        g_ahci_ports_considered++;
        (void)ahci_register_port(port, (uint8_t)port_no);
    }
}

static void ahci_scan_controllers(void) {
    uint16_t bus;
    uint8_t slot;
    uint8_t func;

    g_ahci_count = 0;
    g_ahci_controllers_seen = 0;
    g_ahci_bar64_rejected = 0;
    g_ahci_ports_considered = 0;
    kmemset(g_ahci_devices, 0, sizeof(g_ahci_devices));

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (func = 0; func < 8; func++) {
                ahci_probe_controller((uint8_t)bus, slot, func, 1);
            }
        }
    }

    if (g_ahci_count == 0) {
        /* Fallback for VMs where subclass is reported unexpectedly. */
        ahci_probe_controller(0, 13, 0, -1);
        ahci_probe_controller(0, 31, 2, -1);
        ahci_probe_controller(0, 31, 5, -1);
    }
}

static void clear_device_map(void) {
    kmemset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
}

static void add_device_map(uint8_t kind, uint8_t source_index, uint16_t io_base, uint8_t slave, uint32_t sectors28, const char *model) {
    struct device_map *entry;

    if (g_device_count >= ATA_MAX_DEVICES) {
        return;
    }

    entry = &g_devices[g_device_count];
    entry->present = 1;
    entry->kind = kind;
    entry->source_index = source_index;
    entry->io_base = io_base;
    entry->slave = slave;
    entry->sectors28 = sectors28;
    if (model != 0) {
        kstrcpy(entry->model, model);
    } else {
        entry->model[0] = '\0';
    }
    g_device_count++;
}

int ata_scan(void) {
    int i;
    uint32_t pci_host_id;

    g_selected_device = -1;
    clear_device_map();

    for (i = 0; i < ATA_MAX_IDE_DEVICES; i++) {
        if (ide_probe_device(i) == 0 && g_ide_devices[i].present) {
            add_device_map(
                DEV_IDE,
                (uint8_t)i,
                g_ide_devices[i].io_base,
                g_ide_devices[i].slave,
                g_ide_devices[i].sectors28,
                g_ide_devices[i].model
            );
        }
    }

    pci_host_id = pci_read32(0, 0, 0, 0x00);
    g_dbg_pci_host_id = pci_host_id;
    g_dbg_slot13_id = pci_read32(0, 13, 0, 0x00);
    g_dbg_slot13_class = pci_read32(0, 13, 0, 0x08);
    g_dbg_slot13_bar5 = pci_read32(0, 13, 0, 0x24);
    if ((pci_host_id & 0xFFFFu) != 0xFFFFu) {
        ahci_scan_controllers();
    }
    for (i = 0; i < g_ahci_count; i++) {
        if (g_ahci_devices[i].present) {
            add_device_map(
                DEV_AHCI,
                (uint8_t)i,
                0,
                0,
                g_ahci_devices[i].sectors28,
                g_ahci_devices[i].model
            );
        }
    }

    if (g_device_count > 0) {
        g_selected_device = 0;
        error_ok();
    } else {
        if (g_ahci_controllers_seen > 0 && g_ahci_count == 0) {
            if (g_ahci_bar64_rejected > 0) {
                g_last_error = "AHCI BAR above 4G unsupported";
            } else if (g_ahci_ports_considered == 0) {
                g_last_error = "AHCI found but no disk ports";
            } else {
                g_last_error = "AHCI identify failed";
            }
        } else if ((pci_host_id & 0xFFFFu) == 0xFFFFu) {
            g_last_error = "PCI config space unavailable";
        } else {
            g_last_error = "no supported disk controller";
        }
    }

    return g_device_count;
}

int ata_get_info(int index, struct ata_device_info *out) {
    struct device_map *entry;

    if (out == 0 || index < 0 || index >= g_device_count) {
        return -1;
    }

    entry = &g_devices[index];
    if (!entry->present) {
        return -1;
    }

    out->present = entry->present;
    out->index = index;
    out->interface_type = (entry->kind == DEV_AHCI) ? ATA_IFACE_AHCI : ATA_IFACE_IDE;
    out->io_base = entry->io_base;
    out->slave = entry->slave;
    out->sectors28 = entry->sectors28;
    kstrcpy(out->model, entry->model);
    return 0;
}

int ata_select_device(int index) {
    if (index < 0 || index >= g_device_count) {
        return -1;
    }
    if (!g_devices[index].present) {
        return -1;
    }
    g_selected_device = index;
    return 0;
}

int ata_selected_device(void) {
    return g_selected_device;
}

int ata_read_sector(uint32_t lba, uint8_t *buffer512) {
    struct device_map *entry;

    if (g_selected_device < 0 || g_selected_device >= g_device_count) {
        g_last_error = "no selected disk";
        return -1;
    }
    entry = &g_devices[g_selected_device];
    if (!entry->present) {
        g_last_error = "selected disk missing";
        return -1;
    }

    if (entry->kind == DEV_IDE) {
        return ide_read_sector((int)entry->source_index, lba, buffer512);
    }
    if (entry->kind == DEV_AHCI) {
        return ahci_issue((int)entry->source_index, ATA_CMD_READ_DMA_EXT, lba, 1, 1, 0, ATA_DEV_LBA, buffer512);
    }

    g_last_error = "unsupported disk interface";
    return -1;
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer512) {
    struct device_map *entry;

    if (g_selected_device < 0 || g_selected_device >= g_device_count) {
        g_last_error = "no selected disk";
        return -1;
    }
    entry = &g_devices[g_selected_device];
    if (!entry->present) {
        g_last_error = "selected disk missing";
        return -1;
    }

    if (entry->kind == DEV_IDE) {
        return ide_write_sector((int)entry->source_index, lba, buffer512);
    }
    if (entry->kind == DEV_AHCI) {
        return ahci_issue((int)entry->source_index, ATA_CMD_WRITE_DMA_EXT, lba, 1, 1, 1, ATA_DEV_LBA, (uint8_t *)buffer512);
    }

    g_last_error = "unsupported disk interface";
    return -1;
}

static int ide_flush_cache(int index) {
    struct ide_device *dev;

    if (index < 0 || index >= ATA_MAX_IDE_DEVICES) {
        g_last_error = "IDE invalid flush request";
        return -1;
    }
    dev = &g_ide_devices[index];
    if (!dev->present) {
        g_last_error = "IDE device missing";
        return -1;
    }
    if (ide_wait_ready(dev->io_base) != 0) {
        return -1;
    }
    outb(dev->io_base + IDE_REG_HDDEVSEL, (uint8_t)(0xA0 | (dev->slave << 4)));
    ide_delay_400ns(dev->io_base);
    outb(dev->io_base + IDE_REG_COMMAND, 0xE7u); /* ATA FLUSH CACHE */
    if (ide_wait_not_busy(dev->io_base) != 0) {
        return -1;
    }
    error_ok();
    return 0;
}

int ata_flush(void) {
    struct device_map *entry;

    if (g_selected_device < 0 || g_selected_device >= g_device_count) {
        g_last_error = "no selected disk";
        return -1;
    }
    entry = &g_devices[g_selected_device];
    if (!entry->present) {
        g_last_error = "selected disk missing";
        return -1;
    }
    if (entry->kind == DEV_IDE) {
        return ide_flush_cache((int)entry->source_index);
    }
    /* AHCI: DMA operations complete before returning, no extra flush needed */
    error_ok();
    return 0;
}

const char *ata_last_error(void) {
    return g_last_error;
}

uint8_t ata_last_status(void) {
    return g_last_status;
}

uint8_t ata_last_error_reg(void) {
    return g_last_error_reg;
}

void ata_debug_state(struct ata_debug_state *out) {
    if (out == 0) {
        return;
    }

    out->pci_host_id = g_dbg_pci_host_id;
    out->pci_ahci_slot_id = g_dbg_slot13_id;
    out->pci_ahci_slot_class = g_dbg_slot13_class;
    out->pci_ahci_slot_bar5 = g_dbg_slot13_bar5;
    out->ahci_controllers_seen = g_ahci_controllers_seen;
    out->ahci_ports_considered = g_ahci_ports_considered;
    out->ahci_devices_found = g_ahci_count;
}
