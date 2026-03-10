/*
 * VBE Graphics Driver for Aurion OS
 * Supports VESA linear framebuffer (set by bootloader) with double buffering
 * Falls back to VGA Mode 13h (320x200x256) if VESA unavailable
*/

#include <stdint.h>
#include <stdbool.h>

extern void c_puts(const char *s);
extern void c_putc(char c);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

/* Port I/O */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* VESA Boot Info at 0x9000 (set by bootloader) */
#define VBE_INFO_ADDR 0x9000

typedef struct
{
    uint32_t framebuffer; /* Offset 0: LFB physical address */
    uint16_t width;       /* Offset 4: X resolution */
    uint16_t height;      /* Offset 6: Y resolution */
    uint8_t bpp;          /* Offset 8: Bits per pixel */
    uint8_t reserved;     /* Offset 9 */
    uint16_t pitch;       /* Offset 10: Bytes per scan line */
    uint16_t mode_number; /* Offset 12: VBE mode number */
} __attribute__((packed)) VbeBootInfo;

static VbeBootInfo *vbe_info = (VbeBootInfo *)VBE_INFO_ADDR;

/* Driver state */
static bool vbe_active = false;
static uint32_t *framebuffer = 0;
static uint32_t *backbuffer = 0;
static int screen_width = 320;
static int screen_height = 200;
static int screen_bpp = 8;
static int screen_pitch = 320;
static bool double_buffered = false;

/* VGA state for fallback */
static bool vga_fallback = false;

