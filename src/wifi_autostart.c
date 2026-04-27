/*
 * WiFi Autostart - Simplified for VirtIO mode
 * WiFi is not supported - network uses VirtIO
*/

#include "wifi_autostart.h"
#include "../include/network.h"

extern void c_puts(const char *s);
#define puts c_puts

/* Initialize network on boot - simplified for VirtIO */
void wifi_autostart_init(void) {
    /* VirtIO network is initialized via NETSTART command */
    /* No automatic WiFi connection in VirtIO mode */
}

/* Check if network should auto-connect (always false for VirtIO) */
int wifi_autostart_should_connect(void) {
    return 0;  /* VirtIO doesn't auto-connect */
}

/* Attempt auto-connection (no-op for VirtIO) */
int wifi_autostart_connect(void) {
    puts("VirtIO mode: Use NETSTART to initialize network\n");
    return 0;
}

/* Main wifi_autostart function called from shell */
void wifi_autostart(void) {
    extern int wifi_driver_init(void);
    extern int dhcp_init(network_interface_t *iface);
    extern int dhcp_discover(network_interface_t *iface);
    extern network_interface_t* netif_get_default(void);
    extern void netif_poll(void);
    extern void netif_set_ip(network_interface_t *iface, uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);
    extern uint32_t get_ticks(void);
    
    puts("[AUTOSTART] Initializing network...\n");
    
    if (wifi_driver_init() != 0) {
        puts("[AUTOSTART] Network driver init failed\n");
        return;
    }
    
    network_interface_t *iface = netif_get_default();
    if (!iface) {
        puts("[AUTOSTART] No network interface found\n");
        return;
    }
    
    if (dhcp_init(iface) != 0) {
        puts("[AUTOSTART] DHCP init failed\n");
        return;
    }
    
    puts("[AUTOSTART] Attempting DHCP...\n");
    for (int attempt = 0; attempt < 3; attempt++) {
        if (dhcp_discover(iface) < 0)
            continue;
        uint32_t start = get_ticks();
        int poll_count = 0;
        while (poll_count < 100) {
            for (int i = 0; i < 20; i++) {
                netif_poll();
                poll_count++;
            }
            if (iface->ip_addr != 0) {
                puts("[AUTOSTART] Network ready!\n");
                return;
            }
        }
    }
    
    /* QEMU user-mode (SLIRP) defaults when DHCP doesn't complete */
    if (iface->ip_addr == 0) {
        puts("[AUTOSTART] DHCP timeout, using fallback IP\n");
        netif_set_ip(iface, 0x0A00020F, 0xFFFFFF00, 0x0A000202, 0x0A000203); /* 10.0.2.15 */
        puts("[AUTOSTART] Network ready (fallback)!\n");
    }
}
