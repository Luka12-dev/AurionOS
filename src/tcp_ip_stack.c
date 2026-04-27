/*
 * Basic TCP/IP Stack for AurionOS
 * Implements IP, ICMP, ARP protocols
*/

#include "../include/network.h"
#include <stddef.h>

extern void puts(const char *);
extern void putc(char);

// ARP cache
#define ARP_CACHE_SIZE 16
typedef struct
{
  uint32_t ip;
  uint8_t mac[6];
  bool valid;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

// Byte swap helpers
static uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }

static uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }

// Calculate IP checksum
static uint16_t ip_checksum(const void *data, int len)
{
  const uint16_t *buf = (const uint16_t *)data;
  uint32_t sum = 0;

  while (len > 1)
  {
    sum += *buf++;
    len -= 2;
  }

  if (len == 1)
  {
    sum += *(uint8_t *)buf;
  }

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);

  return ~sum;
}

// Initialize IP stack
int ip_init(void) { return 0; }

// Initialize ARP
int arp_init(void)
{
  for (int i = 0; i < ARP_CACHE_SIZE; i++)
  {
    arp_cache[i].valid = false;
  }
  return 0;
}

// Add ARP entry
int arp_add_entry(uint32_t ip_addr, const uint8_t *mac_addr)
{
  if (!mac_addr)
    return -1;

  // Find empty slot or replace oldest
  for (int i = 0; i < ARP_CACHE_SIZE; i++)
  {
    if (!arp_cache[i].valid)
    {
      arp_cache[i].ip = ip_addr;
      for (int j = 0; j < 6; j++)
      {
        arp_cache[i].mac[j] = mac_addr[j];
      }
      arp_cache[i].valid = true;
      return 0;
    }
  }

  // Replace first entry if cache is full
  arp_cache[0].ip = ip_addr;
  for (int j = 0; j < 6; j++)
  {
    arp_cache[0].mac[j] = mac_addr[j];
  }
  arp_cache[0].valid = true;

  return 0;
}

// Resolve IP to MAC via ARP
int arp_resolve(uint32_t ip_addr, uint8_t *mac_addr)
{
  if (!mac_addr)
    return -1;

  // Check cache
  for (int i = 0; i < ARP_CACHE_SIZE; i++)
  {
    if (arp_cache[i].valid && arp_cache[i].ip == ip_addr)
    {
      for (int j = 0; j < 6; j++)
      {
        mac_addr[j] = arp_cache[i].mac[j];
      }
      return 0;
    }
  }

  // TODO: Send ARP request if not in cache
  // For now, return error
  return -1;
}

