/*
 * VMware SVGA II Driver for AurionOS
 * Supports dynamic resolution changes on VMware virtual machines
 * Only used when VMware SVGA hardware is detected
*/

#include <stdint.h>
#include <stdbool.h>
#include "../include/portio.h"
#include "vmware_svga.h"
#include "../pci.h"

/* VMware SVGA II PCI IDs */
#define VMWARE_VENDOR_ID    0x15AD
#define VMWARE_SVGA2_ID     0x0405

/* VMware SVGA II I/O ports (BAR0) */
#define SVGA_INDEX_PORT     0x0
#define SVGA_VALUE_PORT     0x1
#define SVGA_BIOS_PORT      0x2
#define SVGA_IRQ_PORT       0x8

/* SVGA registers */
#define SVGA_REG_ID                 0
#define SVGA_REG_ENABLE             1
#define SVGA_REG_WIDTH              2
#define SVGA_REG_HEIGHT             3
#define SVGA_REG_MAX_WIDTH          4
#define SVGA_REG_MAX_HEIGHT         5
#define SVGA_REG_DEPTH              6
#define SVGA_REG_BITS_PER_PIXEL     7
#define SVGA_REG_FB_START           13
#define SVGA_REG_FB_OFFSET          14
#define SVGA_REG_BYTES_PER_LINE     12
#define SVGA_REG_FB_SIZE            16
#define SVGA_REG_CAPABILITIES       17

/* SVGA capabilities */
#define SVGA_CAP_RECT_COPY          0x00000002

/* SVGA commands */
#define SVGA_CMD_UPDATE             1

static bool vmware_svga_present = false;
static uint16_t svga_iobase = 0;
static uint32_t svga_fb_addr = 0;
static uint32_t svga_fb_size = 0;

/* Write to SVGA register */
static void svga_write_reg(uint32_t index, uint32_t value) {
    outl(svga_iobase + SVGA_INDEX_PORT, index);
    outl(svga_iobase + SVGA_VALUE_PORT, value);
}

/* Read from SVGA register */
static uint32_t svga_read_reg(uint32_t index) {
    outl(svga_iobase + SVGA_INDEX_PORT, index);
    return inl(svga_iobase + SVGA_VALUE_PORT);
}

/* Detect VMware SVGA hardware */
bool vmware_svga_detect(void) {
    extern void c_puts(const char *s);
    extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
    extern void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
    
    /* c_puts("[VMSVGA] Scanning for VMware SVGA II...\n"); */
    
    /* Scan PCI bus for VMware SVGA device */
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_config_read(bus, dev, func, 0x00);
                uint16_t vendor = id & 0xFFFF;
                uint16_t device = (id >> 16) & 0xFFFF;
                
                if (vendor == 0xFFFF || vendor == 0x0000) continue;
                
                if (vendor == VMWARE_VENDOR_ID && device == VMWARE_SVGA2_ID) {
                    /* c_puts("[VMSVGA] Found VMware SVGA II device!\n"); */
                    
                    /* Read BAR0 (I/O base) */
                    uint32_t bar0 = pci_config_read(bus, dev, func, 0x10);
                    if (bar0 & 0x1) {
                        /* I/O space */
                        svga_iobase = bar0 & 0xFFFFFFF0;
                    } else {
                        c_puts("[VMSVGA] ERROR: BAR0 is not I/O space\n");
                        return false;
                    }
                    
                    /* Read BAR1 (framebuffer) */
                    uint32_t bar1 = pci_config_read(bus, dev, func, 0x14);
                    svga_fb_addr = bar1 & 0xFFFFFFF0;
                    
                    /* Enable PCI bus mastering and I/O space */
                    uint32_t cmd = pci_config_read(bus, dev, func, 0x04);
                    cmd |= 0x03; /* I/O space + Bus master */
                    pci_config_write(bus, dev, func, 0x04, cmd);
                    
                    /* Check SVGA version */
                    uint32_t id = svga_read_reg(SVGA_REG_ID);
                    if (id < 2) {
                        c_puts("[VMSVGA] ERROR: SVGA version too old\n");
                        return false;
                    }
                    
                    /* Get framebuffer info */
                    svga_fb_addr = svga_read_reg(SVGA_REG_FB_START);
                    svga_fb_size = svga_read_reg(SVGA_REG_FB_SIZE);
                    
                    vmware_svga_present = true;
                    /* c_puts("[VMSVGA] Driver initialized successfully\n"); */
                    return true;
                }
            }
        }
    }
    
    /* c_puts("[VMSVGA] VMware SVGA II not found\n"); */
    return false;
}

