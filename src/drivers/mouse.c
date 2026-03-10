#include "mouse.h"
#include <stdint.h>
#include <stdbool.h>

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* External dependencies */
extern int c_mouse_read(void);
extern int gpu_get_width(void);
extern int gpu_get_height(void);

/* Mouse state */
static int mouse_x = 160;
static int mouse_y = 100;
static bool mouse_left = false;
static bool mouse_right = false;
static bool mouse_initialized = false;
static int mouse_limit_w = 320;
static int mouse_limit_h = 200;
static int mouse_divisor = 1;
static int mouse_accum_x = 0;
static int mouse_accum_y = 0;

/* Wait for PS/2 controller to be ready for write */
static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(0x64) & 0x02) == 0) return;
    }
}

/* Read byte from mouse buffer */
static uint8_t mouse_read_byte(void) {
    int timeout = 1000000;
    while (timeout-- > 0) {
        int b = c_mouse_read();
        if (b != -1) return (uint8_t)b;
        for(volatile int i=0; i<100; i++);
    }
    return 0;
}

/* Send command to mouse */
static void mouse_cmd(uint8_t cmd) {
    mouse_wait_write();
    outb(0x64, 0xD4);  /* Tell controller next byte goes to mouse */
    mouse_wait_write();
    outb(0x60, cmd);
}

/* Initialize PS/2 mouse */
int mouse_init(void) {
    /* Always reinitialize - don't skip if already initialized
     * This fixes the bug where mouse doesn't work after GUI app exits */
    // if (mouse_initialized) return 0;

    /* Flush any pending data by reading buffer until empty */
    while (c_mouse_read() != -1);
    
    /* Enable auxiliary device (mouse) */
    mouse_wait_write();
    outb(0x64, 0xA8);
    
    /* Read controller command byte */
    mouse_wait_write();
    outb(0x64, 0x20);
    
    /* Buffer might be empty if we just enabled, wait slightly? */
    for(volatile int i=0; i<1000; i++);
    
    /* Write back command byte with IRQ12 enabled */
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, 0x47); /* Enable IRQ1, IRQ12, Translate, System */
    
    /* Reset mouse */
    mouse_cmd(0xFF);
    mouse_read_byte();  /* ACK */
    mouse_read_byte();  /* Self-test result */
    mouse_read_byte();  /* Device ID */
    
    /* Use default settings */
    mouse_cmd(0xF6);
    mouse_read_byte();  /* ACK */
    
    /* Enable mouse data reporting */
    mouse_cmd(0xF4);
    mouse_read_byte();  /* ACK */
    
    mouse_initialized = true;
    
    /* Set initial bounds */
    int w = gpu_get_width();
    int h = gpu_get_height();
    if (w <= 0) w = 320;
    if (h <= 0) h = 200;
    
    mouse_limit_w = w;
    mouse_limit_h = h;
    
    /* Center mouse */
    mouse_x = w / 2;
    mouse_y = h / 2;
    mouse_accum_x = 0;
    mouse_accum_y = 0;
    
    return 0;
}

static uint8_t mouse_packet[3];
static int mouse_packet_idx = 0;

/* Poll mouse - read from interrupt buffer */
void mouse_poll(void) {
    if (!mouse_initialized) return;
    
    while (1) {
        int b = c_mouse_read();
        if (b == -1) break;
        
        if (mouse_packet_idx == 0) {
            if ((b & 0x08) == 0x08) { /* Bit 3 must be set */
                mouse_packet[0] = (uint8_t)b;
                mouse_packet_idx = 1;
            }
        } else if (mouse_packet_idx == 1) {
            mouse_packet[1] = (uint8_t)b;
            mouse_packet_idx = 2;
        } else {
            mouse_packet[2] = (uint8_t)b;
            mouse_packet_idx = 0;
            
            uint8_t status = mouse_packet[0];
            int dx = (int8_t)mouse_packet[1];
            int dy = (int8_t)mouse_packet[2];
            
            if (status & 0x40) dx = 0;
            if (status & 0x80) dy = 0;
            
            mouse_left = (status & 0x01) ? true : false;
            mouse_right = (status & 0x02) ? true : false;
            
            /* Apply scaled mouse movement with fractional accumulation */
            mouse_accum_x += dx;
            mouse_accum_y += dy;
            mouse_x += mouse_accum_x / mouse_divisor;
            mouse_y -= mouse_accum_y / mouse_divisor;
            mouse_accum_x %= mouse_divisor;
            mouse_accum_y %= mouse_divisor;
            
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x >= mouse_limit_w) mouse_x = mouse_limit_w - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y >= mouse_limit_h) mouse_y = mouse_limit_h - 1;
        }
    }
}

/* Get mouse state */
int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
bool mouse_get_left(void) { return mouse_left; }
bool mouse_get_right(void) { return mouse_right; }

/* Set mouse bounds */
void mouse_set_bounds(int width, int height) {
    mouse_limit_w = width;
    mouse_limit_h = height;
    
    if (mouse_x >= width) mouse_x = width - 1;
    if (mouse_y >= height) mouse_y = height - 1;
}
