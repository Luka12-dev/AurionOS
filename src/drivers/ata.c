/*
 * ATA/IDE Disk Driver for Aurion OS
 * Supports PIO mode LBA28 on all 4 IDE channels (primary/secondary × master/slave)
 * Compatible with real hardware, VirtualBox, VMware, QEMU
*/

#include <stdint.h>
#include <stdbool.h>

/* Port I/O */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile("cld; rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile("cld; rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

/* ATA I/O Base Addresses */
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170
#define ATA_SECONDARY_CTRL  0x376

/* ATA Register Offsets */
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT    0x02
#define ATA_REG_LBA_LO      0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HI      0x05
#define ATA_REG_DRIVE       0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_COMMAND     0x07

/* ATA Commands */
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_PACKET      0xA0

/* SCSI Commands (used inside ATAPI packets) */
#define SCSI_READ_10        0x28

/* CD-ROM sector size */
#define CDROM_SECTOR_SIZE   2048

/* ATA Status Bits */
#define ATA_SR_BSY          0x80
#define ATA_SR_DRDY         0x40
#define ATA_SR_DF           0x20
#define ATA_SR_DRQ          0x08
#define ATA_SR_ERR          0x01

/* Timeouts */
#define ATA_TIMEOUT_READY   200000
#define ATA_TIMEOUT_DRQ     500000
#define ATA_TIMEOUT_IDENT   200000
#define ATA_TIMEOUT_FLUSH   500000

/* Drive Descriptor */
typedef struct {
    uint16_t base;          /* I/O base port (0x1F0 or 0x170) */
    uint16_t ctrl;          /* Control port (0x3F6 or 0x376) */
    uint8_t  slave;         /* 0 = master, 1 = slave */
    bool     present;       /* Drive responded to IDENTIFY */
    bool     is_atapi;      /* ATAPI (CD-ROM) — not usable for disk I/O */
    char     model[41];     /* Model string from IDENTIFY data */
    uint32_t sectors;       /* Total addressable LBA28 sectors */
} AtaDrive;

#define ATA_MAX_DRIVES 4

static AtaDrive ata_drives[ATA_MAX_DRIVES];
static int      ata_drive_count = 0;
static int      ata_boot_drive  = -1;
static int      ata_cdrom_drive = -1;  /* First ATAPI (CD-ROM) drive */
static bool     ata_initialized = false;

extern void c_puts(const char *s);

/* Low-Level Helpers */

/* 400ns I/O delay by reading the alternate status register 4 times */
static void ata_delay(uint16_t ctrl) {
    inb(ctrl); inb(ctrl); inb(ctrl); inb(ctrl);
}

/* Poll until BSY clears. Returns 0 on success, -1 on timeout. */
static int ata_wait_ready(uint16_t base, int timeout) {
    while (timeout-- > 0) {
        if (!(inb(base + ATA_REG_STATUS) & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

/* Poll until DRQ is set (and BSY clear). Returns -1 on error/timeout. */
static int ata_wait_drq(uint16_t base, int timeout) {
    while (timeout-- > 0) {
        uint8_t s = inb(base + ATA_REG_STATUS);
        if (s & (ATA_SR_ERR | ATA_SR_DF))
            return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ))
            return 0;
    }
    return -1;
}

/* Software reset an IDE bus via the Device Control Register */
static void ata_bus_reset(uint16_t ctrl) {
    outb(ctrl, 0x04);       /* Assert SRST */
    ata_delay(ctrl);
    ata_delay(ctrl);
    outb(ctrl, 0x00);       /* De-assert SRST */
    ata_delay(ctrl);
    ata_delay(ctrl);
}

/* IDENTIFY a Single Drive */
static bool ata_identify_drive(uint16_t base, uint16_t ctrl,
                                uint8_t slave, AtaDrive *drv)
{
    drv->base     = base;
    drv->ctrl     = ctrl;
    drv->slave    = slave;
    drv->present  = false;
    drv->is_atapi = false;
    drv->model[0] = 0;
    drv->sectors  = 0;

    /* Select drive */
    outb(base + ATA_REG_DRIVE, 0xA0 | (slave << 4));
    for (volatile int i = 0; i < 50000; i++); 
    ata_delay(ctrl);

    /* Check status - skip if bus is truly empty (0xFF) */
    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0xFF) return false;

    /* Issue IDENTIFY */
    outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    for (volatile int i = 0; i < 50000; i++);
    ata_delay(ctrl);

    status = inb(base + ATA_REG_STATUS);
    
    /* If IDENTIFY failed (ERR) or drive is NOT standard ATA (e.g. ATAPI), 
       check the LBA_MID/HI signature registers. */
    uint8_t lba_mid = inb(base + ATA_REG_LBA_MID);
    uint8_t lba_hi  = inb(base + ATA_REG_LBA_HI);

    /* Wait for BSY to clear or time out */
    int timeout = ATA_TIMEOUT_IDENT;
    while (timeout-- > 0) {
        status = inb(base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) break;
    }

    if (lba_mid == 0x14 && lba_hi == 0xEB) {
        /* PATAPI — CD-ROM or similar */
        drv->present  = true;
        drv->is_atapi = true;
        
        /* Optional: Send ATAPI IDENTIFY (0xA1) to get the model string */
        outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        for (volatile int j = 0; j < 50000; j++); 
        
        timeout = ATA_TIMEOUT_IDENT * 2;
        while (timeout-- > 0) {
            status = inb(base + ATA_REG_STATUS);
            if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
        }
        
        if (timeout > 0) {
            uint16_t ident[256];
            insw(base + ATA_REG_DATA, ident, 256);
            int mi = 0;
            for (int w = 27; w <= 46; w++) {
                drv->model[mi++] = (char)(ident[w] >> 8);
                drv->model[mi++] = (char)(ident[w] & 0xFF);
            }
            drv->model[40] = 0;
        } else {
            for (int i = 0; i < 40; i++) drv->model[i] = "ATAPI Drive"[i];
        }
        return true;
    }
    
    if (lba_mid == 0x3C && lba_hi == 0xC3) {
        drv->present  = true;
        drv->is_atapi = true;
        for (int i = 0; i < 40; i++) drv->model[i] = "SATA-Bridge Drive"[i];
        return true;
    }
    
    if (lba_mid != 0 || lba_hi != 0) return false;

    /* Standard ATA Drive - must have DRQ now */
    if (!(status & ATA_SR_DRQ)) {
        timeout = ATA_TIMEOUT_IDENT;
        while (timeout-- > 0) {
            status = inb(base + ATA_REG_STATUS);
            if (status & ATA_SR_DRQ) break;
        }
        if (timeout <= 0) return false;
    }

    /* Read 256 words of IDENTIFY data */
    uint16_t ident[256];
    insw(base + ATA_REG_DATA, ident, 256);

    /* Extract model string from words 27–46 (byte-swapped) */
    int mi = 0;
    for (int w = 27; w <= 46; w++) {
        drv->model[mi++] = (char)(ident[w] >> 8);
        drv->model[mi++] = (char)(ident[w] & 0xFF);
    }
    drv->model[40] = 0;

    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && drv->model[i] == ' '; i--)
        drv->model[i] = 0;

    /* Total LBA28 sector count from words 60–61 */
    drv->sectors = (uint32_t)ident[60] | ((uint32_t)ident[61] << 16);

    drv->present  = true;
    drv->is_atapi = false;
    return true;
}

/* Helper: print decimal number */
static void ata_print_uint(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        c_puts("0");
        return;
    }
    /* Build digits in reverse */
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    /* Print in correct order */
    while (i > 0)  {
        char c[2] = { buf[--i], 0 };
        c_puts(c);
    }
}