/* Check if VMware SVGA is available */
bool vmware_svga_available(void) {
    return vmware_svga_present;
}

/* Disable VMware SVGA (for text mode) */
void vmware_svga_disable(void) {
    if (!vmware_svga_present) return;
    
    /* Disable SVGA mode */
    svga_write_reg(SVGA_REG_ENABLE, 0);
    
    /* Reset to VGA compatibility mode */
    svga_write_reg(SVGA_REG_WIDTH, 0);
    svga_write_reg(SVGA_REG_HEIGHT, 0);
}

/* Set video mode */
bool vmware_svga_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    extern void c_puts(const char *s);
    
    if (!vmware_svga_present) return false;
    
    /* c_puts("[VMSVGA] Setting mode: "); */
    char buf[32];
    int i = 0;
    
    /* Width */
    if (width >= 1000) buf[i++] = '0' + (width / 1000);
    if (width >= 100) buf[i++] = '0' + ((width / 100) % 10);
    if (width >= 10) buf[i++] = '0' + ((width / 10) % 10);
    buf[i++] = '0' + (width % 10);
    buf[i++] = 'x';
    
    /* Height */
    if (height >= 1000) buf[i++] = '0' + (height / 1000);
    if (height >= 100) buf[i++] = '0' + ((height / 100) % 10);
    if (height >= 10) buf[i++] = '0' + ((height / 10) % 10);
    buf[i++] = '0' + (height % 10);
    buf[i++] = 'x';
    
    /* BPP */
    if (bpp >= 10) buf[i++] = '0' + (bpp / 10);
    buf[i++] = '0' + (bpp % 10);
    buf[i++] = '\n';
    buf[i] = 0;
    /* c_puts(buf); */
    
    /* Disable SVGA */
    svga_write_reg(SVGA_REG_ENABLE, 0);
    
    /* Set mode */
    svga_write_reg(SVGA_REG_WIDTH, width);
    svga_write_reg(SVGA_REG_HEIGHT, height);
    svga_write_reg(SVGA_REG_BITS_PER_PIXEL, bpp);
    
    /* Enable SVGA */
    svga_write_reg(SVGA_REG_ENABLE, 1);
    
    /* Update boot info block for the rest of the system */
    *(uint32_t*)(0x9000) = svga_fb_addr;
    *(uint16_t*)(0x9004) = width;
    *(uint16_t*)(0x9006) = height;
    *(uint8_t*)(0x9008) = bpp;
    
    /* Get actual pitch from hardware */
    uint32_t pitch = svga_read_reg(SVGA_REG_BYTES_PER_LINE);
    *(uint16_t*)(0x900A) = pitch;
    *(uint16_t*)(0x900C) = pitch;
    
    /* c_puts("[VMSVGA] Mode set successfully\n"); */
    return true;
}

/* Get maximum supported resolution */
void vmware_svga_get_max_resolution(uint32_t *width, uint32_t *height) {
    if (!vmware_svga_present) {
        *width = 1920;
        *height = 1080;
        return;
    }
    
    *width = svga_read_reg(SVGA_REG_MAX_WIDTH);
    *height = svga_read_reg(SVGA_REG_MAX_HEIGHT);
}

/* Get framebuffer address */
uint32_t vmware_svga_get_fb_addr(void) {
    return svga_fb_addr;
}

/* Get framebuffer size */
uint32_t vmware_svga_get_fb_size(void) {
    return svga_fb_size;
}