// Send IP packet
int ip_send(uint32_t dest_ip, uint8_t protocol, const uint8_t *data,
            uint32_t len)
{
  network_interface_t *iface = netif_get_default();
  if (!iface || !data)
    return -1;

  // Debug: Show src IP and gateway once
  static int shown = 0;
  if (!shown && protocol == 6)
  { // TCP
    shown = 1;
    extern void puts(const char *);
    extern void putc(char);
    puts("[IP] src=");
    putc('0' + ((iface->ip_addr >> 24) & 0xFF) / 100 % 10);
    putc('0' + ((iface->ip_addr >> 24) & 0xFF) / 10 % 10);
    putc('0' + ((iface->ip_addr >> 24) & 0xFF) % 10);
    putc('.');
    putc('0' + ((iface->ip_addr >> 16) & 0xFF) / 100 % 10);
    putc('0' + ((iface->ip_addr >> 16) & 0xFF) / 10 % 10);
    putc('0' + ((iface->ip_addr >> 16) & 0xFF) % 10);
    putc('.');
    putc('0' + ((iface->ip_addr >> 8) & 0xFF) / 100 % 10);
    putc('0' + ((iface->ip_addr >> 8) & 0xFF) / 10 % 10);
    putc('0' + ((iface->ip_addr >> 8) & 0xFF) % 10);
    putc('.');
    putc('0' + (iface->ip_addr & 0xFF) / 100 % 10);
    putc('0' + (iface->ip_addr & 0xFF) / 10 % 10);
    putc('0' + (iface->ip_addr & 0xFF) % 10);
    puts(" gw=");
    putc('0' + ((iface->gateway >> 24) & 0xFF) / 100 % 10);
    putc('0' + ((iface->gateway >> 24) & 0xFF) / 10 % 10);
    putc('0' + ((iface->gateway >> 24) & 0xFF) % 10);
    putc('.');
    putc('0' + ((iface->gateway >> 16) & 0xFF) / 100 % 10);
    putc('0' + ((iface->gateway >> 16) & 0xFF) / 10 % 10);
    putc('0' + ((iface->gateway >> 16) & 0xFF) % 10);
    putc('.');
    putc('0' + ((iface->gateway >> 8) & 0xFF) / 100 % 10);
    putc('0' + ((iface->gateway >> 8) & 0xFF) / 10 % 10);
    putc('0' + ((iface->gateway >> 8) & 0xFF) % 10);
    putc('.');
    putc('0' + (iface->gateway & 0xFF) / 100 % 10);
    putc('0' + (iface->gateway & 0xFF) / 10 % 10);
    putc('0' + (iface->gateway & 0xFF) % 10);
    puts("\n");
  }

  uint8_t packet[1500];
  eth_header_t *eth = (eth_header_t *)packet;
  ip_header_t *ip = (ip_header_t *)(packet + sizeof(eth_header_t));

  // Resolve destination MAC
  uint8_t dest_mac[6];
  bool same_network =
      (dest_ip & iface->netmask) == (iface->ip_addr & iface->netmask);

  if (same_network)
  {
    // Same network - resolve directly
    if (arp_resolve(dest_ip, dest_mac) < 0)
    {
      // Use broadcast if ARP fails
      for (int i = 0; i < 6; i++)
        dest_mac[i] = 0xFF;
    }
  }
  else
  {
    // Different network - use gateway MAC
    // For QEMU/VirtIO, the gateway is typically at 10.0.2.2
    // and QEMU responds to any MAC, but we should use the proper one
    if (arp_resolve(iface->gateway, dest_mac) < 0)
    {
      // CRITICAL FIX: Use QEMU's virtual router MAC address
      // QEMU's default gateway MAC is 52:55:0a:00:02:02
      dest_mac[0] = 0x52;
      dest_mac[1] = 0x55;
      dest_mac[2] = 0x0a;
      dest_mac[3] = 0x00;
      dest_mac[4] = 0x02;
      dest_mac[5] = 0x02;
    }
  }

  // Ethernet header
  for (int i = 0; i < 6; i++)
  {
    eth->dest_mac[i] = dest_mac[i];
    eth->src_mac[i] = iface->mac_addr[i];
  }
  eth->ethertype = htons(ETH_TYPE_IP);

  // IP header
  ip->version_ihl = 0x45; // IPv4, 20 bytes header
  ip->tos = 0;
  ip->total_length = htons(sizeof(ip_header_t) + len);
  ip->identification = 0;
  ip->flags_fragment = 0;
  ip->ttl = 64;
  ip->protocol = protocol;
  ip->checksum = 0;
  ip->src_ip = htonl(iface->ip_addr);
  ip->dest_ip = htonl(dest_ip);

  // Calculate IP checksum
  ip->checksum = ip_checksum(ip, sizeof(ip_header_t));

  // Copy payload
  for (uint32_t i = 0;
       i < len && i < (1500 - sizeof(eth_header_t) - sizeof(ip_header_t));
       i++)
  {
    packet[sizeof(eth_header_t) + sizeof(ip_header_t) + i] = data[i];
  }

  return netif_send(iface, packet,
                    sizeof(eth_header_t) + sizeof(ip_header_t) + len);
}

// Initialize ICMP
int icmp_init(void) { return 0; }

// Send ICMP ping
int icmp_ping(uint32_t dest_ip, uint16_t seq)
{
  uint8_t packet[64];
  icmp_header_t *icmp = (icmp_header_t *)packet;

  icmp->type = 8; // Echo request
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->id = htons(0x1234);
  icmp->sequence = htons(seq);

  // Fill with some data
  for (int i = sizeof(icmp_header_t); i < 64; i++)
  {
    packet[i] = i;
  }

  // Calculate checksum
  icmp->checksum = ip_checksum(packet, 64);

  return ip_send(dest_ip, IP_PROTO_ICMP, packet, 64);
}

// Process ICMP packet
int icmp_process(const uint8_t *packet, uint32_t len)
{
  if (!packet || len < sizeof(icmp_header_t))
  {
    return -1;
  }

  const icmp_header_t *icmp = (const icmp_header_t *)packet;

  if (icmp->type == 0)
  {           // Echo reply
    return 1; // Ping reply received
  }

  return 0;
}

extern uint32_t get_ticks(void);
int tcp_process(uint32_t src_ip, const uint8_t *packet, uint32_t len);