/* Public: Initialize ATA Subsystem */
int ata_init(void) {
    if (ata_initialized)
        return (ata_boot_drive >= 0) ? 0 : -1;

    ata_drive_count = 0;
    ata_boot_drive  = -1;

    static const uint16_t bases[]      = { ATA_PRIMARY_BASE, ATA_SECONDARY_BASE };
    static const uint16_t ctrls[]      = { ATA_PRIMARY_CTRL, ATA_SECONDARY_CTRL };
    static const char *bus_names[]     = { "Primary",  "Secondary" };
    static const char *role_names[]    = { "Master",   "Slave"     };

    for (int bus = 0; bus < 2; bus++) {
        ata_bus_reset(ctrls[bus]);

        for (int role = 0; role < 2; role++) {
            if (ata_drive_count >= ATA_MAX_DRIVES)
                break;

            AtaDrive *d = &ata_drives[ata_drive_count];

            if (!ata_identify_drive(bases[bus], ctrls[bus], (uint8_t)role, d))
                continue;

            /* Log what we found */
            c_puts("[ATA] Found ");
            c_puts(bus_names[bus]);
            c_puts(" ");
            c_puts(role_names[role]);
            c_puts(": ");

            if (d->is_atapi) {
                c_puts("ATAPI device (");
                c_puts(d->model);
                c_puts(")\n");
            } else {
                c_puts(d->model);
                c_puts(" (");
                ata_print_uint(d->sectors / 2048);
                c_puts(" MB)\n");
            }

            /* First non-ATAPI drive becomes the boot disk */
            if (!d->is_atapi && ata_boot_drive < 0)
                ata_boot_drive = ata_drive_count;

            /* First ATAPI drive becomes the CD-ROM */
            if (d->is_atapi && ata_cdrom_drive < 0)
                ata_cdrom_drive = ata_drive_count;

            ata_drive_count++;
        }
    }

    ata_initialized = true;
    
    /* If no drives found, try one more time with longer delays */
    if (ata_boot_drive < 0) {
        c_puts("[ATA] No drives found on first scan, retrying...\n");
        ata_initialized = false;
        ata_drive_count = 0;
        
        /* Wait a bit longer */
        for (volatile int i = 0; i < 100000; i++);
        
        /* Try again */
        for (int bus = 0; bus < 2; bus++) {
            ata_bus_reset(ctrls[bus]);
            
            for (volatile int i = 0; i < 50000; i++);

            for (int role = 0; role < 2; role++) {
                if (ata_drive_count >= ATA_MAX_DRIVES)
                    break;

                AtaDrive *d = &ata_drives[ata_drive_count];

                if (!ata_identify_drive(bases[bus], ctrls[bus], (uint8_t)role, d))
                    continue;

                if (!d->is_atapi && ata_boot_drive < 0)
                    ata_boot_drive = ata_drive_count;
                if (d->is_atapi && ata_cdrom_drive < 0)
                    ata_cdrom_drive = ata_drive_count;

                ata_drive_count++;
            }
        }
        
        ata_initialized = true;
    }
    
    return (ata_boot_drive >= 0) ? 0 : -1;
}

