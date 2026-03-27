// Developer Note: still working on WIFI drivers, for now Simulation.

/*
 * NETMODE Command - Network Driver Mode Selection
 * Simplified for VirtIO-only mode
*/

extern void c_puts(const char *s);

#define puts c_puts

/* NETMODE command - Show network driver info */
int cmd_netmode(const char *args) {
    (void)args;
    
    puts("=== Network Driver Status ===\n");
    puts("Active driver: VirtIO Network\n");
    puts("\n");
    puts("VirtIO is the only supported network driver.\n");
    puts("Use NETSTART to initialize network.\n");
    puts("Use IPCONFIG to check network status.\n");
    puts("Use WGET to download files.\n");
    
    return 0;
}
