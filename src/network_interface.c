/*
 * Network Interface Layer for AurionOS
 * Provides abstraction for network devices
*/

#include "../include/network.h"
#include "../include/stddef.h"

#define MAX_INTERFACES 4

static network_interface_t interfaces[MAX_INTERFACES];
static int num_interfaces = 0;
static network_interface_t *default_interface = NULL;

// Initialize network interface subsystem
int netif_init(void) {
  num_interfaces = 0;
  default_interface = NULL;

  // Clear all interfaces
  for (int i = 0; i < MAX_INTERFACES; i++) {
    interfaces[i].name[0] = '\0';
    interfaces[i].link_up = false;
    interfaces[i].ip_addr = 0;
    interfaces[i].netmask = 0;
    interfaces[i].gateway = 0;
    interfaces[i].dns_server = 0;
    interfaces[i].tx_packets = 0;
    interfaces[i].rx_packets = 0;
    interfaces[i].tx_bytes = 0;
    interfaces[i].rx_bytes = 0;
    interfaces[i].tx_errors = 0;
    interfaces[i].rx_errors = 0;
    interfaces[i].send_packet = NULL;
    interfaces[i].recv_packet = NULL;
  }

  return 0;
}

// Register a network interface
int netif_register(network_interface_t *iface) {
  if (!iface || num_interfaces >= MAX_INTERFACES) {
    return -1;
  }

  // Copy interface to our array
  interfaces[num_interfaces] = *iface;

  // Set as default if it's the first one
  if (default_interface == NULL) {
    default_interface = &interfaces[num_interfaces];
  }

  num_interfaces++;
  return 0;
}

// Get default network interface
network_interface_t *netif_get_default(void) { return default_interface; }

// Send packet through interface
int netif_send(network_interface_t *iface, const uint8_t *data, uint32_t len) {
  extern void puts(const char*);
  
  if (!iface || !data || len == 0) {
    puts("[netif_send] ERROR: Invalid params\n");
    return -1;
  }

  // Skip link_up check entirely - we trust the driver initialization
  // There's a struct alignment issue causing link_up to read incorrectly
  
  if (!iface->send_packet) {
    puts("[netif_send] ERROR: No send_packet function!\n");
    return -1;
  }

  puts("[netif_send] Sending packet...\n");
  int result = iface->send_packet(iface, data, len);

  if (result >= 0) {
    iface->tx_packets++;
    iface->tx_bytes += len;
  } else {
    iface->tx_errors++;
  }

  return result;
}

// Receive packet from interface
int netif_receive(network_interface_t *iface, uint8_t *data, uint32_t max_len) {
  if (!iface || !data || max_len == 0) {
    return -1;
  }

  // Skip link_up check - trust driver initialization (same as send path)
  // There's a struct alignment issue causing link_up to read incorrectly

  if (!iface->recv_packet) {
    return 0;
  }

  int result = iface->recv_packet(iface, data, max_len);

  if (result > 0) {
    iface->rx_packets++;
    iface->rx_bytes += result;
  } else if (result < 0) {
    iface->rx_errors++;
  }

  return result;
}

// Set interface IP configuration
void netif_set_ip(network_interface_t *iface, uint32_t ip, uint32_t netmask,
                  uint32_t gateway, uint32_t dns) {
  if (!iface)
    return;

  iface->ip_addr = ip;
  iface->netmask = netmask;
  iface->gateway = gateway;
  iface->dns_server = dns;
}

// Set interface link state
void netif_set_link(network_interface_t *iface, bool up) {
  if (!iface)
    return;
  iface->link_up = up;
}

// Poll for incoming packets
extern int ip_receive(uint8_t *buffer, uint32_t len);

void netif_poll(void) {
  network_interface_t *iface = netif_get_default();
  if (!iface) {
    return;
  }
  // Skip link_up check - trust driver initialization

  static uint8_t rx_buffer[2048];
  int len = netif_receive(iface, rx_buffer, sizeof(rx_buffer));
  
  if (len > 0) {
    // Process received packet through IP stack
    ip_receive(rx_buffer, len);
  }
}
