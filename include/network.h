#ifndef NETWORK_H
#define NETWORK_H

#include "stdbool.h"
#include "stdint.h"

// Network interface structure
typedef struct network_interface {
  char name[16];
  uint8_t mac_addr[6];
  uint32_t ip_addr;
  uint32_t netmask;
  uint32_t gateway;
  uint32_t dns_server;
  bool link_up;

  // Statistics
  unsigned long long tx_packets;
  unsigned long long rx_packets;
  unsigned long long tx_bytes;
  unsigned long long rx_bytes;
  unsigned long long tx_errors;
  unsigned long long rx_errors;

  // Function pointers for driver operations
  int (*send_packet)(struct network_interface *iface, const uint8_t *data,
                     uint32_t len);
  int (*recv_packet)(struct network_interface *iface, uint8_t *data,
                     uint32_t max_len);
} network_interface_t;

// IP address helpers
#define IP_ADDR(a, b, c, d)                                                    \
  ((uint32_t)(((a) << 24) | ((b) << 16) | ((c) << 8) | (d)))
#define IP_A(ip) (((ip) >> 24) & 0xFF)
#define IP_B(ip) (((ip) >> 16) & 0xFF)
#define IP_C(ip) (((ip) >> 8) & 0xFF)
#define IP_D(ip) ((ip) & 0xFF)

// Network interface functions
int netif_init(void);
int netif_register(network_interface_t *iface);
network_interface_t *netif_get_default(void);
int netif_send(network_interface_t *iface, const uint8_t *data, uint32_t len);
int netif_receive(network_interface_t *iface, uint8_t *data, uint32_t max_len);
void netif_poll(void);

// IP stack functions
int ip_init(void);
int ip_send(uint32_t dest_ip, uint8_t protocol, const uint8_t *data,
            uint32_t len);
int ip_receive(uint8_t *buffer, uint32_t len);

// DHCP functions
int dhcp_init(network_interface_t *iface);
int dhcp_discover(network_interface_t *iface);
int dhcp_process(network_interface_t *iface, const uint8_t *packet,
                 uint32_t len);

// ARP functions
int arp_init(void);
int arp_resolve(uint32_t ip_addr, uint8_t *mac_addr);
int arp_add_entry(uint32_t ip_addr, const uint8_t *mac_addr);

// ICMP functions (for ping)
int icmp_init(void);
int icmp_ping(uint32_t dest_ip, uint16_t seq);
int icmp_process(const uint8_t *packet, uint32_t len);

// Protocol numbers
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

// Ethernet frame type
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP 0x0800

// Ethernet header
typedef struct {
  uint8_t dest_mac[6];
  uint8_t src_mac[6];
  uint16_t ethertype;
} __attribute__((packed)) eth_header_t;

// IP header
typedef struct {
  uint8_t version_ihl;
  uint8_t tos;
  uint16_t total_length;
  uint16_t identification;
  uint16_t flags_fragment;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;
  uint32_t src_ip;
  uint32_t dest_ip;
} __attribute__((packed)) ip_header_t;

// UDP header
typedef struct {
  uint16_t src_port;
  uint16_t dest_port;
  uint16_t length;
  uint16_t checksum;
} __attribute__((packed)) udp_header_t;

// ICMP header
typedef struct {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t id;
  uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

// TCP Flags
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

// TCP header
typedef struct {
  uint16_t src_port;
  uint16_t dest_port;
  uint32_t sequence;
  uint32_t ack_num;
  uint8_t data_offset_reserved; // 4 bits offset, 4 bits reserved (usually 0)
  uint8_t flags;
  uint16_t window_size;
  uint16_t checksum;
  uint16_t urgent_pointer;
} __attribute__((packed)) tcp_header_t;

// DNS Header
typedef struct {
  uint16_t id;
  uint16_t flags;
  uint16_t q_count;
  uint16_t ans_count;
  uint16_t auth_count;
  uint16_t add_count;
} __attribute__((packed)) dns_header_t;

#endif // NETWORK_H
