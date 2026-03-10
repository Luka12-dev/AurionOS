/*
 * NE2000-compatible Ethernet Driver for AurionOS
 * Works on real hardware, VirtualBox, and QEMU with NE2000 emulation
 * 
 * The NE2000 is widely emulated and many real NICs are compatible.
*/

#include <stdint.h>
#include <stdbool.h>
#include "portio.h"

/* NE2000 I/O Ports (relative to base) */
#define NE_CMD          0x00  /* Command register */
#define NE_PSTART       0x01  /* Page start (write) */
#define NE_PSTOP        0x02  /* Page stop (write) */
#define NE_BOUNDARY     0x03  /* Boundary pointer */
#define NE_TPSR         0x04  /* Transmit page start */
#define NE_TBCR0        0x05  /* Transmit byte count low */
#define NE_TBCR1        0x06  /* Transmit byte count high */
#define NE_ISR          0x07  /* Interrupt status */
#define NE_RSAR0        0x08  /* Remote start address low */
#define NE_RSAR1        0x09  /* Remote start address high */
#define NE_RBCR0        0x0A  /* Remote byte count low */
#define NE_RBCR1        0x0B  /* Remote byte count high */
#define NE_RCR          0x0C  /* Receive config (write) */
#define NE_TCR          0x0D  /* Transmit config (write) */
#define NE_DCR          0x0E  /* Data config (write) */
#define NE_IMR          0x0F  /* Interrupt mask (write) */

/* Page 1 registers */
#define NE_PAR0         0x01  /* Physical address 0 */
#define NE_CURR         0x07  /* Current page */

/* NE2000 data port */
#define NE_DATA         0x10

/* Reset port */
#define NE_RESET        0x1F

/* Commands */
#define NE_CMD_STOP     0x01
#define NE_CMD_START    0x02
#define NE_CMD_TRANS    0x04
#define NE_CMD_RREAD    0x08
#define NE_CMD_RWRITE   0x10
#define NE_CMD_NODMA    0x20
#define NE_CMD_PAGE0    0x00
#define NE_CMD_PAGE1    0x40
#define NE_CMD_PAGE2    0x80

/* Buffer layout */
#define NE_TXSTART      0x40
#define NE_RXSTART      0x46
#define NE_RXSTOP       0x80

/* Common NE2000 I/O base addresses to probe */
static const uint16_t ne2000_ports[] = {
    0x300, 0x280, 0x320, 0x340, 0x360,
    0x240, 0x260, 0x200, 0x220, 0x380
};

/* Driver state */
static bool ne2000_initialized = false;
static uint16_t ne2000_base = 0;
static uint8_t ne2000_mac[6];
static uint8_t ne2000_next_pkt = NE_RXSTART;

/* External functions */
extern void c_puts(const char *s);
extern void c_putc(char c);

/* Delay */
static void ne_delay(void) {
    for (volatile int i = 0; i < 1000; i++);
}

/* Check if NE2000 exists at given port */
static bool ne2000_probe(uint16_t base) {
    /* Reset the card */
    uint8_t reset_val = inb(base + NE_RESET);
    outb(base + NE_RESET, reset_val);
    ne_delay();
    
    /* Stop the NIC */
    outb(base + NE_CMD, NE_CMD_STOP | NE_CMD_NODMA | NE_CMD_PAGE0);
    ne_delay();
    
    /* Check for valid ISR reset bit */
    uint8_t isr = inb(base + NE_ISR);
    if ((isr & 0x80) == 0) {
        return false;
    }
    
    /* Clear ISR */
    outb(base + NE_ISR, 0xFF);
    
    return true;
}

/* Read from NE2000 buffer memory */
static void ne2000_read_mem(uint16_t src, uint8_t *dst, uint16_t len) {
    /* Setup remote DMA */
    outb(ne2000_base + NE_CMD, NE_CMD_NODMA | NE_CMD_PAGE0 | NE_CMD_START);
    outb(ne2000_base + NE_RBCR0, len & 0xFF);
    outb(ne2000_base + NE_RBCR1, len >> 8);
    outb(ne2000_base + NE_RSAR0, src & 0xFF);
    outb(ne2000_base + NE_RSAR1, src >> 8);
    outb(ne2000_base + NE_CMD, NE_CMD_RREAD | NE_CMD_PAGE0 | NE_CMD_START);
    
    /* Read data (16-bit) */
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = inw(ne2000_base + NE_DATA);
        dst[i] = w & 0xFF;
        if (i + 1 < len) {
            dst[i + 1] = (w >> 8) & 0xFF;
        }
    }
}

