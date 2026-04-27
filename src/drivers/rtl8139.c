#include <stdint.h>
#include <stdbool.h>
#include "network.h"
#include "portio.h"

/* Realtek RTL8139 Configuration */
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

/* RTL8139 Registers */
#define REG_MAC          0x00
#define REG_MAR          0x08
#define REG_TSD0         0x10
#define REG_TSAD0        0x20
#define REG_RBSTART      0x30
#define REG_CR           0x37
#define REG_CAPR         0x38
#define REG_IMR          0x3C
#define REG_ISR          0x3E
#define REG_TCR          0x40
#define REG_RCR          0x44
#define REG_MPC          0x4C
#define REG_CONFIG1      0x52

/* Command Register Bits */
#define CR_BUFE          0x01
#define CR_TE            0x08
#define CR_RE            0x10
#define CR_RST           0x10

static uint16_t io_base = 0;
static uint8_t mac_addr[6];
static uint8_t rx_buffer[8192 + 16 + 1514] __attribute__((aligned(4096)));
static uint32_t current_packet_ptr = 0;
static bool initialized = false;
static network_interface_t rtl_iface;

extern void c_puts(const char *s);
extern void c_putc(char c);

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static uint16_t find_rtl8139(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read(bus, dev, 0, 0);
            if ((id & 0xFFFF) == RTL8139_VENDOR_ID && ((id >> 16) & 0xFFFF) == RTL8139_DEVICE_ID) {
                uint32_t bar0 = pci_read(bus, dev, 0, 0x10);
                if (bar0 & 1) return (uint16_t)(bar0 & 0xFFFC);
            }
        }
    }
    return 0;
}

int rtl8139_init(void) {
    if (initialized) return 0;
    
    io_base = find_rtl8139();
    if (io_base == 0) return -1;
    
    c_puts("[RTL] Found RTL8139 at base 0x");
    char hex[] = "0123456789ABCDEF";
    c_putc(hex[(io_base >> 12) & 0xF]);
    c_putc(hex[(io_base >> 8) & 0xF]);
    c_putc(hex[(io_base >> 4) & 0xF]);
    c_putc(hex[io_base & 0xF]);
    c_puts("\n");
    
    /* Power on */
    outb(io_base + REG_CONFIG1, 0x00);
    
    /* Reset */
    outb(io_base + REG_CR, 0x10);
    while ((inb(io_base + REG_CR) & 0x10) != 0);
    
    /* Read MAC */
    for (int i = 0; i < 6; i++) mac_addr[i] = inb(io_base + REG_MAC + i);
    
    /* Init RX buffer */
    outl(io_base + REG_RBSTART, (uint32_t)rx_buffer);
    
    /* Interrupt mask */
    outw(io_base + REG_IMR, 0x0005); /* ROK + TOK */
    
    /* Receive config: Accept all packets (AB+AM+APM+AAP) and 8k+16 buffer */
    outl(io_base + REG_RCR, 0x0000000F | (1 << 7));
    
    /* Start RX/TX */
    outb(io_base + REG_CR, 0x0C);
    
    /* Setup interface */
    rtl_iface.name[0] = 'r';
    rtl_iface.name[1] = 't';
    rtl_iface.name[2] = 'l';
    rtl_iface.name[3] = '0';
    rtl_iface.name[4] = '\0';
    for (int i = 0; i < 6; i++) rtl_iface.mac_addr[i] = mac_addr[i];
    rtl_iface.link_up = true;
    
    extern int rtl8139_send(network_interface_t * iface, const uint8_t *data, uint32_t len);
    extern int rtl8139_recv(network_interface_t * iface, uint8_t *data, uint32_t max_len);
    rtl_iface.send_packet = rtl8139_send;
    rtl_iface.recv_packet = rtl8139_recv;
    
    extern int netif_register(network_interface_t * iface);
    netif_register(&rtl_iface);
    
    initialized = true;
    return 0;
}

static uint8_t tx_buffers[4][1536] __attribute__((aligned(4096)));
static int current_tx = 0;

int rtl8139_send(network_interface_t *iface, const uint8_t *data, uint32_t len) {
    (void)iface;
    if (len > 1514) return -1;
    
    for (uint32_t i = 0; i < len; i++) tx_buffers[current_tx][i] = data[i];
    
    outl(io_base + REG_TSAD0 + (current_tx * 4), (uint32_t)tx_buffers[current_tx]);
    outl(io_base + REG_TSD0 + (current_tx * 4), len);
    
    current_tx = (current_tx + 1) % 4;
    return (int)len;
}

int rtl8139_recv(network_interface_t *iface, uint8_t *data, uint32_t max_len) {
    (void)iface;
    if (inb(io_base + REG_CR) & 0x01) return 0; /* Buffer empty */
    
    uint16_t offset = (uint16_t)(current_packet_ptr % 8192);
    uint32_t header = *(uint32_t *)(rx_buffer + offset);
    uint16_t status = (uint16_t)(header & 0xFFFF);
    uint16_t len = (uint16_t)(header >> 16);
    
    if (!(status & 1)) return 0; /* Not OK */
    
    len -= 4; /* CRC */
    if (len > max_len) len = max_len;
    
    for (uint16_t i = 0; i < len; i++) {
        data[i] = rx_buffer[(offset + 4 + i) % 8192];
    }
    
    current_packet_ptr = (current_packet_ptr + len + 4 + 3) & ~3;
    outw(io_base + REG_CAPR, (uint16_t)(current_packet_ptr - 0x10));
    
    return (int)len;
}
