#include <stdint.h>
#include <stdbool.h>
#include "window_manager.h"
#include "network.h"
#include "pci.h"

extern void c_puts(const char *s);
extern void c_putc(char c);
extern int load_file_content(const char *filename, char *buffer, int max_len);
extern int save_file_content(const char *filename, const char *data, int len);
extern network_interface_t *netif_get_default(void);
extern int network_ensure_ready(void);
extern int dns_resolve(const char *hostname);
extern int tcp_connect(uint32_t dest_ip, uint16_t dest_port);
extern int tcp_send(int socket, const void *data, uint32_t len);
extern int tcp_receive(int socket, void *buffer, uint32_t max_len);
extern int tcp_close(int socket);
extern void netif_poll(void);
extern uint32_t get_ticks(void);

/* Test targets - multiple fallbacks for reliable testing */
static const struct {
    const char *name;
    const char *url;
    uint32_t static_ip;  /* 0 = use DNS */
} test_targets[] = {
    {"httpforever.com (Plain HTTP test site)", "http://httpforever.com/", 0x92BE3E27},
    {"neverssl.com (Always HTTP)", "http://neverssl.com/", 0x22DF7C2D},
    {"example.org (Cloudflare)", "http://example.org/", 0x68120218},
    {"example.com (Cloudflare)", "http://example.com/", 0x68121B78},
    {NULL, NULL, 0}
};

static void print_ip(uint32_t ip) {
    c_putc('0' + ((ip >> 24) & 0xFF) / 100 % 10);
    c_putc('0' + ((ip >> 24) & 0xFF) / 10 % 10);
    c_putc('0' + ((ip >> 24) & 0xFF) % 10);
    c_putc('.');
    c_putc('0' + ((ip >> 16) & 0xFF) / 100 % 10);
    c_putc('0' + ((ip >> 16) & 0xFF) / 10 % 10);
    c_putc('0' + ((ip >> 16) & 0xFF) % 10);
    c_putc('.');
    c_putc('0' + ((ip >> 8) & 0xFF) / 100 % 10);
    c_putc('0' + ((ip >> 8) & 0xFF) / 10 % 10);
    c_putc('0' + ((ip >> 8) & 0xFF) % 10);
    c_putc('.');
    c_putc('0' + (ip & 0xFF) / 100 % 10);
    c_putc('0' + (ip & 0xFF) / 10 % 10);
    c_putc('0' + (ip & 0xFF) % 10);
}

static void print_hex32(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        c_putc(hex[(val >> (i * 4)) & 0xF]);
    }
}