/* Public: Read Sectors (LBA28 PIO) */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!ata_initialized && ata_init() != 0)
        return -1;
    if (ata_boot_drive < 0 || count == 0)
        return -1;

    AtaDrive *d  = &ata_drives[ata_boot_drive];
    uint16_t base = d->base;
    uint16_t ctrl = d->ctrl;

    if (d->sectors > 0 && lba + count > d->sectors)
        return -1;

    if (ata_wait_ready(base, ATA_TIMEOUT_READY) != 0)
        return -1;

    /* Select drive, LBA mode, upper 4 LBA bits */
    outb(base + ATA_REG_DRIVE,
         0xE0 | (d->slave << 4) | ((lba >> 24) & 0x0F));
    ata_delay(ctrl);

    outb(base + ATA_REG_FEATURES, 0x00);
    outb(base + ATA_REG_SECCOUNT, count);
    outb(base + ATA_REG_LBA_LO,  (uint8_t)(lba));
    outb(base + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(base + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));

    outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ata_wait_drq(base, ATA_TIMEOUT_DRQ) != 0)
            return -1;

        insw(base + ATA_REG_DATA, buf, 256);
        buf += 256;
        ata_delay(ctrl);
    }

    return 0;
}

/* Public: Write Sectors (LBA28 PIO) */
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    if (!ata_initialized && ata_init() != 0)
        return -1;
    if (ata_boot_drive < 0 || count == 0)
        return -1;

    AtaDrive *d   = &ata_drives[ata_boot_drive];
    uint16_t base = d->base;
    uint16_t ctrl = d->ctrl;

    if (d->sectors > 0 && lba + count > d->sectors)
        return -1;

    if (ata_wait_ready(base, ATA_TIMEOUT_READY) != 0)
        return -1;

    outb(base + ATA_REG_DRIVE,
         0xE0 | (d->slave << 4) | ((lba >> 24) & 0x0F));
    ata_delay(ctrl);

    outb(base + ATA_REG_FEATURES, 0x00);
    outb(base + ATA_REG_SECCOUNT, count);
    outb(base + ATA_REG_LBA_LO,  (uint8_t)(lba));
    outb(base + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(base + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));

    outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ata_wait_drq(base, ATA_TIMEOUT_DRQ) != 0)
            return -1;

        outsw(base + ATA_REG_DATA, buf, 256);
        buf += 256;
        ata_delay(ctrl);

        /* Wait for the drive to finish writing the sector */
        if (ata_wait_ready(base, ATA_TIMEOUT_READY) != 0)
            return -1;
    }

    /* Flush write cache to guarantee data hits the platters */
    outb(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_ready(base, ATA_TIMEOUT_FLUSH);
    /* Flush timeout is non-fatal — some drives don't support it */

    return 0;
}