/* Write to NE2000 buffer memory */
static void ne2000_write_mem(uint16_t dst, const uint8_t *src, uint16_t len) {
    /* Setup remote DMA */
    outb(ne2000_base + NE_CMD, NE_CMD_NODMA | NE_CMD_PAGE0 | NE_CMD_START);
    outb(ne2000_base + NE_RBCR0, len & 0xFF);
    outb(ne2000_base + NE_RBCR1, len >> 8);
    outb(ne2000_base + NE_RSAR0, dst & 0xFF);
    outb(ne2000_base + NE_RSAR1, dst >> 8);
    outb(ne2000_base + NE_CMD, NE_CMD_RWRITE | NE_CMD_PAGE0 | NE_CMD_START);
    
    /* Write data (16-bit) */
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = src[i];
        if (i + 1 < len) {
            w |= ((uint16_t)src[i + 1]) << 8;
        }
        outw(ne2000_base + NE_DATA, w);
    }
    
    /* Wait for DMA complete */
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(ne2000_base + NE_ISR) & 0x40) {
            outb(ne2000_base + NE_ISR, 0x40);
            break;
        }
    }
}

/* Initialize NE2000 */
int ne2000_init(void) {
    if (ne2000_initialized) return 0;
    
    c_puts("[NE2K] Probing for NE2000...\n");
    
    /* Probe for card */
    for (int i = 0; i < sizeof(ne2000_ports)/sizeof(ne2000_ports[0]); i++) {
        if (ne2000_probe(ne2000_ports[i])) {
            ne2000_base = ne2000_ports[i];
            c_puts("[NE2K] Found at 0x");
            char hex[] = "0123456789ABCDEF";
            c_putc(hex[(ne2000_base >> 8) & 0xF]);
            c_putc(hex[(ne2000_base >> 4) & 0xF]);
            c_putc(hex[ne2000_base & 0xF]);
            c_puts("\n");
            break;
        }
    }
    
    if (ne2000_base == 0) {
        c_puts("[NE2K] No NE2000 found\n");
        return -1;
    }
    
    /* Initialize the card */
    outb(ne2000_base + NE_CMD, NE_CMD_STOP | NE_CMD_NODMA | NE_CMD_PAGE0);
    ne_delay();
    
    /* Set data config: FIFO threshold, loopback, word mode */
    outb(ne2000_base + NE_DCR, 0x49);  /* Word-wide DMA, normal operation */
    
    /* Clear remote byte count */
    outb(ne2000_base + NE_RBCR0, 0);
    outb(ne2000_base + NE_RBCR1, 0);
    
    /* Receive config: Accept broadcast and physical match */
    outb(ne2000_base + NE_RCR, 0x04);
    
    /* Transmit config: Normal operation */
    outb(ne2000_base + NE_TCR, 0x00);
    
    /* Setup buffer pages */
    outb(ne2000_base + NE_PSTART, NE_RXSTART);
    outb(ne2000_base + NE_BOUNDARY, NE_RXSTART);
    outb(ne2000_base + NE_PSTOP, NE_RXSTOP);
    outb(ne2000_base + NE_TPSR, NE_TXSTART);
    
    /* Clear ISR */
    outb(ne2000_base + NE_ISR, 0xFF);
    
    /* Disable interrupts (we poll) */
    outb(ne2000_base + NE_IMR, 0x00);
    
    /* Read MAC address from PROM */
    uint8_t prom[32];
    ne2000_read_mem(0x0000, prom, 32);
    
    /* NE2000 stores MAC as words */
    for (int i = 0; i < 6; i++) {
        ne2000_mac[i] = prom[i * 2];
    }
    
    c_puts("[NE2K] MAC: ");
    for (int i = 0; i < 6; i++) {
        c_putc("0123456789ABCDEF"[(ne2000_mac[i] >> 4) & 0xF]);
        c_putc("0123456789ABCDEF"[ne2000_mac[i] & 0xF]);
        if (i < 5) c_putc(':');
    }
    c_puts("\n");
    
    /* Switch to page 1 and set MAC address */
    outb(ne2000_base + NE_CMD, NE_CMD_STOP | NE_CMD_NODMA | NE_CMD_PAGE1);
    for (int i = 0; i < 6; i++) {
        outb(ne2000_base + NE_PAR0 + i, ne2000_mac[i]);
    }
    outb(ne2000_base + NE_CURR, NE_RXSTART + 1);
    
    /* Back to page 0, start the NIC */
    outb(ne2000_base + NE_CMD, NE_CMD_START | NE_CMD_NODMA | NE_CMD_PAGE0);
    
    ne2000_next_pkt = NE_RXSTART + 1;
    ne2000_initialized = true;
    
    c_puts("[NE2K] Initialized\n");
    return 0;
}