extern int udp_process(uint32_t src_ip, const uint8_t *packet, uint32_t len);

// ARP packet structure
typedef struct
{
  uint16_t hw_type;
  uint16_t proto_type;
  uint8_t hw_size;
  uint8_t proto_size;
  uint16_t opcode;
  uint8_t sender_mac[6];
  uint32_t sender_ip;
  uint8_t target_mac[6];
  uint32_t target_ip;
} __attribute__((packed)) arp_packet_t;

#define ARP_REQUEST 1
#define ARP_REPLY 2

// Process ARP packet and send reply if it's a request for us
static int arp_process(uint8_t *buffer, uint32_t len)
{
  if (len < sizeof(eth_header_t) + sizeof(arp_packet_t))
  {
    return -1;
  }

  network_interface_t *iface = netif_get_default();
  if (!iface)
    return -1;

  eth_header_t *eth = (eth_header_t *)buffer;
  arp_packet_t *arp = (arp_packet_t *)(buffer + sizeof(eth_header_t));

  uint16_t opcode = htons(arp->opcode);
  uint32_t target_ip = arp->target_ip; // Already in network byte order
  uint32_t our_ip = htonl(iface->ip_addr);

  // Is this ARP request for our IP?
  if (opcode == ARP_REQUEST && target_ip == our_ip)
  {
    // Build ARP reply
    uint8_t reply[64];
    eth_header_t *reply_eth = (eth_header_t *)reply;
    arp_packet_t *reply_arp = (arp_packet_t *)(reply + sizeof(eth_header_t));

    // Ethernet header - send back to requester
    for (int i = 0; i < 6; i++)
    {
      reply_eth->dest_mac[i] = arp->sender_mac[i];
      reply_eth->src_mac[i] = iface->mac_addr[i];
    }
    reply_eth->ethertype = htons(ETH_TYPE_ARP);

    // ARP reply
    reply_arp->hw_type = htons(1);         // Ethernet
    reply_arp->proto_type = htons(0x0800); // IPv4
    reply_arp->hw_size = 6;
    reply_arp->proto_size = 4;
    reply_arp->opcode = htons(ARP_REPLY);

    // Our MAC and IP as sender
    for (int i = 0; i < 6; i++)
    {
      reply_arp->sender_mac[i] = iface->mac_addr[i];
    }
    reply_arp->sender_ip = our_ip;

    // Original requester as target
    for (int i = 0; i < 6; i++)
    {
      reply_arp->target_mac[i] = arp->sender_mac[i];
    }
    reply_arp->target_ip = arp->sender_ip;

    // Send reply
    netif_send(iface, reply, sizeof(eth_header_t) + sizeof(arp_packet_t));
    return 1;
  }

  // If it's an ARP reply, add to our cache
  if (opcode == ARP_REPLY)
  {
    uint32_t sender_ip = __builtin_bswap32(arp->sender_ip);
    arp_add_entry(sender_ip, arp->sender_mac);
  }

  return 0;
}

// Process received IP packet
int ip_receive(uint8_t *buffer, uint32_t len)
{
  if (!buffer || len < sizeof(eth_header_t))
  {
    return -1;
  }

  eth_header_t *eth = (eth_header_t *)buffer;

  // Handle ARP packets
  if (eth->ethertype == htons(ETH_TYPE_ARP))
  {
    return arp_process(buffer, len);
  }

  if (eth->ethertype != htons(ETH_TYPE_IP))
    return 0;

  if (len < sizeof(eth_header_t) + sizeof(ip_header_t))
    return -1;

  ip_header_t *ip = (ip_header_t *)(buffer + sizeof(eth_header_t));

  uint32_t src_ip = __builtin_bswap32(ip->src_ip);
  uint8_t *payload = buffer + sizeof(eth_header_t) + sizeof(ip_header_t);
  uint32_t payload_len = len - sizeof(eth_header_t) - sizeof(ip_header_t);

  if (ip->protocol == IP_PROTO_ICMP)
  {
    return icmp_process(payload, payload_len);
  }
  else if (ip->protocol == IP_PROTO_TCP)
  {
    // Debug: Show we received TCP packet
    static int tcp_packet_count = 0;
    tcp_packet_count++;
    if (tcp_packet_count <= 5)
    { // Only show first 5 to avoid spam
      puts("[IP] Received TCP packet #");
      putc('0' + tcp_packet_count);
      puts("\n");
    }
    return tcp_process(src_ip, payload, payload_len);
  }
  else if (ip->protocol == IP_PROTO_UDP)
  {
    return udp_process(src_ip, payload, payload_len);
  }

  return 0;
}