/* Legacy Wrapper */
int disk_write_lba_ata(uint32_t lba, uint8_t count, const void *buffer) {
    return ata_write_sectors(lba, count, buffer);
}

/* ═══════════════════════════════════════════════════════════════════════
   ATAPI (CD-ROM) Read Support

   ATAPI uses SCSI commands sent via the ATA PACKET interface.
   CD-ROM sectors are 2048 bytes. We use READ(10) (opcode 0x28)
   to read one sector at a time.

   Protocol:
   1. Select the ATAPI drive
   2. Set byte count registers to expected transfer size (2048)
   3. Issue PACKET command (0xA0)
   4. Wait for DRQ, send 12-byte Command Descriptor Block (CDB)
   5. Wait for DRQ, read 2048 bytes of sector data
   ═══════════════════════════════════════════════════════════════════════ */

/* Read one 2048-byte CD-ROM sector via ATAPI READ(10).
 * lba = CD-ROM sector number (2048-byte sectors)
 * buffer must be at least 2048 bytes
 * Returns 0 on success, -1 on failure. */
static int atapi_read_sector(int drive_idx, uint32_t lba, void *buffer) {
    if (drive_idx < 0 || drive_idx >= ata_drive_count)
        return -1;

    AtaDrive *d = &ata_drives[drive_idx];
    if (!d->present || !d->is_atapi)
        return -1;

    uint16_t base = d->base;
    uint16_t ctrl = d->ctrl;

    /* Wait for drive ready */
    if (ata_wait_ready(base, ATA_TIMEOUT_READY) != 0)
        return -1;

    /* Select drive */
    outb(base + ATA_REG_DRIVE, 0xA0 | (d->slave << 4));
    ata_delay(ctrl);

    /* Set PIO transfer mode, byte count = 2048 */
    outb(base + ATA_REG_FEATURES, 0x00);     /* PIO mode */
    outb(base + ATA_REG_SECCOUNT, 0x00);     /* unused for PACKET */
    outb(base + ATA_REG_LBA_LO,   0x00);     /* unused for PACKET */
    outb(base + ATA_REG_LBA_MID,  (uint8_t)(CDROM_SECTOR_SIZE & 0xFF));
    outb(base + ATA_REG_LBA_HI,   (uint8_t)(CDROM_SECTOR_SIZE >> 8));

    /* Issue PACKET command */
    outb(base + ATA_REG_COMMAND, ATA_CMD_PACKET);

    /* Wait for BSY clear and DRQ set (drive wants the CDB) */
    if (ata_wait_drq(base, ATA_TIMEOUT_DRQ) != 0)
        return -1;

    /* Build 12-byte SCSI READ(10) Command Descriptor Block */
    uint8_t cdb[12];
    cdb[0]  = SCSI_READ_10;              /* Opcode */
    cdb[1]  = 0;
    cdb[2]  = (uint8_t)(lba >> 24);      /* LBA (big-endian) */
    cdb[3]  = (uint8_t)(lba >> 16);
    cdb[4]  = (uint8_t)(lba >> 8);
    cdb[5]  = (uint8_t)(lba);
    cdb[6]  = 0;
    cdb[7]  = 0;                         /* Transfer length (big-endian) */
    cdb[8]  = 1;                         /* Read 1 sector */
    cdb[9]  = 0;
    cdb[10] = 0;
    cdb[11] = 0;

    /* Send CDB as 6 words (12 bytes) */
    outsw(base + ATA_REG_DATA, cdb, 6);

    /* Wait for data to be ready */
    if (ata_wait_drq(base, ATA_TIMEOUT_DRQ) != 0)
        return -1;

    /* Read 2048 bytes (1024 words) */
    insw(base + ATA_REG_DATA, buffer, CDROM_SECTOR_SIZE / 2);

    /* Wait for BSY to clear (command complete) */
    ata_wait_ready(base, ATA_TIMEOUT_READY);

    return 0;
}