/* 8x8 font bitmap */
static const uint8_t font_8x8[128][8] = {
    [0] = {0},
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    ['"'] = {0x6C, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['#'] = {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00},
    ['$'] = {0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00},
    ['%'] = {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00},
    ['&'] = {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00},
    ['\''] = {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    [')'] = {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    ['*'] = {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    ['+'] = {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    [','] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    ['-'] = {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    ['/'] = {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},
    ['0'] = {0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0x7C, 0x00},
    ['1'] = {0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x7E, 0x00},
    ['2'] = {0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x66, 0xFE, 0x00},
    ['3'] = {0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00},
    ['4'] = {0x0C, 0x1C, 0x3C, 0x6C, 0xFE, 0x0C, 0x0C, 0x00},
    ['5'] = {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00},
    ['6'] = {0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00},
    ['7'] = {0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    ['8'] = {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00},
    ['9'] = {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00},
    [':'] = {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    [';'] = {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30},
    ['<'] = {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    ['='] = {0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00},
    ['>'] = {0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00},
    ['?'] = {0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00},
    ['@'] = {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00},
    ['A'] = {0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00},
    ['B'] = {0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00},
    ['C'] = {0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00},
    ['D'] = {0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00},
    ['E'] = {0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00},
    ['F'] = {0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00},
    ['G'] = {0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00},
    ['H'] = {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00},
    ['I'] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['J'] = {0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00},
    ['K'] = {0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00},
    ['L'] = {0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00},
    ['M'] = {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    ['N'] = {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00},
    ['O'] = {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    ['P'] = {0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00},
    ['Q'] = {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x06},
    ['R'] = {0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00},
    ['S'] = {0x7C, 0xC6, 0x60, 0x38, 0x0C, 0xC6, 0x7C, 0x00},
    ['T'] = {0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['U'] = {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    ['V'] = {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00},
    ['W'] = {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00},
    ['X'] = {0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00},
    ['Y'] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00},
    ['Z'] = {0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00},
    ['['] = {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    ['\\'] = {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00},
    [']'] = {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    ['^'] = {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00},
    ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    ['`'] = {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['a'] = {0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00},
    ['b'] = {0xE0, 0x60, 0x60, 0x7C, 0x66, 0x66, 0xDC, 0x00},
    ['c'] = {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00},
    ['d'] = {0x1C, 0x0C, 0x0C, 0x7C, 0xCC, 0xCC, 0x76, 0x00},
    ['e'] = {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00},
    ['f'] = {0x38, 0x6C, 0x60, 0xF0, 0x60, 0x60, 0xF0, 0x00},
    ['g'] = {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8},
    ['h'] = {0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00},
    ['i'] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['j'] = {0x06, 0x00, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C},
    ['k'] = {0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00},
    ['l'] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['m'] = {0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    ['n'] = {0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00},
    ['o'] = {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    ['p'] = {0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0},
    ['q'] = {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E},
    ['r'] = {0x00, 0x00, 0xDC, 0x76, 0x66, 0x60, 0xF0, 0x00},
    ['s'] = {0x00, 0x00, 0x7C, 0xC0, 0x7C, 0x06, 0xFC, 0x00},
    ['t'] = {0x10, 0x30, 0x7C, 0x30, 0x30, 0x34, 0x18, 0x00},
    ['u'] = {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00},
    ['v'] = {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00},
    ['w'] = {0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0x6C, 0x00},
    ['x'] = {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00},
    ['y'] = {0x00, 0x00, 0xC6, 0xC6, 0xCE, 0x76, 0x06, 0xFC},
    ['z'] = {0x00, 0x00, 0xFE, 0x8C, 0x18, 0x32, 0xFE, 0x00},
    ['{'] = {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    ['|'] = {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    ['}'] = {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    ['~'] = {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

/* Setup VGA Mode 13h palette */
extern void setup_palette(void);
extern void set_mode_13h(void);

/* Detect Bochs VBE and return LFB base address, or 0 if not present */
extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
extern void c_puts(const char *s); /* Use c_puts for debug logging */
extern void c_putc(char c);

/* Check if Bochs VBE hardware is actually present and responding */
static bool bochs_vbe_present(void)
{
    outw(0x01CE, 0x00); /* VBE_DISPI_INDEX_ID */
    uint16_t id = inw(0x01CF);
    if ((id & 0xFFF0) == 0xB0C0)
        return true;
    return false;
}

static uint32_t detect_bochs_vbe_lfb(void)
{
    /* Only scan PCI if Bochs VBE is actually responding.
       On VMware SVGA and real hardware GPUs, Bochs VBE ports are absent.
       Their PCI BAR0 is an MMIO register window, NOT a framebuffer.
       Writing pixels there would corrupt GPU state instead of displaying them. */
    if (!bochs_vbe_present())
    {
        c_puts("[VBE] Bochs VBE not detected - will use VGA fallback\n");
        return 0;
    }

    c_puts("[VBE] Scanning PCI for Display Controller...\n");
    for (int bus = 0; bus < 256; bus++)
    {
        for (int dev = 0; dev < 32; dev++)
        {
            uint32_t vendor_dev = pci_config_read(bus, dev, 0, 0x00);
            if ((vendor_dev & 0xFFFF) == 0xFFFF)
                continue;

            uint32_t class_rev = pci_config_read(bus, dev, 0, 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;

            if (class_code == 0x03)
            {
                c_puts("[VBE] Found Display Controller at PCI ");
                char msg[32];
                int i = 0;
                uint8_t val = bus;
                msg[i++] = "0123456789ABCDEF"[(val >> 4) & 0xF];
                msg[i++] = "0123456789ABCDEF"[val & 0xF];
                msg[i++] = ':';
                val = dev;
                msg[i++] = "0123456789ABCDEF"[(val >> 4) & 0xF];
                msg[i++] = "0123456789ABCDEF"[val & 0xF];
                msg[i++] = '\n';
                msg[i] = 0;
                c_puts(msg);

                uint32_t bar0 = pci_config_read(bus, dev, 0, 0x10);
                uint32_t bar1 = pci_config_read(bus, dev, 0, 0x14);

                if ((vendor_dev & 0xFFFF) == 0x1234 || (vendor_dev & 0xFFFF) == 0x1AF4)
                    c_puts("[VBE] Found Bochs/VirtIO Graphics!\n");

                if ((bar0 & 0x01) == 0 && bar0 != 0)
                {
                    uint32_t addr = bar0 & 0xFFFFFFF0;
                    c_puts("[VBE] LFB Address detected (BAR0): ");
                    for (int j = 7; j >= 0; j--)
                        c_putc("0123456789ABCDEF"[(addr >> (j * 4)) & 0xF]);
                    c_puts("\n");
                    return addr;
                }
                if ((bar1 & 0x01) == 0 && bar1 != 0)
                {
                    uint32_t addr = bar1 & 0xFFFFFFF0;
                    c_puts("[VBE] LFB Address detected (BAR1): ");
                    for (int j = 7; j >= 0; j--)
                        c_putc("0123456789ABCDEF"[(addr >> (j * 4)) & 0xF]);
                    c_puts("\n");
                    return addr;
                }
            }
        }
    }
    c_puts("[VBE] PCI scan found no usable Display Controller BAR.\n");
    /* Bochs VBE confirmed but PCI scan failed - try default QEMU address */
    /* Update: On real hardware, this is fatal if we assume VESA is active.
       Return 0 to force fallback to VGA unless we are absolutely sure. */
    return 0;
}

/* Initialize framebuffer - called once during boot */
uint32_t *gpu_setup_framebuffer(void)
{

    /* Read boot info from 0x9000 (set by vesa_reinit_mode or bootloader) */
    uint16_t info_width = vbe_info->width;
    uint16_t info_height = vbe_info->height;
    uint8_t info_bpp = vbe_info->bpp;
    uint32_t info_fb = vbe_info->framebuffer;

    /* Check if resolution values are sane (set by vesa_reinit_mode) */
    bool have_vesa = (info_width >= 640 && info_width <= 4096 &&
                      info_height >= 480 && info_height <= 4096 &&
                      info_bpp >= 24);

    /* If framebuffer address is missing OR looks like the hardcoded default (set by vesa.asm),
       try to detect the real LFB address via PCI to ensure it's correct */
    if (have_vesa && (info_fb == 0 || info_fb == 0xFD000000))
    {
        uint32_t detected_fb = detect_bochs_vbe_lfb();
        if (detected_fb != 0)
        {
            info_fb = detected_fb;
            vbe_info->framebuffer = info_fb;
        }
    }

    if (have_vesa && info_fb != 0)
    {
        vbe_active = true;
        vga_fallback = false;
        framebuffer = (uint32_t *)info_fb;
        screen_width = info_width;
        screen_height = info_height;
        screen_bpp = info_bpp;
        screen_pitch = vbe_info->pitch ? vbe_info->pitch : screen_width * (screen_bpp / 8);

        if (screen_pitch < screen_width * (screen_bpp / 8))
        {
            screen_pitch = screen_width * (screen_bpp / 8);
        }

        c_puts("[VBE] VESA mode active: ");
        char buf[16];
        int w = screen_width;
        buf[0] = '0' + (w / 1000) % 10;
        buf[1] = '0' + (w / 100) % 10;
        buf[2] = '0' + (w / 10) % 10;
        buf[3] = '0' + w % 10;
        buf[4] = 'x';
        buf[5] = 0;
        c_puts(buf);
        int h = screen_height;
        buf[0] = '0' + (h / 1000) % 10;
        buf[1] = '0' + (h / 100) % 10;
        buf[2] = '0' + (h / 10) % 10;
        buf[3] = '0' + h % 10;
        buf[4] = 0;
        c_puts(buf);
        c_puts("\n");

        c_puts("[VBE] Pitch: ");
        buf[0] = '0' + (screen_pitch / 1000) % 10;
        buf[1] = '0' + (screen_pitch / 100) % 10;
        buf[2] = '0' + (screen_pitch / 10) % 10;
        buf[3] = '0' + screen_pitch % 10;
        buf[4] = ' ';
        buf[5] = 'B';
        buf[6] = 'P';
        buf[7] = 'P';
        buf[8] = ':';
        buf[9] = ' ';
        buf[10] = '0' + screen_bpp / 10;
        buf[11] = '0' + screen_bpp % 10;
        buf[12] = 0;
        c_puts(buf);
        c_puts("\n");

        /* Allocate backbuffer for double buffering (eliminates flicker) */
        uint32_t fb_size = (uint32_t)screen_width * (uint32_t)screen_height * 4;
        backbuffer = (uint32_t *)kmalloc(fb_size);
        if (backbuffer)
        {
            double_buffered = true;
            c_puts("[VBE] Double buffering enabled\n");
        }
        else
        {
            backbuffer = framebuffer;
            double_buffered = false;
            c_puts("[VBE] Direct rendering (no memory for backbuffer)\n");
        }

        /* Clear both buffers to black */
        c_puts("[VBE] Clearing framebuffer...\n");
        int pitch_pixels = screen_pitch / 4;
        /* Clear backbuffer (packed, width-strided) */
        int bb_total = screen_width * screen_height;
        for (int i = 0; i < bb_total; i++)
        {
            backbuffer[i] = 0x00000000;
        }
        /* Clear framebuffer row-by-row using pitch stride */
        for (int y = 0; y < screen_height; y++)
        {
            uint32_t *row = framebuffer + y * pitch_pixels;
            for (int x = 0; x < screen_width; x++)
            {
                row[x] = 0x00000000;
            }
        }
        c_puts("[VBE] Framebuffer cleared to black\n");

        return backbuffer;
    }

    /* Fallback to VGA 320x200 */
    c_puts("[VGA] Falling back to Mode 13h (320x200)\n");
    vbe_active = false;
    vga_fallback = true;
    screen_width = 320;
    screen_height = 200;
    screen_bpp = 8;
    screen_pitch = 320;
    framebuffer = (uint32_t *)0xA0000;
    backbuffer = framebuffer;
    double_buffered = false;
    set_mode_13h();
    setup_palette();
    return framebuffer;
}

/* Get screen dimensions */
int gpu_get_width(void) { return screen_width; }
int gpu_get_height(void) { return screen_height; }

/* Convert 32-bit color to VGA palette index */
static uint8_t rgb_to_vga(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    /* Map to standard 16 colors */
    int brightness = (r + g + b) / 3;
    if (brightness < 32)
        return 0;
    if (r > g && r > b)
        return (brightness > 128) ? 12 : 4;
    if (g > r && g > b)
        return (brightness > 128) ? 10 : 2;
    if (b > r && b > g)
        return (brightness > 128) ? 9 : 1;
    if (brightness > 200)
        return 15;
    if (brightness > 128)
        return 7;
    return 8;
}

/* Clear entire screen */
void gpu_clear(uint32_t color)
{
    if (vga_fallback)
    {
        uint8_t vga_col = rgb_to_vga(color);
        uint8_t *vga = (uint8_t *)0xA0000;
        for (int i = 0; i < 320 * 200; i++)
            vga[i] = vga_col;
        return;
    }
    uint32_t *buf = backbuffer;
    int total = screen_width * screen_height;
    for (int i = 0; i < total; i++)
        buf[i] = color;
}

/* Draw single pixel */
void gpu_draw_pixel(int x, int y, uint32_t color)
{
    if (x < 0 || x >= screen_width || y < 0 || y >= screen_height)
        return;
    if (vga_fallback)
    {
        uint8_t *vga = (uint8_t *)0xA0000;
        vga[y * 320 + x] = rgb_to_vga(color);
        return;
    }
    backbuffer[y * screen_width + x] = color;
}

/* Fill rectangle */
void gpu_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    /* Clip */
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > screen_width)
        w = screen_width - x;
    if (y + h > screen_height)
        h = screen_height - y;
    if (w <= 0 || h <= 0)
        return;

    if (vga_fallback)
    {
        uint8_t vga_col = rgb_to_vga(color);
        uint8_t *vga = (uint8_t *)0xA0000;
        for (int dy = 0; dy < h; dy++)
        {
            int offset = (y + dy) * 320 + x;
            for (int dx = 0; dx < w; dx++)
            {
                vga[offset + dx] = vga_col;
            }
        }
        return;
    }

    uint32_t *buf = backbuffer;
    for (int dy = 0; dy < h; dy++)
    {
        int offset = (y + dy) * screen_width + x;
        for (int dx = 0; dx < w; dx++)
        {
            buf[offset + dx] = color;
        }
    }
}

/* Draw 8x8 character */
void gpu_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg)
{
    if (c > 127)
        c = '?';
    const uint8_t *glyph = font_8x8[c];

    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            uint32_t color = (glyph[row] & (0x80 >> col)) ? fg : bg;
            if (bg == 0 && color == 0)
                continue; /* Transparent background */
            gpu_draw_pixel(x + col, y + row, color);
        }
    }
}

/* Draw string */
void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg)
{
    int cx = x;
    while (*str)
    {
        if (*str == '\n')
        {
            cx = x;
            y += 10;
        }
        else
        {
            gpu_draw_char(cx, y, *str, fg, bg);
            cx += 8;
        }
        str++;
    }
}

/* Flush backbuffer to screen */
int gpu_flush(void)
{
    if (!vbe_active)
        return 0;
    if (backbuffer == framebuffer)
        return 0;

    if (screen_bpp == 32)
    {
        /* Pitch-aware blit for 32bpp */
        int pitch_pixels = screen_pitch / 4; /* pitch in 32-bit pixels */
        if (pitch_pixels == screen_width)
        {
            /* Fast path: pitch matches width, do one big copy */
            uint32_t *src = backbuffer;
            uint32_t *dst = framebuffer;
            uint32_t count = (uint32_t)screen_width * (uint32_t)screen_height;
            __asm__ volatile(
                "cld\n"
                "rep movsl\n"
                : "+S"(src), "+D"(dst), "+c"(count)
                :
                : "memory");
        }
        else
        {
            /* Slow path: copy row by row, respecting pitch */
            for (int y = 0; y < screen_height; y++)
            {
                uint32_t *src = backbuffer + y * screen_width;
                uint32_t *dst = framebuffer + y * pitch_pixels;
                uint32_t count = (uint32_t)screen_width;
                __asm__ volatile(
                    "cld\n"
                    "rep movsl\n"
                    : "+S"(src), "+D"(dst), "+c"(count)
                    :
                    : "memory");
            }
        }
    }
    else if (screen_bpp == 24)
    {
        /* 24bpp conversion: each pixel in backbuffer (uint32_t) to 3 bytes in framebuffer */
        uint8_t *dst_base = (uint8_t *)framebuffer;
        for (int y = 0; y < screen_height; y++)
        {
            uint32_t *src = backbuffer + y * screen_width;
            uint8_t *dst = dst_base + y * screen_pitch;
            for (int x = 0; x < screen_width; x++)
            {
                uint32_t color = src[x];
                *dst++ = (uint8_t)(color & 0xFF);         /* B */
                *dst++ = (uint8_t)((color >> 8) & 0xFF);  /* G */
                *dst++ = (uint8_t)((color >> 16) & 0xFF); /* R */
            }
        }
    }
    return 0;
}

/* Expose framebuffer pointers */
uint32_t *gpu_get_framebuffer(void) { return framebuffer; }
uint32_t *gpu_get_backbuffer(void) { return backbuffer; }
bool gpu_is_vesa(void) { return vbe_active; }
bool gpu_is_vga(void) { return vga_fallback; }

/* Stubs for backward compatibility with gui_apps.c */
int use_vga_fallback = 0;
uint32_t *gui_buffer = 0;

/* Set text mode (VGA mode 3) */
void set_text_mode(void)
{
    /* Disable VBE if active */
    if (vbe_active)
    {
        /* Use Bochs VBE to disable */
        __asm__ volatile(
            "mov $0x01CE, %%dx\n"
            "mov $4, %%ax\n"
            "outw %%ax, %%dx\n"
            "mov $0x01CF, %%dx\n"
            "mov $0, %%ax\n"
            "outw %%ax, %%dx\n"
            : : : "ax", "dx");
    }

    /* Program VGA registers for text mode 3 */
    outb(0x3C2, 0x67);

    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01);
    outb(0x3C4, 0x01);
    outb(0x3C5, 0x00);
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x03);
    outb(0x3C4, 0x03);
    outb(0x3C5, 0x00);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x02);
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03);

    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);

    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF};
    for (int i = 0; i < 25; i++)
    {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    outb(0x3CE, 0x05);
    outb(0x3CF, 0x10);
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x0E);

    inb(0x3DA);
    static const uint8_t pal[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
    for (int i = 0; i < 16; i++)
    {
        inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, pal[i]);
    }
    inb(0x3DA);
    outb(0x3C0, 0x10);
    outb(0x3C0, 0x0C);
    inb(0x3DA);
    outb(0x3C0, 0x11);
    outb(0x3C0, 0x00);
    inb(0x3DA);
    outb(0x3C0, 0x12);
    outb(0x3C0, 0x0F);
    inb(0x3DA);
    outb(0x3C0, 0x13);
    outb(0x3C0, 0x08);
    inb(0x3DA);
    outb(0x3C0, 0x14);
    outb(0x3C0, 0x00);
    inb(0x3DA);
    outb(0x3C0, 0x20);

    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = 0x0720;

    vbe_active = false;
}

/* Weak overrides for gpu_disable_scanout */
__attribute__((weak)) int gpu_disable_scanout(void)
{
    set_text_mode();
    return 0;
}