// UDP and DNS Implementation

static int udp_send_packet(uint32_t dest_ip, uint16_t dest_port,
                           uint16_t src_port, const uint8_t *data,
                           uint16_t len)
{
  network_interface_t *net = netif_get_default();
  if (!net)
    return -1;

  uint8_t buf[1500];
  udp_header_t *udp = (udp_header_t *)buf;

  udp->src_port = htons(src_port);
  udp->dest_port = htons(dest_port);
  udp->length = htons(sizeof(udp_header_t) + len);
  udp->checksum = 0; // Optional in IPv4

  for (int i = 0; i < len; i++)
  {
    buf[sizeof(udp_header_t) + i] = data[i];
  }

  return ip_send(dest_ip, IP_PROTO_UDP, buf, sizeof(udp_header_t) + len);
}

// Helper string functions
static int str_cmp(const char *a, const char *b)
{
  while (*a && *a == *b)
  {
    a++;
    b++;
  }
  return *(const unsigned char *)a - *(const unsigned char *)b;
}

static void str_copy(char *dest, const char *src)
{
  while ((*dest++ = *src++))
    ;
}

// Simple DNS Cache (Last resolved)
static char last_dns_host[64];
static uint32_t last_dns_ip;

// Static DNS entries for common domains (workaround for VirtIO RX issue)
static const struct
{
  const char *hostname;
  uint32_t ip;
} static_dns[] = {
    /* Fallback DNS entries for when UDP is unreliable or blocked */
    {"httpforever.com", 0x92BE3E27}, // 146.190.62.39
    {"www.httpforever.com", 0x92BE3E27},
    /* example.com is behind Cloudflare; use their latest edge IPs */
    {"example.com", 0x5DB8D40E},      // 93.184.216.14 (ICANN)
    {"www.example.com", 0x5DB8D40E},
    {"httpbin.org", 0x36F7F036},      // 54.247.240.54
    {"www.httpbin.org", 0x36F7F036},
    {"google.com", 0x8EFA7F0E},       // 142.250.127.14
    {"www.google.com", 0x8EFA7F0E},
    {"localhost", 0x0A000202},        // 10.0.2.2 (QEMU gateway to host)
    {"neverssl.com", 0x22DF7C2D},     // 34.223.124.45 (plain HTTP test site)
    {"www.neverssl.com", 0x22DF7C2D},
    {"example.org", 0x68120218},      // 104.18.2.24 (Cloudflare)
    {"www.example.org", 0x68120218},
    {NULL, 0}};

// Helper to check if hostname is an IPv4 address
static bool is_ipv4_literal(const char *s) {
    if (!s || !*s) return false;
    int dots = 0;
    int digits = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            digits++;
        } else if (*s == '.') {
            if (digits == 0) return false; // .. or . at start
            dots++;
            digits = 0;
        } else {
            return false;
        }
        s++;
    }
    return dots == 3 && digits > 0;
}

static uint32_t parse_ip_literal(const char *s) {
    uint32_t ip = 0;
    const char *p = s;
    for (int i = 0; i < 4; i++) {
        uint32_t part = 0;
        while (*p >= '0' && *p <= '9') {
            part = part * 10 + (*p - '0');
            p++;
        }
        ip = (ip << 8) | (part & 0xFF);
        if (*p == '.') p++;
    }
    return ip;
}

static void dns_hostname_lower(char *out, const char *in, int max)
{
  int i = 0;
  while (i < max - 1 && in[i])
  {
    char c = in[i];
    if (c >= 'A' && c <= 'Z')
      c = (char)(c + 32);
    out[i++] = c;
  }
  out[i] = 0;
}