/* Send packet */
int ne2000_send(const uint8_t *data, uint16_t len) {
    if (!ne2000_initialized) return -1;
    if (len < 14 || len > 1514) return -1;
    
    /* Ensure minimum packet size */
    if (len < 60) len = 60;
    
    /* Copy packet to TX buffer */
    ne2000_write_mem(NE_TXSTART << 8, data, len);
    
    /* Set transmit page and byte count */
    outb(ne2000_base + NE_TPSR, NE_TXSTART);
    outb(ne2000_base + NE_TBCR0, len & 0xFF);
    outb(ne2000_base + NE_TBCR1, len >> 8);
    
    /* Issue transmit command */
    outb(ne2000_base + NE_CMD, NE_CMD_START | NE_CMD_TRANS | NE_CMD_NODMA);
    
    /* Wait for transmit complete */
    int timeout = 100000;
    while (timeout-- > 0) {
        uint8_t isr = inb(ne2000_base + NE_ISR);
        if (isr & 0x02) {  /* PTX - packet transmitted */
            outb(ne2000_base + NE_ISR, 0x02);
            return 0;
        }
        if (isr & 0x08) {  /* TXE - transmit error */
            outb(ne2000_base + NE_ISR, 0x08);
            return -1;
        }
    }
    
    return -1;  /* Timeout */
}

/* Receive packet */
int ne2000_recv(uint8_t *buffer, uint16_t max_len) {
    if (!ne2000_initialized) return -1;
    
    /* Check for received packet */
    uint8_t isr = inb(ne2000_base + NE_ISR);
    if (!(isr & 0x01)) {  /* PRX - packet received */
        return 0;  /* No packet */
    }
    outb(ne2000_base + NE_ISR, 0x01);
    
    /* Get current page pointer */
    outb(ne2000_base + NE_CMD, NE_CMD_START | NE_CMD_NODMA | NE_CMD_PAGE1);
    uint8_t curr = inb(ne2000_base + NE_CURR);
    outb(ne2000_base + NE_CMD, NE_CMD_START | NE_CMD_NODMA | NE_CMD_PAGE0);
    
    uint8_t boundary = inb(ne2000_base + NE_BOUNDARY) + 1;
    if (boundary >= NE_RXSTOP) boundary = NE_RXSTART;
    
    if (boundary == curr) {
        return 0;  /* No packet */
    }
    
    /* Read packet header */
    uint8_t header[4];
    ne2000_read_mem(boundary << 8, header, 4);
    
    uint8_t next = header[1];
    uint16_t len = header[2] | (header[3] << 8);
    
    /* Validate */
    if (len < 4 || len > 1518) {
        /* Bad packet, reset boundary */
        outb(ne2000_base + NE_BOUNDARY, curr > NE_RXSTART ? curr - 1 : NE_RXSTOP - 1);
        return -1;
    }
    
    /* Actual data length */
    len -= 4;  /* Remove header */
    if (len > max_len) len = max_len;
    
    /* Read packet data */
    ne2000_read_mem((boundary << 8) + 4, buffer, len);
    
    /* Update boundary */
    outb(ne2000_base + NE_BOUNDARY, next > NE_RXSTART ? next - 1 : NE_RXSTOP - 1);
    
    return len;
}

/* Get MAC address */
void ne2000_get_mac(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = ne2000_mac[i];
    }
}

/* Check if initialized */
bool ne2000_is_active(void) {
    return ne2000_initialized;
}