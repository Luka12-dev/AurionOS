/* MicroPython command handler for AurionOS */

#include <stdint.h>

/* AurionOS system calls */
extern void c_puts(const char *s);
extern void set_attr(uint8_t a);

/* MicroPython runtime hooks: weak fallbacks keep kernel linkable when runtime is absent. */
__attribute__((weak)) void micropython_init(void) {}
__attribute__((weak)) void micropython_deinit(void) {}
__attribute__((weak)) void micropython_repl(void) {
    c_puts("MicroPython runtime not available in this build.\n");
}
__attribute__((weak)) int micropython_exec_file(const char *filename) {
    (void)filename;
    return -1;
}

/* String helpers */
static int mp_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void mp_strcpy(char *d, const char *s) {
    int i = 0;
    while (s[i]) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

static void mp_tolower(char *s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            s[i] += 32;
        }
    }
}

/* Command entry point */
int cmd_micropython(const char *args) {
    /* Initialize MicroPython */
    micropython_init();
    
    /* Skip leading spaces */
    while (*args == ' ') args++;
    
    if (*args == 0) {
        /* No argument: start REPL */
        micropython_repl();
    } else {
        /* Run a Python file */
        char filename[64];
        int i = 0;
        
        /* Copy filename */
        while (*args && *args != ' ' && i < 63) {
            filename[i++] = *args++;
        }
        filename[i] = 0;
        
        /* Try original case first */
        set_attr(0x0B);
        c_puts("Running: ");
        c_puts(filename);
        c_puts("\n");
        set_attr(0x07);
        
        int result = micropython_exec_file(filename);

        /* Only retry lowercase if the file wasn't found. */
        if (result == -2) {
            mp_tolower(filename);
            result = micropython_exec_file(filename);
        }
        
        if (result == 0) {
            set_attr(0x08);
            c_puts("\n[Process exited]\n");
            set_attr(0x07);
        }
    }
    
    /* Cleanup */
    micropython_deinit();
    
    return 0;
}
