/*
 * Lazy network bring-up for GUI / Blaze: initializes VirtIO-Net + DHCP
 * without requiring the user to run NETSTART first.
*/

#include "../../include/network.h"

extern int wifi_driver_init(void);
extern int dhcp_init(network_interface_t *iface);
extern int dhcp_discover(network_interface_t *iface);
extern void netif_poll(void);
extern void netif_set_ip(network_interface_t *iface, uint32_t ip, uint32_t netmask,
                         uint32_t gateway, uint32_t dns);
extern uint32_t get_ticks(void);

int network_ensure_ready(void) {
  network_interface_t *iface = netif_get_default();
  if (iface && iface->ip_addr != 0)
    return 0;  /* Already up - fast path */

  puts("[NET] Initializing network driver...\n");
  if (wifi_driver_init() != 0)
    return -1;

  iface = netif_get_default();
  if (!iface)
    return -1;

  /*
   * DHCP: VMware/VirtualBox need real leases — do not force QEMU SLIRP 10.0.2.15 here;
   * that breaks VMware NAT (wrong subnet/gateway) and looks like a dead network.
   */
  if (dhcp_init(iface) == 0) {
    puts("[NET] Trying DHCP...\n");
    for (int retry = 0; retry < 8 && iface->ip_addr == 0; retry++) {
        if (dhcp_discover(iface) == 0) {
          uint32_t start = get_ticks();
          int spins = 0;
          while (iface->ip_addr == 0 && spins < 25000) {
            spins++;
            if ((get_ticks() - start) >= 450)
              break;
            for (int j = 0; j < 24; j++)
              netif_poll();
            for (volatile int i = 0; i < 400; i++);
          }
        }
    }
  }

  if (iface->ip_addr == 0)
    puts("[NET] DHCP did not configure an address (check NIC driver + VM network).\n");

  puts("[NET] Network ready\n");
  return iface->ip_addr != 0 ? 0 : -1;
}