int dns_resolve(const char *hostname)
{
  // Check if it's already an IP address
  if (is_ipv4_literal(hostname))
  {
    return parse_ip_literal(hostname);
  }

  char host_l[256];
  dns_hostname_lower(host_l, hostname, (int)sizeof(host_l));
  if (!host_l[0])
    return 0;

  // Check rudimentary cache (normalized host)
  if (str_cmp(last_dns_host, host_l) == 0 && last_dns_ip != 0)
  {
    return last_dns_ip;
  }

  // Check static DNS entries first (these always work)
  for (int i = 0; static_dns[i].hostname != NULL; i++)
  {
    if (str_cmp(host_l, static_dns[i].hostname) == 0)
    {
      last_dns_ip = static_dns[i].ip;
      str_copy(last_dns_host, host_l);
      puts("[DNS] Using static entry for ");
      puts(host_l);
      puts("\n");
      return static_dns[i].ip;
    }
  }

  uint8_t buf[512];
  dns_header_t *dns = (dns_header_t *)buf;

  // Construct Query
  dns->id = htons(0xCAFE);
  dns->flags = htons(0x0100); // Standard Query, Recursion Desired
  dns->q_count = htons(1);
  dns->ans_count = 0;
  dns->auth_count = 0;
  dns->add_count = 0;

  // QNAME
  uint8_t *q = buf + sizeof(dns_header_t);
  const char *p = host_l;

  while (*p)
  {
    const char *dot = p;
    while (*dot && *dot != '.')
      dot++;

    int label_len = dot - p;
    if (label_len > 63)
      return 0; // Too long

    *q++ = label_len;

    for (int i = 0; i < label_len; i++)
      *q++ = *p++;

    if (*p == '.')
      p++; // Skip dot
  }
  *q++ = 0; // Root

  // QTYPE (A=1)
  *(uint16_t *)q = htons(1);
  q += 2;
  // QCLASS (IN=1)
  *(uint16_t *)q = htons(1);
  q += 2;

  int query_len = q - buf;

  // Send to QEMU's DNS server (10.0.2.3 in user-mode networking)
  network_interface_t *net = netif_get_default();
  uint32_t dns_server = 0x0A000203; // 10.0.2.3 (QEMU user-mode DNS)
  if (net && net->dns_server)
    dns_server = net->dns_server;

  // Clear previous DNS answer
  last_dns_ip = 0;
  str_copy(last_dns_host, host_l);

  for (int retry = 0; retry < 5; retry++)
  {
    udp_send_packet(dns_server, 53, (uint16_t)(52001 + retry), buf, query_len);

    uint32_t start = get_ticks();
    int spins = 0;
    while (spins < 20000)
    {
      spins++;
      if ((get_ticks() - start) >= 50)
        break;
      for (int k = 0; k < 4; k++)
        netif_poll();
      if (last_dns_ip != 0)
        return last_dns_ip;
      for (volatile int i = 0; i < 1500; i++)
        ;
    }
  }

  return 0; /* Failed */
}