/* Public: Read from CD-ROM using 512-byte sector addressing.
 * This bridges the gap between the kernel's 512-byte LBA convention
 * and the CD-ROM's native 2048-byte sectors.
 *
 * lba_512 = sector number in 512-byte units
 * count   = number of 512-byte sectors to read
 * buffer  = output buffer (must be count * 512 bytes)
 * Returns 0 on success, -1 on failure. */
int ata_read_cdrom_512(uint32_t lba_512, uint32_t count, void *buffer) {
    if (!ata_initialized && ata_init() != 0)
        return -1;
    if (ata_cdrom_drive < 0)
        return -1;

    static uint8_t cd_sector_buf[CDROM_SECTOR_SIZE];
    uint8_t *out = (uint8_t *)buffer;
    uint32_t cached_cd_lba = 0xFFFFFFFF; /* impossible LBA = nothing cached */

    for (uint32_t i = 0; i < count; i++) {
        uint32_t cur_512 = lba_512 + i;
        uint32_t cd_lba  = cur_512 / 4;       /* which 2048-byte sector */
        uint32_t offset  = (cur_512 % 4) * 512; /* byte offset within it */

        /* Only read a new CD sector if we need a different one */
        if (cd_lba != cached_cd_lba) {
            /* Retry logic for QEMU/Slow emulators */
            bool read_ok = false;
            for (int retry = 0; retry < 3; retry++) {
                if (atapi_read_sector(ata_cdrom_drive, cd_lba, cd_sector_buf) == 0) {
                    read_ok = true;
                    break;
                }
                for (volatile int delay = 0; delay < 100000; delay++);
            }

            if (!read_ok) {
                c_puts("[ATA] ERROR: CD-ROM read failed LBA ");
                ata_print_uint(cd_lba);
                c_puts("\n");
                return -1;
            }
            
            cached_cd_lba = cd_lba;
        }

        /* Copy the relevant 512-byte slice */
        for (int b = 0; b < 512; b++)
            out[i * 512 + b] = cd_sector_buf[offset + b];
    }

    return 0;
}

/* Query Functions */
bool ata_is_available(void) {
    if (!ata_initialized) ata_init();
    return ata_boot_drive >= 0;
}

bool ata_cdrom_available(void) {
    if (!ata_initialized) ata_init();
    return ata_cdrom_drive >= 0;
}

int ata_get_drive_count(void) {
    if (!ata_initialized) ata_init();
    return ata_drive_count;
}

const char *ata_get_model(int index) {
    if (index < 0 || index >= ata_drive_count)
        return "Unknown";
    return ata_drives[index].model;
}

uint32_t ata_get_size_mb(int index) {
    if (index < 0 || index >= ata_drive_count)
        return 0;
    return ata_drives[index].sectors / 2048;
}