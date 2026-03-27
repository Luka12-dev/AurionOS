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

void cmd_net_test(int argc, char** argv) {
    (void)argc;
    (void)argv;
    c_puts("\n--- AurionOS Network Diagnostic ---\n");

    // ERROR 0: PCI Probing
    c_puts("STEP 0: Probing PCI for Network Controllers... ");
    bool found_nic = false;
    for(int bus = 0; bus < 8; bus++) {
        for(int slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_config_read(bus, slot, 0, 0);
            if(vendor_device != 0xFFFFFFFF) {
                uint32_t class_rev = pci_config_read(bus, slot, 0, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                if(class_code == 0x02) { // Network Controller
                    found_nic = true;
                    c_puts("FOUND (");
                    // Print Hex Vendor/Device
                    for(int i=0; i<8; i++) {
                        uint8_t nibble = (vendor_device >> (28 - i*4)) & 0xF;
                        c_putc(nibble < 10 ? '0'+nibble : 'A'+nibble-10);
                    }
                    c_puts(") ");
                }
            }
        }
    }
    if(!found_nic) { c_puts("\nERROR: 0 (No NIC found on PCI bus)\n"); return; }
    c_puts("OK\n");

    // ERROR 1: Driver Initialization
    c_puts("STEP 1: Checking Driver Status... ");
    // Check if any interface is registered
    network_interface_t *default_if = netif_get_default();
    if(!default_if) { c_puts("\nERROR: 1 (No drivers initialized for found hardware)\n"); return; }
    c_puts("OK\n");

    // ERROR 2: IP/Link
    c_puts("STEP 2: Checking IP Configuration... ");
    if(default_if->ip_addr == 0) {
        c_puts("\nERROR: 2 (No IP assigned. DHCP failed?)\n");
        return;
    }
    c_puts("OK\n");

    // ERROR 3: Connection Test
    c_puts("STEP 3: Fetching example.com... ");
    char* content = (char*)0x2000000; // Use a safe temporary buffer
    uint32_t len = 0;
    extern int blaze_fetch(const char *url, char **out_content, uint32_t *out_len);
    
    int res = blaze_fetch("http://example.com", &content, &len);
    if(res != 0) {
        c_puts("\nERROR: 3 (HTTP Fetch failed)\n");
        return;
    }
    c_puts("OK\n");

    // FINAL STATUS
    c_puts("STATUS: WORKING\n");
    c_puts("Saving index.html to /index.html... ");
    save_file_content("/index.html", content, (int)len);
    c_puts("DONE\n");
}
