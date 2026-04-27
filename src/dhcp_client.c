/*
 * DHCP Client for AurionOS
 * Implements basic DHCP to get IP address automatically
*/

#include "../include/network.h"
#include "../include/stddef.h"

// DHCP message types
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5

// DHCP options
#define DHCP_OPT_MSG_TYPE 53
#define DHCP_OPT_SERVER_ID 54
#define DHCP_OPT_REQ_IP 50
#define DHCP_OPT_SUBNET 1
#define DHCP_OPT_ROUTER 3
#define DHCP_OPT_DNS 6
#define DHCP_OPT_END 255

// DHCP packet structure
typedef struct {
  uint8_t op;    // Message opcode
  uint8_t htype; // Hardware address type
  uint8_t hlen;  // Hardware address length
  uint8_t hops;
  uint32_t xid; // Transaction ID
  uint16_t secs;
  uint16_t flags;
  uint32_t ciaddr;      // Client IP
  uint32_t yiaddr;      // Your IP
  uint32_t siaddr;      // Server IP
  uint32_t giaddr;      // Gateway IP
  uint8_t chaddr[16];   // Client hardware address
  uint8_t sname[64];    // Server name
  uint8_t file[128];    // Boot file
  uint32_t magic;       // Magic cookie (0x63825363)
  uint8_t options[312]; // Options
} __attribute__((packed)) dhcp_packet_t;

static uint32_t dhcp_xid = 0x12345678;
static uint32_t offered_ip = 0;
static uint32_t server_ip = 0;

// Helper: Add DHCP option
static int dhcp_add_option(uint8_t *options, int offset, uint8_t code,
                           uint8_t len, const uint8_t *data) {
  options[offset++] = code;
  options[offset++] = len;
  for (int i = 0; i < len; i++) {
    options[offset++] = data[i];
  }
  return offset;
}

// Send DHCP DISCOVER
int dhcp_discover(network_interface_t *iface) {
  extern void puts(const char*);
  
  puts("[DHCP_DISC] Entering dhcp_discover\n");
  
  if (!iface) {
    puts("[DHCP_DISC] ERROR: iface is NULL!\n");
    return -1;
  }
  
  puts("[DHCP_DISC] iface is valid, link_up=");
  puts(iface->link_up ? "true" : "false");
  puts(", send_packet=");
  puts(iface->send_packet ? "set" : "NULL");
  puts("\n");
  
  /* Use static buffer to avoid stack overflow (CPU Exception) */
  static uint8_t packet[1500];
  // Zero out buffer for new request
  for(int i=0; i<1500; i++) packet[i] = 0;

  eth_header_t *eth = (eth_header_t *)packet;
  udp_header_t *udp =
      (udp_header_t *)(packet + sizeof(eth_header_t) + sizeof(ip_header_t));
  dhcp_packet_t *dhcp =
      (dhcp_packet_t *)(packet + sizeof(eth_header_t) + sizeof(ip_header_t) +
                        sizeof(udp_header_t));

  // Ethernet header - broadcast
  for (int i = 0; i < 6; i++) {
    eth->dest_mac[i] = 0xFF;
    eth->src_mac[i] = iface->mac_addr[i];
  }
  eth->ethertype = 0x0008; // IP (big endian)

  // IP header
  ip_header_t *ip = (ip_header_t *)(packet + sizeof(eth_header_t));
  ip->version_ihl = 0x45;
  ip->tos = 0;
  ip->total_length = __builtin_bswap16(
      sizeof(ip_header_t) + sizeof(udp_header_t) + sizeof(dhcp_packet_t));
  ip->identification = __builtin_bswap16(0x1234);
  ip->flags_fragment = 0;
  ip->ttl = 64;
  ip->protocol = IP_PROTO_UDP;
  ip->checksum = 0;  // Will calculate below
  ip->src_ip = 0;
  ip->dest_ip = 0xFFFFFFFF; // Broadcast
  
  // Calculate IP header checksum
  {
    uint32_t sum = 0;
    uint8_t *ptr = (uint8_t *)ip;
    for (int i = 0; i < 20; i += 2) {  // IP header is 20 bytes
      sum += (ptr[i] << 8) | ptr[i+1];
    }
    while (sum >> 16) {
      sum = (sum & 0xFFFF) + (sum >> 16);
    }
    ip->checksum = __builtin_bswap16(~sum);
  }

  // UDP header
  udp->src_port = __builtin_bswap16(68);  // DHCP client port
  udp->dest_port = __builtin_bswap16(67); // DHCP server port
  udp->length = __builtin_bswap16(sizeof(udp_header_t) + sizeof(dhcp_packet_t));

  // DHCP packet
  dhcp->op = 1;    // BOOTREQUEST
  dhcp->htype = 1; // Ethernet
  dhcp->hlen = 6;
  /* xid is network byte order on the wire (RFC 2131) */
  dhcp->xid = __builtin_bswap32(dhcp_xid);
  dhcp->flags = __builtin_bswap16(0x8000); // Broadcast flag
  for (int i = 0; i < 6; i++) {
    dhcp->chaddr[i] = iface->mac_addr[i];
  }
  dhcp->magic = __builtin_bswap32(0x63825363);

  // Add options
  int opt_offset = 0;
  uint8_t msg_type = DHCP_DISCOVER;
  opt_offset = dhcp_add_option(dhcp->options, opt_offset, DHCP_OPT_MSG_TYPE, 1,
                               &msg_type);
  dhcp->options[opt_offset++] = DHCP_OPT_END;

  // Send packet
  puts("[DHCP_DISC] About to call netif_send, link_up=");
  puts(iface->link_up ? "true" : "false");
  puts("\n");
  
  int result = netif_send(iface, packet,
                    sizeof(eth_header_t) + sizeof(ip_header_t) +
                        sizeof(udp_header_t) + sizeof(dhcp_packet_t));
  
  puts("[DHCP_DISC] netif_send returned: ");
  if (result >= 0) {
    puts("success\n");
  } else {
    puts("FAILED\n");
  }
  return result;
}