// UDP Process (extracted from ip_receive dispatch)
int udp_process(uint32_t src_ip, const uint8_t *packet, uint32_t len)
{
  extern void puts(const char *);
  (void)src_ip;
  if (len < sizeof(udp_header_t))
    return -1;

  const udp_header_t *udp = (const udp_header_t *)packet;
  uint32_t payload_len = htons(udp->length) - sizeof(udp_header_t);
  const uint8_t *data = packet + sizeof(udp_header_t);

  uint16_t src_port = htons(udp->src_port);
  uint16_t dst_port = htons(udp->dest_port);

  // Debug: Show UDP packet info
  static int udp_count = 0;
  udp_count++;
  if (udp_count <= 10)
  {
    puts("[UDP] Received packet #");
    extern void putc(char);
    putc('0' + (udp_count / 10));
    putc('0' + (udp_count % 10));
    puts(" src=");
    putc('0' + (src_port / 10));
    putc('0' + (src_port % 10));
    puts(" dst=");
    putc('0' + (dst_port / 10));
    putc('0' + (dst_port % 10));
    puts("\n");
  }

  // Check if DHCP response (Source port 67, dest port 68)
  if (src_port == 67 && dst_port == 68)
  {
    puts("[DHCP] Received DHCP packet! payload_len=");
    extern void putc(char);
    char buf[8];
    buf[0] = '0' + (payload_len / 1000) % 10;
    buf[1] = '0' + (payload_len / 100) % 10;
    buf[2] = '0' + (payload_len / 10) % 10;
    buf[3] = '0' + payload_len % 10;
    buf[4] = '\0';
    puts(buf);
    puts("\n");

    extern int dhcp_process(network_interface_t * iface, const uint8_t *packet, uint32_t len);
    network_interface_t *iface = netif_get_default();
    if (iface)
    {
      int result = dhcp_process(iface, data, payload_len);
      puts("[DHCP] dhcp_process returned: ");
      putc('0' + (result & 0xF));
      puts("\n");
      return result;
    }
    return 0;
  }

  // Check if DNS response (Source port 53)
  if (src_port == 53)
  {
    puts("[DNS] Received DNS response!\n");

    // Parse DNS Response
    if (payload_len < sizeof(dns_header_t))
    {
      puts("[DNS] Payload too small\n");
      return -1;
    }
    const dns_header_t *d = (const dns_header_t *)data;

    // Skip Header
    const uint8_t *p = data + sizeof(dns_header_t);

    // Skip Questions
    int q_count = htons(d->q_count);
    puts("[DNS] Questions: ");
    extern void putc(char);
    putc('0' + q_count);
    puts("\n");

    for (int i = 0; i < q_count; i++)
    {
      while (*p != 0 && p < data + payload_len)
        p += (*p) + 1; // Skip labels
      p++;             // Skip root
      p += 4;          // Skip QTYPE, QCLASS
    }

    // Parse Answers
    int ans_count = htons(d->ans_count);
    puts("[DNS] Answers: ");
    putc('0' + ans_count);
    puts("\n");

    for (int i = 0; i < ans_count && p < data + payload_len; i++)
    {
      // Name pointer (could be compressed 0xC0xx)
      if ((*p & 0xC0) == 0xC0)
        p += 2;
      else
      {
        while (*p != 0 && p < data + payload_len)
          p += (*p) + 1;
        p++;
      }

      if (p + 10 > data + payload_len)
        break;

      uint16_t type = (p[0] << 8) | p[1];
      // uint16_t class = (p[2] << 8) | p[3];
      // uint32_t ttl ...
      uint16_t dlen = (p[8] << 8) | p[9];

      p += 10; // Header of RR

      if (type == 1 && dlen == 4 && p + 4 <= data + payload_len)
      { // A Record
        uint32_t ip = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        last_dns_ip = ip; // Store global result
        puts("[DNS] Got IP: ");
        putc('0' + p[0] / 100);
        putc('0' + (p[0] / 10) % 10);
        putc('0' + p[0] % 10);
        putc('.');
        putc('0' + p[1] / 100);
        putc('0' + (p[1] / 10) % 10);
        putc('0' + p[1] % 10);
        putc('.');
        putc('0' + p[2] / 100);
        putc('0' + (p[2] / 10) % 10);
        putc('0' + p[2] % 10);
        putc('.');
        putc('0' + p[3] / 100);
        putc('0' + (p[3] / 10) % 10);
        putc('0' + p[3] % 10);
        puts("\n");
        return 0;
      }
      p += dlen;
    }
  }
  return 0;
}

/* TCP IMPLEMENTATION */

// TCP State
typedef enum
{
  TCP_CLOSED,
  TCP_SYN_SENT,
  TCP_ESTABLISHED,
  TCP_FIN_WAIT
} tcp_state_t;

static struct
{
  tcp_state_t state;
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;
  uint32_t snd_nxt;
  uint32_t rcv_nxt;
  // Larger receive buffer for better performance
  uint8_t rx_buffer[16384]; // 16KB buffer
  uint32_t rx_len;
  uint32_t rx_processed;
  volatile bool has_data;
} tcb;

