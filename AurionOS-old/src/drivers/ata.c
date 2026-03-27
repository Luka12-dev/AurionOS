/*
 * ATA/IDE Disk Driver for AurionOS
 * Works on real hardware, VirtualBox, VMware
 * 
 * This replaces VirtIO disk for compatibility
*/

#include <stdint.h>
#include <stdbool.h>
#include "portio.h"

/* ATA I/O Ports */
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170
#define ATA_SECONDARY_CTRL  0x376

/* ATA Registers (offsets from base) */
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
#define ATA_CMD_IDENTIFY    0xEC

/* ATA Status bits */
#define ATA_SR_BSY          0x80  /* Busy */
#define ATA_SR_DRDY         0x40  /* Drive Ready */
#define ATA_SR_DRQ          0x08  /* Data Request */
#define ATA_SR_ERR          0x01  /* Error */

/* ATA driver state */
static bool ata_initialized = false;
static uint16_t ata_base = ATA_PRIMARY_BASE;
static uint16_t ata_ctrl = ATA_PRIMARY_CTRL;
static uint8_t ata_drive = 0;  /* 0 = master, 1 = slave */

/* 400ns delay (read alternate status register 4 times) */
static void ata_delay(void) {
    inb(ata_ctrl);
    inb(ata_ctrl);
    inb(ata_ctrl);
    inb(ata_ctrl);
}

/* Wait for BSY to clear */
static int ata_wait_ready(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        uint8_t status = inb(ata_base + ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            return 0;
        }
    }
    return -1;  /* Timeout */
}

/* Wait for DRQ */
static int ata_wait_drq(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        uint8_t status = inb(ata_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;  /* Error */
        }
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -1;  /* Timeout */
}

/* Initialize ATA driver - detect drive */
int ata_init(void) {
    if (ata_initialized) return 0;
    
    /* Soft reset primary bus */
    outb(ata_ctrl, 0x04);  /* Set SRST */
    ata_delay();
    outb(ata_ctrl, 0x00);  /* Clear SRST */
    ata_delay();
    
    /* Wait for drive to be ready */
    if (ata_wait_ready() != 0) {
        /* Try secondary bus */
        ata_base = ATA_SECONDARY_BASE;
        ata_ctrl = ATA_SECONDARY_CTRL;
        
        outb(ata_ctrl, 0x04);
        ata_delay();
        outb(ata_ctrl, 0x00);
        ata_delay();
        
        if (ata_wait_ready() != 0) {
            return -1;  /* No drives found */
        }
    }
    
    /* Select master drive */
    outb(ata_base + ATA_REG_DRIVE, 0xA0);
    ata_delay();
    
    /* Check if drive exists */
    uint8_t status = inb(ata_base + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        /* Try slave */
        ata_drive = 1;
        outb(ata_base + ATA_REG_DRIVE, 0xB0);
        ata_delay();
        status = inb(ata_base + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF) {
            return -1;  /* No drives */
        }
    }
    
    ata_initialized = true;
    return 0;
}

/* Read sectors using LBA28 */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!ata_initialized) {
        if (ata_init() != 0) return -1;
    }
    
    if (count == 0) return -1;
    
    /* Wait for drive ready */
    if (ata_wait_ready() != 0) return -1;
    
    /* Select drive with LBA bit and upper 4 bits of LBA */
    uint8_t drive_select = 0xE0 | (ata_drive << 4) | ((lba >> 24) & 0x0F);
    outb(ata_base + ATA_REG_DRIVE, drive_select);
    ata_delay();
    
    /* Send sector count and LBA */
    outb(ata_base + ATA_REG_SECCOUNT, count);
    outb(ata_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ata_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    
    /* Send READ command */
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    /* Read sectors */
    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        /* Wait for data */
        if (ata_wait_drq() != 0) return -1;
        
        /* Read 256 words (512 bytes) */
        insw(ata_base + ATA_REG_DATA, buf, 256);
        buf += 256;
        
        ata_delay();
    }
    
    return 0;
}

/* Write sectors using LBA28 */
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    if (!ata_initialized) {
        if (ata_init() != 0) return -1;
    }
    
    if (count == 0) return -1;
    
    /* Wait for drive ready */
    if (ata_wait_ready() != 0) return -1;
    
    /* Select drive with LBA bit and upper 4 bits of LBA */
    uint8_t drive_select = 0xE0 | (ata_drive << 4) | ((lba >> 24) & 0x0F);
    outb(ata_base + ATA_REG_DRIVE, drive_select);
    ata_delay();
    
    /* Send sector count and LBA */
    outb(ata_base + ATA_REG_SECCOUNT, count);
    outb(ata_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ata_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    
    /* Send WRITE command */
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    /* Write sectors */
    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        /* Wait for ready */
        if (ata_wait_drq() != 0) return -1;
        
        /* Write 256 words (512 bytes) */
        outsw(ata_base + ATA_REG_DATA, buf, 256);
        buf += 256;
        
        ata_delay();
        
        /* Wait for write to complete */
        if (ata_wait_ready() != 0) return -1;
    }
    
    /* Flush cache (if supported) */
    outb(ata_base + ATA_REG_COMMAND, 0xE7);  /* CACHE FLUSH */
    ata_wait_ready();
    
    return 0;
}

/* Hook for the existing disk_read_lba/disk_write_lba functions */
int disk_read_lba_ata(uint32_t lba, uint8_t count, void *buffer) {
    return ata_read_sectors(lba, count, buffer);
}

int disk_write_lba_ata(uint32_t lba, uint8_t count, const void *buffer) {
    return ata_write_sectors(lba, count, buffer);
}