// Process DHCP packet
int dhcp_process(network_interface_t *iface, const uint8_t *packet,
                 uint32_t len) {
  extern void puts(const char*);
  extern void putc(char);
  
  puts("[DHCP_PROC] Called with len=");
  char buf[8];
  buf[0] = '0' + (len / 1000) % 10;
  buf[1] = '0' + (len / 100) % 10;
  buf[2] = '0' + (len / 10) % 10;
  buf[3] = '0' + len % 10;
  buf[4] = '\0';
  puts(buf);
  puts("\n");
  
  // DHCP minimum size is 236 bytes (not including full options array)
  // We check for at least the fixed header fields
  #define DHCP_MIN_SIZE 240
  if (!iface || !packet || len < DHCP_MIN_SIZE) {
    puts("[DHCP_PROC] Failed validation check (len < ");
    buf[0] = '0' + (DHCP_MIN_SIZE / 100) % 10;
    buf[1] = '0' + (DHCP_MIN_SIZE / 10) % 10;
    buf[2] = '0' + DHCP_MIN_SIZE % 10;
    buf[3] = '\0';
    puts(buf);
    puts(")\n");
    return -1;
  }

  dhcp_packet_t *dhcp = (dhcp_packet_t *)packet;

  puts("[DHCP_PROC] Checking XID: got=0x");
  const char hex[] = "0123456789ABCDEF";
  uint32_t got_xid = __builtin_bswap32(dhcp->xid);
  for (int i = 7; i >= 0; i--) {
    putc(hex[(got_xid >> (i*4)) & 0xF]);
  }
  puts(" expected=0x");
  for (int i = 7; i >= 0; i--) {
    putc(hex[(dhcp_xid >> (i*4)) & 0xF]);
  }
  puts("\n");
  
  /* Compare in host order (xid on wire is big-endian) */
  if (got_xid != dhcp_xid) {
    puts("[DHCP_PROC] XID mismatch, ignoring\n");
    return 0;
  }

  // Parse options
  uint8_t msg_type = 0;
  uint32_t subnet = 0;
  uint32_t router = 0;
  uint32_t dns = 0;

  int i = 0;
  while (i < 312 && dhcp->options[i] != DHCP_OPT_END) {
    uint8_t opt = dhcp->options[i++];
    if (opt == 0)
      continue; // Padding

    uint8_t opt_len = dhcp->options[i++];

    switch (opt) {
    case DHCP_OPT_MSG_TYPE:
      msg_type = dhcp->options[i];
      break;
    case DHCP_OPT_SERVER_ID:
      server_ip = *(uint32_t *)&dhcp->options[i];
      break;
    case DHCP_OPT_SUBNET:
      subnet = *(uint32_t *)&dhcp->options[i];
      break;
    case DHCP_OPT_ROUTER:
      router = *(uint32_t *)&dhcp->options[i];
      break;
    case DHCP_OPT_DNS:
      dns = *(uint32_t *)&dhcp->options[i];
      break;
    }

    i += opt_len;
  }

  // Handle different DHCP message types
  if (msg_type == DHCP_OFFER) {
    // Save offered IP (convert from network byte order to host byte order)
    offered_ip = __builtin_bswap32(dhcp->yiaddr);
    uint32_t host_subnet = subnet ? __builtin_bswap32(subnet) : 0x00FFFFFF;  // 255.255.255.0
    uint32_t host_router = router ? __builtin_bswap32(router) : 0;
    uint32_t host_dns = dns ? __builtin_bswap32(dns) : 0x08080808;  // 8.8.8.8 default

    puts("[DHCP] Got IP offer: ");
    extern void putc(char);
    char buf[4];
    buf[0] = '0' + ((offered_ip >> 24) & 0xFF) / 100;
    buf[1] = '0' + (((offered_ip >> 24) & 0xFF) / 10) % 10;
    buf[2] = '0' + ((offered_ip >> 24) & 0xFF) % 10;
    buf[3] = '\0';
    puts(buf);
    puts(".");
    buf[0] = '0' + ((offered_ip >> 16) & 0xFF) / 100;
    buf[1] = '0' + (((offered_ip >> 16) & 0xFF) / 10) % 10;
    buf[2] = '0' + ((offered_ip >> 16) & 0xFF) % 10;
    puts(buf);
    puts(".");
    buf[0] = '0' + ((offered_ip >> 8) & 0xFF) / 100;
    buf[1] = '0' + (((offered_ip >> 8) & 0xFF) / 10) % 10;
    buf[2] = '0' + ((offered_ip >> 8) & 0xFF) % 10;
    puts(buf);
    puts(".");
    buf[0] = '0' + (offered_ip & 0xFF) / 100;
    buf[1] = '0' + ((offered_ip & 0xFF) / 10) % 10;
    buf[2] = '0' + (offered_ip & 0xFF) % 10;
    puts(buf);
    puts("\n");

    // Send DHCP REQUEST (simplified - would normally send a proper request)
    // For now, just accept the offered IP
    extern void netif_set_ip(network_interface_t * iface, uint32_t ip,
                             uint32_t netmask, uint32_t gateway, uint32_t dns);
    netif_set_ip(iface, offered_ip, host_subnet, host_router, host_dns);

    return 1; // IP configured
  }

  return 0;
}

// Initialize DHCP client
int dhcp_init(network_interface_t *iface) {
  if (!iface)
    return -1;

  // Generate random transaction ID (simplified)
  dhcp_xid = 0x12345678;

  return 0;
}