// TCP Pseudo-Header for Checksum
typedef struct
{
  uint32_t src_ip;
  uint32_t dest_ip;
  uint8_t reserved;
  uint8_t protocol;
  uint16_t tcp_length;
} __attribute__((packed)) tcp_pseudo_header_t;

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip,
                             const void *data, uint16_t len)
{
  tcp_pseudo_header_t ph;
  ph.src_ip = htonl(src_ip);
  ph.dest_ip = htonl(dest_ip);
  ph.reserved = 0;
  ph.protocol = IP_PROTO_TCP;
  ph.tcp_length = htons(len);

  uint32_t sum = 0;

  // Sum pseudo-header in network byte order (big-endian)
  const uint8_t *ptr = (const uint8_t *)&ph;
  for (size_t i = 0; i < sizeof(tcp_pseudo_header_t); i += 2)
  {
    uint16_t word = (ptr[i] << 8) | ptr[i + 1];
    sum += word;
  }

  // Sum TCP segment in network byte order (big-endian)
  const uint8_t *data_ptr = (const uint8_t *)data;
  for (size_t i = 0; i < (size_t)(len & ~1); i += 2)
  {
    uint16_t word = (data_ptr[i] << 8) | data_ptr[i + 1];
    sum += word;
  }

  if (len & 1)
  {
    uint16_t word = data_ptr[len - 1] << 8; // high byte, low byte 0
    sum += word;
  }

  while (sum >> 16)
  {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return htons(~sum);
}

static int tcp_send_packet(uint32_t dest_ip, uint16_t dest_port,
                           uint16_t src_port, uint32_t seq, uint32_t ack,
                           uint8_t flags, const uint8_t *payload,
                           uint32_t payload_len)
{
  network_interface_t *net = netif_get_default();
  if (!net)
    return -1;

  uint8_t buf[1500];
  tcp_header_t *tcp = (tcp_header_t *)buf;

  tcp->src_port = htons(src_port);
  tcp->dest_port = htons(dest_port);
  tcp->sequence = htonl(seq);
  tcp->ack_num = htonl(ack);
  tcp->data_offset_reserved = (sizeof(tcp_header_t) / 4) << 4;
  tcp->flags = flags;
  tcp->window_size = htons(8192); // 8KB Window
  tcp->checksum = 0;
  tcp->urgent_pointer = 0;

  // Copy payload
  if (payload && payload_len > 0)
  {
    for (uint32_t i = 0; i < payload_len; i++)
    {
      buf[sizeof(tcp_header_t) + i] = payload[i];
    }
  }

  // Calculate Checksum
  uint16_t tcp_len = sizeof(tcp_header_t) + payload_len;
  tcp->checksum = tcp_checksum(net->ip_addr, dest_ip, buf, tcp_len);

  return ip_send(dest_ip, IP_PROTO_TCP, buf, tcp_len);
}

// Process Incoming TCP
int tcp_process(uint32_t src_ip, const uint8_t *packet, uint32_t len)
{
  extern void puts(const char *);
  (void)src_ip;
  if (len < sizeof(tcp_header_t))
    return -1;

  const tcp_header_t *tcp = (const tcp_header_t *)packet;

  // Check if it belongs to our connection
  if (tcb.state == TCP_CLOSED)
    return 0;
  // We should check ports but ignoring for simplicity in demo

  uint32_t seq = __builtin_bswap32(tcp->sequence);
  uint32_t ack = __builtin_bswap32(tcp->ack_num);
  uint32_t seg_len = len - ((tcp->data_offset_reserved >> 4) * 4);

  if (tcb.state == TCP_SYN_SENT)
  {
    puts("[TCP] Received packet in SYN_SENT state, flags=0x");
    extern void putc(char);
    const char hex[] = "0123456789ABCDEF";
    putc(hex[(tcp->flags >> 4) & 0xF]);
    putc(hex[tcp->flags & 0xF]);
    puts("\n");

    if ((tcp->flags & TCP_FLAG_SYN) && (tcp->flags & TCP_FLAG_ACK))
    {
      // Received SYN-ACK
      puts("[TCP] Got SYN-ACK! Sending ACK...\n");
      tcb.rcv_nxt = seq + 1;
      tcb.snd_nxt = ack;
      tcb.state = TCP_ESTABLISHED;

      // Send ACK
      tcp_send_packet(tcb.remote_ip, tcb.remote_port, tcb.local_port,
                      tcb.snd_nxt, tcb.rcv_nxt, TCP_FLAG_ACK, NULL, 0);
      return 1;
    }
  }
  else if (tcb.state == TCP_ESTABLISHED)
  {
    if (tcp->flags & TCP_FLAG_ACK)
    {
      // Handle ACK updates
    }
    if (seg_len > 0)
    {
      // Data received
      // Simplified: Copy to buffer
      uint32_t hdr_len = (tcp->data_offset_reserved >> 4) * 4;
      const uint8_t *data = packet + hdr_len;

      if (tcb.rx_len + seg_len < 16384)
      {
        for (uint32_t i = 0; i < seg_len; i++)
        {
          tcb.rx_buffer[tcb.rx_len + i] = data[i];
        }
        tcb.rx_len += seg_len;
        tcb.has_data = true;

        tcb.rcv_nxt += seg_len;

        // Send ACK
        tcp_send_packet(tcb.remote_ip, tcb.remote_port, tcb.local_port,
                        tcb.snd_nxt, tcb.rcv_nxt, TCP_FLAG_ACK, NULL, 0);
      }
    }
    if (tcp->flags & TCP_FLAG_FIN)
    {
      tcb.rcv_nxt++;
      tcp_send_packet(tcb.remote_ip, tcb.remote_port, tcb.local_port,
                      tcb.snd_nxt, tcb.rcv_nxt, TCP_FLAG_ACK | TCP_FLAG_FIN,
                      NULL, 0);
      tcb.state = TCP_CLOSED;
      return 1;
    }
  }
  return 0;
}

// Update ip_receive to call tcp_process
// We invoke this manually in the dispatch loop above or modify ip_receive

int tcp_connect(uint32_t dest_ip, uint16_t dest_port)
{
  tcb.state = TCP_CLOSED;
  tcb.rx_len = 0;
  tcb.rx_processed = 0;
  tcb.has_data = false;

  tcb.remote_ip = dest_ip;
  tcb.remote_port = dest_port;
  tcb.local_port = 10000 + (get_ticks() % 50000); // Random port
  tcb.snd_nxt = get_ticks();                      // Random ISN
  tcb.rcv_nxt = 0;

  network_interface_t *net = netif_get_default();
  if (!net)
  {
    extern void puts(const char *);
    puts("[TCP] ERROR: No network interface!\n");
    return -1;
  }

  /* link_up can read wrong on some builds; rely on driver + IP */
  if (net->ip_addr == 0)
  {
    extern void puts(const char *);
    puts("[TCP] ERROR: No IP address configured!\n");
    return -1;
  }

  tcb.local_ip = net->ip_addr;

  // Send SYN with retries
  extern void puts(const char *);
  extern void putc(char);

  for (int retry = 0; retry < 3; retry++)
  {
    if (retry > 0)
    {
      puts("[TCP] Retry ");
      putc('0' + (char)retry);
      puts("/3...\n");
    }

    int sent = tcp_send_packet(tcb.remote_ip, tcb.remote_port, tcb.local_port, tcb.snd_nxt,
                               0, TCP_FLAG_SYN, NULL, 0);

    if (sent < 0)
    {
      puts("[TCP] ERROR: Failed to send SYN packet!\n");
      continue;
    }

    tcb.state = TCP_SYN_SENT;

    uint32_t start = get_ticks();
    int spins = 0;
    while (tcb.state != TCP_ESTABLISHED && spins < 60000)
    {
      spins++;
      if ((get_ticks() - start) >= 320)
        break;
      for (int k = 0; k < 8; k++)
        netif_poll();
      if (tcb.state == TCP_ESTABLISHED)
      {
        puts("[TCP] Connection established\n");
        return 0;
      }
      for (volatile int i = 0; i < 2000; i++);
    }
    
    if (tcb.state != TCP_ESTABLISHED) {
        puts("[TCP] Timeout - no SYN-ACK received\n");
    }

    if (tcb.state == TCP_ESTABLISHED)
      return 0; // Connected successfully
  }

  tcb.state = TCP_CLOSED;
  puts("[TCP] Connection failed after all retries\n");
  return -1; // Failed after retries
}

int tcp_send(int socket, const void *data, uint32_t len)
{
  (void)socket;
  if (tcb.state != TCP_ESTABLISHED)
    return -1;

  tcp_send_packet(tcb.remote_ip, tcb.remote_port, tcb.local_port, tcb.snd_nxt,
                  tcb.rcv_nxt, TCP_FLAG_PSH | TCP_FLAG_ACK,
                  (const uint8_t *)data, len);

  tcb.snd_nxt += len;
  return len;
}

int tcp_receive(int socket, void *buffer, uint32_t max_len)
{
  (void)socket;
  uint32_t start = get_ticks();
  int spins = 0;
  while (!tcb.has_data && tcb.state == TCP_ESTABLISHED && spins < 80000)
  {
    spins++;
    netif_poll();
    if ((get_ticks() - start) > 150)
      break;
    for (volatile int i = 0; i < 2000; i++);
  }

  if (tcb.rx_len <= tcb.rx_processed)
    return 0;

  uint32_t available = tcb.rx_len - tcb.rx_processed;
  uint32_t to_copy = (available > max_len) ? max_len : available;

  char *buf = (char *)buffer;
  for (uint32_t i = 0; i < to_copy; i++)
  {
    buf[i] = tcb.rx_buffer[tcb.rx_processed + i];
  }
  tcb.rx_processed += to_copy;

  if (tcb.rx_processed == tcb.rx_len)
  {
    tcb.has_data = false; // All read
  }

  return to_copy;
}

int tcp_close(int socket)
{
  (void)socket;
  if (tcb.state == TCP_ESTABLISHED)
  {
    tcp_send_packet(tcb.remote_ip, tcb.remote_port, tcb.local_port, tcb.snd_nxt,
                    tcb.rcv_nxt, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    tcb.state = TCP_CLOSED;
  }
  return 0;
}
