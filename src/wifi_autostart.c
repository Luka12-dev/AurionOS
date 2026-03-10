/*
 * WiFi Autostart - Simplified for VirtIO mode
 * WiFi is not supported - network uses VirtIO
*/

#include "wifi_autostart.h"

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
    /* No auto-start in VirtIO mode */
}
