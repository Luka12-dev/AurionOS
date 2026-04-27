/*
 * AurionOS Kernel Panic Handler
 */

#include <stdint.h>
#include <stdbool.h>

extern void c_puts(const char *s);
extern void c_putc(char c);
extern void set_attr(uint8_t a);

/* Graphics mode panic */
extern int gpu_get_width(void);
extern int gpu_get_height(void);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern int gpu_flush(void);
extern bool gpu_is_vesa(void);
extern int sys_kb_hit(void);
extern uint16_t sys_getkey(void);
extern uint32_t get_ticks(void);

static void clear_screen(void) {
    set_attr(0x4F);
    for (int i = 0; i < 80 * 25; i++) {
        c_putc(' ');
    }
}

static void halt_cpu(void) {
    __asm__ volatile("cli; hlt");
}

static void reboot_now(void) {
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    while (1) __asm__ volatile("hlt");
}

static void panic_wait_for_reboot(void) {
    uint32_t start = get_ticks();
    while (1) {
        if (sys_kb_hit()) {
            uint16_t k = sys_getkey();
            uint8_t lo = (uint8_t)(k & 0xFF);
            if (lo == 13 || lo == 10) reboot_now(); /* Enter */
        }
        /* Auto-reboot after ~10 seconds so users are not stuck. */
        if (get_ticks() - start > 1000) reboot_now();
        __asm__ volatile("hlt");
    }
}

static void panic_print_hex(uint32_t value) {
    const char hex[] = "0123456789ABCDEF";
    char buf[11] = "0x00000000";
    
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    
    c_puts(buf);
}

/* Graphical blue screen panic */
static void graphical_panic(const char *message, const char *file, int line, uint32_t error_code) {
    /* Disable interrupts */
    __asm__ volatile("cli");
    
    int sw = gpu_get_width();
    int sh = gpu_get_height();
    
    /* Blue screen background */
    gpu_fill_rect(0, 0, sw, sh, 0xFF0000AA);
    
    int y = 40;
    int x = 40;
    
    /* Title */
    gpu_draw_string(x, y, (const uint8_t *)"A problem has been detected and AurionOS has been shut down", 
                   0xFFFFFFFF, 0);
    y += 16;
    gpu_draw_string(x, y, (const uint8_t *)"to prevent damage to your computer.", 
                   0xFFFFFFFF, 0);
    y += 32;
    
    /* Error message */
    gpu_draw_string(x, y, (const uint8_t *)"KERNEL_PANIC", 0xFFFFFFFF, 0);
    y += 24;
    
    if (message) {
        gpu_draw_string(x, y, (const uint8_t *)"Error: ", 0xFFFFFFFF, 0);
        gpu_draw_string(x + 56, y, (const uint8_t *)message, 0xFFFFFFFF, 0);
        y += 16;
    }
    
    y += 16;
    
    /* Technical information */
    gpu_draw_string(x, y, (const uint8_t *)"Technical information:", 0xFFFFFFFF, 0);
    y += 24;
    
    if (file) {
        gpu_draw_string(x, y, (const uint8_t *)"*** File: ", 0xFFE0E0E0, 0);
        gpu_draw_string(x + 88, y, (const uint8_t *)file, 0xFFE0E0E0, 0);
        y += 16;
    }
    
    if (line > 0) {
        char line_buf[32] = "*** Line: ";
        int i = 10;
        int temp = line;
        if (temp == 0) {
            line_buf[i++] = '0';
        } else {
            char rev[16];
            int j = 0;
            while (temp > 0) {
                rev[j++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (j > 0) {
                line_buf[i++] = rev[--j];
            }
        }
        line_buf[i] = '\0';
        gpu_draw_string(x, y, (const uint8_t *)line_buf, 0xFFE0E0E0, 0);
        y += 16;
    }
    
    if (error_code != 0) {
        char code_buf[64] = "*** Code: 0x";
        int i = 12;
        const char hex[] = "0123456789ABCDEF";
        for (int shift = 28; shift >= 0; shift -= 4) {
            code_buf[i++] = hex[(error_code >> shift) & 0xF];
        }
        code_buf[i] = '\0';
        gpu_draw_string(x, y, (const uint8_t *)code_buf, 0xFFE0E0E0, 0);
        y += 16;
    }
    
    y += 24;
    
    /* Instructions */
    gpu_draw_string(x, y, (const uint8_t *)"If this is the first time you've seen this error screen,", 
                   0xFFFFFFFF, 0);
    y += 16;
    gpu_draw_string(x, y, (const uint8_t *)"restart your computer. If this screen appears again, follow", 
                   0xFFFFFFFF, 0);
    y += 16;
    gpu_draw_string(x, y, (const uint8_t *)"these steps:", 
                   0xFFFFFFFF, 0);
    y += 24;
    
    gpu_draw_string(x, y, (const uint8_t *)"Check to make sure any new hardware or software is properly installed.", 
                   0xFFFFFFFF, 0);
    y += 16;
    gpu_draw_string(x, y, (const uint8_t *)"If this is a new installation, ask your hardware or software manufacturer", 
                   0xFFFFFFFF, 0);
    y += 16;
    gpu_draw_string(x, y, (const uint8_t *)"for any AurionOS updates you might need.", 
                   0xFFFFFFFF, 0);
    y += 28;
    gpu_draw_string(x, y, (const uint8_t *)"Press ENTER to reboot now.", 0xFFFFFFFF, 0);
    y += 16;
    gpu_draw_string(x, y, (const uint8_t *)"Auto reboot in 10 seconds...", 0xFFE0E0E0, 0);
    
    gpu_flush();
    panic_wait_for_reboot();
}

void kernel_panic(const char *message, const char *file, int line, uint32_t error_code) {
    /* Check if we're in graphics mode */
    if (gpu_is_vesa()) {
        graphical_panic(message, file, line, error_code);
        return;
    }
    
    /* Text mode panic (fallback) */
    __asm__ volatile("cli");
    
    clear_screen();
    set_attr(0x4F); /* White on red */
    
    c_puts("\n\n");
    c_puts("  *** KERNEL PANIC ***\n\n");
    
    set_attr(0x0F);
    
    c_puts("  Error: ");
    c_puts(message);
    c_puts("\n\n");
    
    if (file) {
        c_puts("  File: ");
        c_puts(file);
        c_puts("\n");
    }
    
    if (line > 0) {
        c_puts("  Line: ");
        char line_buf[16];
        int i = 0;
        int temp = line;
        if (temp == 0) {
            line_buf[i++] = '0';
        } else {
            char rev[16];
            int j = 0;
            while (temp > 0) {
                rev[j++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (j > 0) {
                line_buf[i++] = rev[--j];
            }
        }
        line_buf[i] = '\0';
        c_puts(line_buf);
        c_puts("\n");
    }
    
    if (error_code != 0) {
        c_puts("  Code: ");
        panic_print_hex(error_code);
        c_puts("\n");
    }
    
    c_puts("\n");
    c_puts("  Press ENTER to reboot now.\n");
    c_puts("  Auto reboot in 10 seconds...\n");
    panic_wait_for_reboot();
}

/* Simple panic without file/line info */
void panic(const char *message) {
    kernel_panic(message, NULL, 0, 0);
}

/* Panic with error code */
void panic_code(const char *message, uint32_t code) {
    kernel_panic(message, NULL, 0, code);
}