void cmd_net_test(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    int tests_passed = 0;
    int tests_total = 0;
    int tcp_passed = 0;
    int tcp_tests = 0;
    
    c_puts("\n+------------------------------------------------------------------+\n");
    c_puts("|          AURIONOS NETWORK DIAGNOSTIC SUITE v2.0                  |\n");
    c_puts("+------------------------------------------------------------------+\n\n");

    /*------------------------------------------------------------------
     * TEST 0: PCI BUS SCAN - Look for Network Interface Cards
     *------------------------------------------------------------------*/
    tests_total++;
    c_puts("[TEST 0] PCI Network Controller Detection\n");
    c_puts("---------------------------------------------\n");
    
    bool found_nic = false;
    int nic_count = 0;
    
    for (int bus = 0; bus < 8; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_config_read(bus, slot, 0, 0);
            if (vendor_device != 0xFFFFFFFF) {
                uint32_t class_rev = pci_config_read(bus, slot, 0, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                if (class_code == 0x02) { // Network Controller
                    found_nic = true;
                    nic_count++;
                    c_puts("  [OK] NIC #");
                    c_putc('0' + nic_count);
                    c_puts(" found at bus=");
                    c_putc('0' + bus);
                    c_puts(" slot=");
                    c_putc('0' + (slot / 10));
                    c_putc('0' + (slot % 10));
                    c_puts("  Vendor:Device=");
                    print_hex32(vendor_device);
                    c_puts("\n");
                }
            }
        }
    }
    
    if (!found_nic) {
        c_puts("  [FAIL] No Network Interface Card found on PCI bus!\n");
        c_puts("    Common NICs: VirtIO-Net (1AF4:1000), RTL8139 (10EC:8139),\n");
        c_puts("                 NE2000 (1050:0940), Intel e1000 (8086:100E)\n");
        c_puts("\n*** ABORTING: No network hardware detected ***\n\n");
        return;
    }
    tests_passed++;
    c_puts("  [OK] PASS: ");
    c_putc('0' + nic_count);
    c_puts(" NIC(s) detected\n\n");

    /*------------------------------------------------------------------
     * TEST 1: NETWORK DRIVER INITIALIZATION
     *------------------------------------------------------------------*/
    tests_total++;
    c_puts("[TEST 1] Network Driver Initialization\n");
    c_puts("----------------------------------------\n");
    c_puts("  Checking for existing network interface...\n");
    
    network_interface_t *iface = netif_get_default();
    if (!iface) {
        c_puts("  ! No interface registered, attempting init...\n");
        extern int wifi_driver_init(void);
        if (wifi_driver_init() != 0) {
            c_puts("  [FAIL] FAIL: Network driver initialization failed\n");
            c_puts("    - Check that NIC driver exists for your hardware\n");
            c_puts("    - Ensure VM network adapter is supported\n");
        } else {
            iface = netif_get_default();
            if (iface) {
                tests_passed++;
                c_puts("  [OK] PASS: Network driver initialized\n");
            }
        }
    } else {
        tests_passed++;
        c_puts("  [OK] PASS: Network interface already ready\n");
    }
    if (iface) {
        c_puts("    Interface: ");
        c_puts(iface->name);
        c_puts("\n    MAC: ");
        for (int i = 0; i < 6; i++) {
            const char *hex = "0123456789ABCDEF";
            c_putc(hex[(iface->mac_addr[i] >> 4) & 0xF]);
            c_putc(hex[iface->mac_addr[i] & 0xF]);
            if (i < 5) c_putc(':');
        }
        c_puts("\n");
    }
    c_puts("\n");

    /*------------------------------------------------------------------
     * TEST 2: IP CONFIGURATION (DHCP or Static)
     *------------------------------------------------------------------*/
    tests_total++;
    c_puts("[TEST 2] IP Configuration\n");
    c_puts("---------------------------\n");
    
    if (!iface || iface->ip_addr == 0) {
        c_puts("  No IP address - attempting DHCP...\n");
        
        extern int dhcp_init(network_interface_t *iface);
        extern int dhcp_discover(network_interface_t *iface);
        extern void netif_poll(void);
        
        if (iface && dhcp_init(iface) == 0) {
            c_puts("  DHCP client initialized, sending DISCOVER...\n");
            
            for (int attempt = 0; attempt < 3 && iface->ip_addr == 0; attempt++) {
                if (dhcp_discover(iface) == 0) {
                    c_puts("  Waiting for DHCP response (attempt ");
                    c_putc('0' + attempt + 1);
                    c_puts("/3)...\n");
                    
                    for (int poll = 0; poll < 2000 && iface->ip_addr == 0; poll++) {
                        netif_poll();
                    }
                }
            }
        }
    }
    
    if (!iface || iface->ip_addr == 0) {
        c_puts("  [FAIL] FAIL: No IP address configured\n");
        c_puts("    - DHCP may have failed or not completed\n");
        c_puts("    - Check VM network mode (NAT/Bridged)\n");
        c_puts("    - Try manual: NET-CONFIG <ip> <mask> <gw>\n");
    } else {
        tests_passed++;
        c_puts("  [OK] PASS: IP configured via DHCP/Static\n");
        c_puts("    IP Address: ");
        print_ip(iface->ip_addr);
        c_puts("\n    Netmask:    ");
        print_ip(iface->netmask);
        c_puts("\n    Gateway:    ");
        print_ip(iface->gateway);
        c_puts("\n    DNS Server: ");
        print_ip(iface->dns_server);
        c_puts("\n");
    }
    c_puts("\n");

    /*------------------------------------------------------------------
     * TEST 3: DNS RESOLUTION TEST
     *------------------------------------------------------------------*/
    tests_total++;
    c_puts("[TEST 3] DNS Resolution\n");
    c_puts("-------------------------\n");
    
    int dns_passed = 0;
    int dns_tests = 0;
    
    for (int i = 0; test_targets[i].name != NULL; i++) {
        dns_tests++;
        c_puts("  Testing ");
        c_puts(test_targets[i].name);
        c_puts("...\n");
        
        uint32_t ip = dns_resolve(test_targets[i].name);
        if (ip != 0) {
            dns_passed++;
            c_puts("    [OK] Resolved to: ");
            print_ip(ip);
            c_puts("\n");
        } else {
            c_puts("    [FAIL] DNS failed (will try static fallback)\n");
        }
    }
    
    if (dns_passed > 0) {
        tests_passed++;
        c_puts("  [OK] PASS: ");
        c_putc('0' + dns_passed);
        c_putc('/');
        c_putc('0' + dns_tests);
        c_puts(" DNS lookups succeeded\n");
    } else {
        c_puts("  [FAIL] FAIL: All DNS lookups failed\n");
        c_puts("    - Check DNS server configuration\n");
        c_puts("    - VM may not have internet connectivity\n");
    }
    c_puts("\n");

    /*------------------------------------------------------------------
     * TEST 4: TCP CONNECTION TEST
     *------------------------------------------------------------------*/
    tests_total++;
    c_puts("[TEST 4] TCP Connection\n");
    c_puts("-------------------------\n");
    
    if (!iface || iface->ip_addr == 0) {
        c_puts("  [SKIP] SKIP: No IP address (tests 2 failed)\n\n");
    } else {
        tcp_tests = 0;
        
        for (int i = 0; test_targets[i].name != NULL && tcp_tests < 2; i++) {
            uint32_t ip = dns_resolve(test_targets[i].name);
            if (ip == 0 && test_targets[i].static_ip != 0) {
                ip = test_targets[i].static_ip;
                c_puts("  Using static IP for ");
                c_puts(test_targets[i].name);
                c_puts("\n");
            }
            if (ip == 0) continue;
            
            tcp_tests++;
            c_puts("  Testing TCP to ");
            print_ip(ip);
            c_puts(":80...\n");
            
            if (tcp_connect(ip, 80) == 0) {
                tcp_passed++;
                c_puts("    [OK] TCP connected\n");
                tcp_close(0);
            } else {
                c_puts("    [FAIL] TCP connection failed\n");
            }
        }
        
        if (tcp_passed > 0) {
            tests_passed++;
            c_puts("  [OK] PASS: TCP working (");
            c_putc('0' + tcp_passed);
            c_putc('/');
            c_putc('0' + tcp_tests);
            c_puts(" tests)\n");
        } else {
            c_puts("  [FAIL] FAIL: TCP connections failing\n");
            c_puts("    - Firewall may be blocking outbound connections\n");
            c_puts("    - Gateway/Router may be misconfigured\n");
        }
    }
    c_puts("\n");

    /*------------------------------------------------------------------
     * TEST 5: HTTP FETCH TEST
     *------------------------------------------------------------------*/
    tests_total++;
    c_puts("[TEST 5] HTTP Request/Response\n");
    c_puts("-------------------------------\n");
    
    extern int blaze_fetch(const char *url, char **out_content, uint32_t *out_len);
    char *content = NULL;
    uint32_t len = 0;
    int http_passed = 0;
    int http_tests = 0;
    
    for (int i = 0; test_targets[i].name != NULL && http_tests < 2; i++) {
        http_tests++;
        c_puts("  Fetching ");
        c_puts(test_targets[i].url);
        c_puts("...\n");
        
        int res = blaze_fetch(test_targets[i].url, &content, &len);
        if (res == 0 && len > 0) {
            http_passed++;
            c_puts("    [OK] HTTP 200 OK, received ");
            char buf[16];
            int n = len;
            int pos = 0;
            if (n == 0) {
                buf[pos++] = '0';
            } else {
                int digits = 0;
                int tmp = n;
                while (tmp > 0) {
                    digits++;
                    tmp /= 10;
                }
                pos = digits;
                buf[pos] = '\0';
                while (n > 0) {
                    buf[--pos] = '0' + (n % 10);
                    n /= 10;
                }
            }
            buf[pos] = '\0';
            c_puts(buf);
            c_puts(" bytes\n");
            
            if (http_passed == 1 && len > 0) {
                c_puts("\n  Saving first successful response to /nettest.html\n");
                if (len > 8192) len = 8192;  /* Limit file size */
                save_file_content("/nettest.html", content, (int)len);
            }
            
            if (content) {
                extern void kfree(void *ptr);
                kfree(content);
                content = NULL;
            }
        } else {
            c_puts("    [FAIL] HTTP failed (code ");
            c_putc('0' + (res / 100) % 10);
            c_putc('0' + (res / 10) % 10);
            c_putc('0' + res % 10);
            c_puts(")\n");
        }
    }
    
    if (http_passed > 0) {
        tests_passed++;
        c_puts("  [OK] PASS: HTTP working (");
        c_putc('0' + http_passed);
        c_putc('/');
        c_putc('0' + http_tests);
        c_puts(" sites)\n");
    } else {
        c_puts("  [FAIL] FAIL: HTTP requests failing\n");
        c_puts("    - Server may require HTTPS (not supported)\n");
        c_puts("    - Server may block requests without proper headers\n");
    }
    c_puts("\n");

    /*------------------------------------------------------------------
     * SUMMARY
     *------------------------------------------------------------------*/
    c_puts("+------------------------------------------------------------------+\n");
    c_puts("|                        TEST SUMMARY                              |\n");
    c_puts("+------------------------------------------------------------------+\n");
    c_puts("|  Tests Passed: ");
    c_putc('0' + tests_passed);
    c_puts("/");
    c_putc('0' + tests_total);
    
    /* Pad to align */
    if (tests_passed < 10 && tests_total < 10) {
        c_puts("                                          |\n");
    } else {
        c_puts("                                         |\n");
    }
    
    if (tests_passed == tests_total) {
        c_puts("|  Status:  [OK] ALL TESTS PASSED - Network fully operational      |\n");
    } else if (tests_passed >= 3) {
        c_puts("|  Status:  [WARN] PARTIAL - Network functional with limitations   |\n");
    } else {
        c_puts("|  Status:  [FAIL] FAILED - Network connectivity issues detected   |\n");
    }
    
    c_puts("+------------------------------------------------------------------+\n\n");
    
    /* Troubleshooting hints based on results */
    if (tests_passed < tests_total) {
        c_puts("TROUBLESHOOTING GUIDE:\n");
        c_puts("----------------------\n");
        
        if (!found_nic) {
            c_puts("* No NIC found: Configure VM with VirtIO-Net, RTL8139, or NE2000\n");
            c_puts("  VMware e1000/vmxnet3 are NOT supported\n");
        }
        if (found_nic && (!iface || iface->ip_addr == 0)) {
            c_puts("* No IP: Check DHCP server (VM network settings -> NAT/Bridged)\n");
            c_puts("  Try QEMU: -netdev user,id=n0 -device virtio-net-pci,netdev=n0\n");
        }
        if (iface && iface->ip_addr != 0 && dns_passed == 0) {
            c_puts("* DNS failing: Try accessing sites by IP directly\n");
            c_puts("  http://146.190.62.39 (httpforever.com)\n");
        }
        if (iface && iface->ip_addr != 0 && tcp_passed == 0) {
            c_puts("* TCP failing: Check firewall, ensure outbound port 80 is open\n");
        }
        c_puts("\n");
    }
}
