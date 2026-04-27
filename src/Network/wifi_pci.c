/*
 * PCI 802.11 device discovery and bring-up hooks.
 * Full station mode (firmware upload, MLME) is chipset-specific and added per NIC.
*/

#include <stdint.h>
#include "../pci.h"

extern void c_puts(const char *s);
extern void c_putc(char c);

static void put_hex32(uint32_t v) {
    for (int i = 7; i >= 0; i--) {
        int n = (int)((v >> (i * 4)) & 0xF);
        c_putc(n < 10 ? (char)('0' + n) : (char)('A' + n - 10));
    }
}

static void put_hex16(uint16_t v) {
    for (int i = 3; i >= 0; i--) {
        int n = (int)((v >> (i * 4)) & 0xF);
        c_putc(n < 10 ? (char)('0' + n) : (char)('A' + n - 10));
    }
}

/*
 * Probe PCI for wireless class devices. When a real driver exists for the VID/DID,
 * return 0 after netif_register. Until then return -1 so VirtIO-Net can be used.
 */
int wifi_pci_try_init(void) {
    pci_device_t list[8];
    int n = pci_find_wifi_devices(list, 8);
    if (n <= 0) return -1;

    c_puts("[WiFi] PCI: ");
    c_putc((char)('0' + n));
    c_puts(" 802.11 device(s) found (no chipset driver bound yet)\n");

    for (int i = 0; i < n; i++) {
        pci_enable_device(&list[i]);
        c_puts("  [");
        c_putc((char)('0' + i));
        c_puts("] VID=0x");
        put_hex16(list[i].vendor_id);
        c_puts(" DID=0x");
        put_hex16(list[i].device_id);
        c_puts(" BAR0=0x");
        put_hex32(list[i].bar0);
        c_puts("\n");
    }

    return -1;
}
