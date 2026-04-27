/*
 * Network bring-up: try real PCI Wi-Fi first, then VirtIO-Net Ethernet.
*/

#include <stdint.h>
#include "../../include/network.h"
#include "../pci.h"

extern void c_puts(const char *s);
extern void c_putc(char c);

extern int wifi_pci_try_init(void);
extern int virtio_net_driver_init(void);
extern int rtl8139_init(void);
extern int ne2000_init(void);
extern int virtio_net_ready(void);
extern void virtio_net_get_mac(uint8_t *m);
extern void virtio_net_shutdown_impl(void);

static void pci_warn_unsupported_nic(void) {
    static const struct {
        uint16_t vendor;
        uint16_t device;
        const char *name;
    } known[] = {
        {0x8086, 0x100E, "Intel 82540EM (e1000)"},
        {0x8086, 0x100F, "Intel PRO/1000 MT (e1000)"},
        {0x8086, 0x10D3, "Intel e1000e-class"},
        {0x15AD, 0x07B0, "VMware vmxnet3"},
        {0x1022, 0x2000, "AMD PCnet / Lance"},
    };
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        pci_device_t d = pci_find_device(known[i].vendor, known[i].device);
        if (d.vendor_id != 0xFFFF && d.vendor_id == known[i].vendor) {
            c_puts("[NET] Detected ");
            c_puts(known[i].name);
            c_puts(" — no driver in AurionOS yet.\n");
            c_puts("[NET] VMware: Settings → Network Adapter → Advanced → use RTL8139 (legacy).\n");
            c_puts("[NET] QEMU: use -device virtio-net-pci -netdev user,id=n0\n");
            return;
        }
    }
    c_puts("[NET] No supported NIC (VirtIO-Net, RTL8139, or NE2000).\n");
}

int wifi_driver_init(void) {
    if (wifi_pci_try_init() == 0) return 0;
    if (virtio_net_driver_init() == 0) return 0;
    if (rtl8139_init() == 0) return 0;
    if (ne2000_init() == 0) return 0;
    pci_warn_unsupported_nic();
    return -1;
}

int wifi_driver_test(void) {
    c_puts("\n=== Network driver test (VirtIO-Net if no Wi-Fi driver) ===\n");
    if (!virtio_net_ready() && wifi_driver_init() != 0) {
        c_puts("FAILED\n");
        return -1;
    }
    c_puts("Status: link up (Ethernet path)\nMAC: ");
    uint8_t mac[6];
    virtio_net_get_mac(mac);
    for (int i = 0; i < 6; i++) {
        c_putc("0123456789ABCDEF"[(mac[i] >> 4) & 0xF]);
        c_putc("0123456789ABCDEF"[mac[i] & 0xF]);
        if (i < 5) c_putc(':');
    }
    c_puts("\n=== Test complete ===\n");
    return 0;
}

int wifi_get_mac(uint8_t *mac) {
    if (!mac) return -1;
    if (!virtio_net_ready()) return -1;
    virtio_net_get_mac(mac);
    return 0;
}

int wifi_is_connected(void) { return virtio_net_ready() ? 1 : 0; }

void wifi_driver_shutdown(void) { virtio_net_shutdown_impl(); }
