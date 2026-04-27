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

/* VMware SVGA externs */
extern bool vmware_svga_available(void);
extern uint32_t vmware_svga_get_pitch(void);
extern void vmware_svga_update(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

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
    uint16_t lin_pitch;   /* Offset 12: LinBytesPerScanLine */
    uint16_t mode_number; /* Offset 14: VBE mode number */
} __attribute__((packed)) VbeBootInfo;

static VbeBootInfo *vbe_info = (VbeBootInfo *)VBE_INFO_ADDR;

/* Driver state */
static bool vbe_active = false;
static uint32_t *framebuffer = 0;
static uint32_t *backbuffer = 0;
static int screen_width = 320;
static int screen_height = 200;
static int screen_bpp = 8;
static int screen_pitch = 320; /* bytes per scanline in the HARDWARE framebuffer */
static bool double_buffered = false;

/* VGA state for fallback */
static bool vga_fallback = false;

/* 8x8 font bitmap */
const uint8_t font_8x8[128][8] = {
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

/* Forward declarations */
void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Detect Bochs VBE and return LFB base address, or 0 if not present */
extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/* Check if Bochs VBE hardware is actually present and responding */
bool bochs_vbe_present(void)
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

    /* Check if resolution values are sane */
    bool have_vesa = (info_width >= 640 && info_width <= 4096 &&
                      info_height >= 480 && info_height <= 4096 &&
                      info_bpp >= 24);

    if (have_vesa && info_fb != 0)
    {
        if (info_fb == 0xFD000000)
        {
            c_puts("[VBE] Using default LFB address with hardware check...\n");
            uint32_t detected = detect_bochs_vbe_lfb();
            if (detected != 0)
                info_fb = detected;
        }
    }
    else if (have_vesa && info_fb == 0)
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

        /* ----------------------------------------------------------------
         * PITCH DETECTION — critical for correct flush behaviour.
         *
         * The hardware pitch (bytes per scanline) governs how gpu_flush()
         * walks the hardware framebuffer.  The RAM backbuffer always uses
         * a tight stride of (screen_width * 4) bytes so every drawing
         * primitive can use simple array indexing.
         *
         * Priority order:
         *   1. lin_pitch from bootloader VBE info  (most reliable)
         *   2. pitch from bootloader VBE info
         *   3. Bochs VBE virtual-width register
         *   4. Tight pitch (width * bytes_per_pixel) — safe default
         * ---------------------------------------------------------------- */
        uint32_t min_pitch = (uint32_t)screen_width * (screen_bpp / 8);

        /* Start with zero so we can fall through each probe cleanly */
        screen_pitch = 0;

        /* Probe 0: VMware SVGA (highest priority if active) */
        if (vmware_svga_available())
        {
            uint32_t vm_pitch = vmware_svga_get_pitch();
            if (vm_pitch >= min_pitch)
            {
                screen_pitch = vm_pitch;
                c_puts("[VBE] Pitch from VMware SVGA\n");
            }
        }

        /* Probe 1: lin_pitch (linear mode bytes-per-scanline)
         * On QEMU/Bochs this field is sometimes unreliable; prefer tight pitch
         * unless VMware SVGA is active.
         */
        if (screen_pitch == 0 &&
            vmware_svga_available() &&
            vbe_info->lin_pitch != 0 &&
            (uint32_t)vbe_info->lin_pitch >= min_pitch)
        {
            screen_pitch = vbe_info->lin_pitch;
            c_puts("[VBE] Pitch from lin_pitch\n");
        }

        /* Probe 2: regular pitch field */
        if (screen_pitch == 0 &&
            vmware_svga_available() &&
            vbe_info->pitch != 0 &&
            (uint32_t)vbe_info->pitch >= min_pitch)
        {
            screen_pitch = vbe_info->pitch;
            c_puts("[VBE] Pitch from pitch field\n");
        }

        /* Probe 3: Bochs VBE virtual-width register (QEMU/Bochs path) */
        if (screen_pitch == 0 && bochs_vbe_present())
        {
            outw(0x01CE, 0x06); /* VBE_DISPI_INDEX_VIRT_WIDTH */
            uint16_t virt_w = inw(0x01CF);
            if (virt_w >= (uint16_t)screen_width)
            {
                screen_pitch = (uint32_t)virt_w * (screen_bpp / 8);
                c_puts("[VBE] Pitch from Bochs virt-width\n");
            }
        }

        /* Probe 4: tight pitch — safest universal default */
        if (screen_pitch == 0)
        {
            /*
             * Align tight pitch to 64 bytes. This is the 'gold standard' for
             * modern GPU DMA and emulators (QEMU, VirtualBox).
             * 1024x768x32 = 4096 (already 64-byte aligned).
             * But 800x600x32 = 3200 (needs alignment to 3264 in some environments).
             */
            screen_pitch = min_pitch;
            c_puts("[VBE] Pitch: tight fallback\n");
        }

        /* Sanity-clamp: pitch must never be less than one full row */
        if ((uint32_t)screen_pitch < min_pitch)
            screen_pitch = min_pitch;

        /* ----------------------------------------------------------------
         * BACKBUFFER ALLOCATION
         *
         * The backbuffer is a plain RAM buffer addressed with tight stride
         * (screen_width * 4).  All drawing functions write here.
         * gpu_flush() translates to hardware pitch when copying to the LFB.
         * ---------------------------------------------------------------- */
        /* Free old backbuffer if it was dynamically allocated */
        if (double_buffered && backbuffer && backbuffer != framebuffer)
            kfree(backbuffer);

        uint32_t bb_size = (uint32_t)screen_width * (uint32_t)screen_height * 4;
        backbuffer = (uint32_t *)kmalloc(bb_size);

        if (backbuffer)
        {
            double_buffered = true;
            c_puts("[VBE] Double buffering active\n");

            /* Clear backbuffer */
            uint32_t bb_total = (uint32_t)screen_width * (uint32_t)screen_height;
            for (uint32_t i = 0; i < bb_total; i++)
                backbuffer[i] = 0;
        }
        else
        {
            /* No heap yet — render directly to LFB (no double buffering).
             * In this mode every drawing primitive must use screen_pitch
             * itself, which they currently don't.  Flag it so gpu_flush
             * becomes a no-op and the LFB == backbuffer path is used. */
            backbuffer = framebuffer;
            double_buffered = false;
            c_puts("[VBE] Warning: Direct rendering (no heap)\n");
        }

        /* Clear hardware framebuffer row-by-row using real pitch */
        {
            uint8_t *fb_ptr = (uint8_t *)framebuffer;
            for (int y = 0; y < screen_height; y++)
            {
                uint8_t *row = fb_ptr + (uint32_t)y * (uint32_t)screen_pitch;
                for (uint32_t x = 0; x < min_pitch; x++)
                    row[x] = 0;
            }
        }

        return backbuffer;
    }

    /* Fallback to VGA 320x200 */
    c_puts("[VGA] Falling back to Mode 13h\n");
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

extern bool vesa_set_mode(uint16_t width, uint16_t height, uint8_t bpp);
extern bool vmware_svga_available(void);
extern bool vmware_svga_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
extern void vesa_reinit_mode(void);

bool gpu_set_mode(uint16_t width, uint16_t height, uint8_t bpp)
{
    if (vmware_svga_available())
    {
        if (vmware_svga_set_mode(width, height, bpp))
        {
            screen_width = width;
            screen_height = height;
            screen_bpp = bpp;

            extern void io_wait(void);
            for (int i = 0; i < 100; i++)
                io_wait();

            framebuffer = (uint32_t *)gpu_setup_framebuffer();
            if (!framebuffer)
                return false;

            gpu_clear(0xFF000000);
            return true;
        }
        return false;
    }

    if (vesa_set_mode(width, height, bpp))
    {
        screen_width = width;
        screen_height = height;
        screen_bpp = bpp;

        extern void io_wait(void);
        for (int i = 0; i < 100; i++)
            io_wait();

        framebuffer = (uint32_t *)gpu_setup_framebuffer();
        if (!framebuffer)
            return false;

        gpu_clear(0xFF000000);
        return true;
    }

    return false;
}

/* Convert 32-bit color to VGA palette index */
static uint8_t rgb_to_vga(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
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

    /* Always clear the RAM backbuffer with tight stride */
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

    /* Backbuffer always uses tight stride: width * 4 */
    backbuffer[y * screen_width + x] = color;
}

/* Fill rectangle with alpha blending (0=transparent, 255=opaque) */
void gpu_fill_rect_blend(int x, int y, int w, int h,
                         uint32_t color, uint8_t alpha)
{
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
    if (w <= 0 || h <= 0)
        return;
    if (x >= screen_width || y >= screen_height)
        return;
    if (x + w > screen_width)
        w = screen_width - x;
    if (y + h > screen_height)
        h = screen_height - y;

    if (alpha == 255)
    {
        gpu_fill_rect(x, y, w, h, color);
        return;
    }
    if (alpha == 0)
        return;
    if (vga_fallback)
    {
        gpu_fill_rect(x, y, w, h, color);
        return;
    }

    uint32_t r_src = (color >> 16) & 0xFF;
    uint32_t g_src = (color >> 8) & 0xFF;
    uint32_t b_src = color & 0xFF;
    uint32_t inv = 255 - alpha;

    /* Backbuffer tight stride */
    for (int j = 0; j < h; j++)
    {
        uint32_t *row = backbuffer + (y + j) * screen_width + x;
        for (int i = 0; i < w; i++)
        {
            uint32_t d = row[i];
            uint32_t r = (r_src * alpha + ((d >> 16) & 0xFF) * inv) >> 8;
            uint32_t g = (g_src * alpha + ((d >> 8) & 0xFF) * inv) >> 8;
            uint32_t b = (b_src * alpha + (d & 0xFF) * inv) >> 8;
            row[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}

/* Perform a 3x3 box blur on a rectangle */
void gpu_blur_rect(int x, int y, int w, int h)
{
    if (vga_fallback || !backbuffer)
        return;

    /* Clip to safe interior (need 1-pixel border for 3×3 kernel) */
    if (x < 1)
    {
        w += (x - 1);
        x = 1;
    }
    if (y < 1)
    {
        h += (y - 1);
        y = 1;
    }
    if (x + w >= screen_width - 1)
        w = screen_width - 1 - x;
    if (y + h >= screen_height - 1)
        h = screen_height - 1 - y;
    if (w <= 0 || h <= 0)
        return;

    /* Backbuffer always uses tight stride (screen_width * 4) */
    int stride_px = screen_width; /* stride in uint32_t units */

    for (int j = 0; j < h; j++)
    {
        int cy = y + j;
        uint32_t *row = backbuffer + cy * stride_px + x;

        for (int i = 0; i < w; i++)
        {
            int cx = x + i;
            uint32_t r = 0, g = 0, b = 0;

            for (int dy = -1; dy <= 1; dy++)
            {
                uint32_t *sr = backbuffer + (cy + dy) * stride_px;
                for (int dx = -1; dx <= 1; dx++)
                {
                    uint32_t px = sr[cx + dx];
                    r += (px >> 16) & 0xFF;
                    g += (px >> 8) & 0xFF;
                    b += px & 0xFF;
                }
            }

            row[i] = 0xFF000000 |
                     ((r / 9) << 16) |
                     ((g / 9) << 8) |
                     (b / 9);
        }
    }
}

/* Fill rectangle — always writes to RAM backbuffer with tight stride */
void gpu_fill_rect(int x, int y, int w, int h, uint32_t color)
{
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
            int off = (y + dy) * 320 + x;
            for (int dx = 0; dx < w; dx++)
                vga[off + dx] = vga_col;
        }
        return;
    }

    /* Tight stride in RAM backbuffer */
    for (int dy = 0; dy < h; dy++)
    {
        uint32_t *row = backbuffer + (y + dy) * screen_width + x;
        for (int dx = 0; dx < w; dx++)
            row[dx] = color;
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
                continue;
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

/* =========================================================================
 * gpu_flush — copy RAM backbuffer → hardware LFB
 *
 * KEY FIX: The backbuffer uses tight stride (screen_width * 4 bytes/row).
 * The hardware LFB uses screen_pitch bytes/row (may be larger).
 * We MUST use screen_pitch when addressing the LFB, otherwise every row
 * is written at the wrong offset, producing the horizontal-band corruption
 * visible in the screenshot.
 *
 * This function is the ONLY place that touches screen_pitch.
 * All other drawing primitives use the tight backbuffer stride exclusively.
 * ========================================================================= */
int gpu_flush(void)
{
    if (!vbe_active)
        return 0;
    if (!double_buffered)
        return 0; /* backbuffer == framebuffer */
    if (backbuffer == framebuffer)
        return 0;

    if (screen_bpp == 32)
    {
        /* Fast 32-bpp path: one uint32_t per pixel.
         *
         *   src row: backbuffer + y * screen_width          (tight)
         *   dst row: (uint8_t*)framebuffer + y * screen_pitch (hardware)
         *
         * The two strides are intentionally different.  Never mix them.
         * NOTE: It is crucial to keep the pitch logic separate for correct functionality.
         */
        for (int y = 0; y < screen_height; y++)
        {
            uint32_t *src = backbuffer + (uint32_t)y * (uint32_t)screen_width;
            uint32_t *dst = (uint32_t *)((uint8_t *)framebuffer +
                                         (uint32_t)y * (uint32_t)screen_pitch);
            /* Use __builtin_memcpy for potentially faster word-aligned copy if available,
               but a simple loop is safest for kernel code. */
            for (int x = 0; x < screen_width; x++)
                dst[x] = src[x];
        }
    }
    else if (screen_bpp == 24)
    {
        /* 24-bpp: pack each 32-bit backbuffer pixel into 3 bytes in the LFB */
        uint8_t *dst_base = (uint8_t *)framebuffer;
        for (int y = 0; y < screen_height; y++)
        {
            uint32_t *src = backbuffer + (uint32_t)y * (uint32_t)screen_width;
            uint8_t *dst = dst_base + (uint32_t)y * (uint32_t)screen_pitch;
            for (int x = 0; x < screen_width; x++)
            {
                uint32_t c = src[x];
                *dst++ = (uint8_t)(c & 0xFF);         /* B */
                *dst++ = (uint8_t)((c >> 8) & 0xFF);  /* G */
                *dst++ = (uint8_t)((c >> 16) & 0xFF); /* R */
            }
        }
    }

    /* CRITICAL: On VMware, we MUST notify the host of changes */
    if (vmware_svga_available())
        vmware_svga_update(0, 0, screen_width, screen_height);

    return 0;
}

/* Expose framebuffer pointers */
uint32_t *gpu_get_framebuffer(void) { return framebuffer; }
uint32_t *gpu_get_backbuffer(void) { return backbuffer; }
bool gpu_is_vesa(void) { return vbe_active; }
bool gpu_is_vga(void) { return vga_fallback; }

/* Stubs for backward compatibility */
int use_vga_fallback = 0;
uint32_t *gui_buffer = 0;

/* Set text mode (VGA mode 3) */
void set_text_mode(void)
{
    /* Step 0: Disable VMware SVGA if present */
    extern void vmware_svga_disable(void);
    vmware_svga_disable();

    /* Step 1: Disable Bochs VBE if present */
    if (bochs_vbe_present())
    {
        outw(0x01CE, 4); /* VBE_DISPI_INDEX_ENABLE */
        outw(0x01CF, 0); /* VBE_DISPI_DISABLED */
    }

    /* Step 1.5: Force VGA mode via misc output */
    outb(0x3C2, 0x67);

    /* Sequencer Registers */
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01); /* Assert sync reset */
    outb(0x3C4, 0x01);
    outb(0x3C5, 0x00);
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x03);
    outb(0x3C4, 0x03);
    outb(0x3C5, 0x00);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x02);
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03); /* Release reset */

    /* CRTC: unlock then program */
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);

    static const uint8_t crtc_regs[25] = {
        0x5F,
        0x4F,
        0x50,
        0x82,
        0x55,
        0x81,
        0xBF,
        0x1F,
        0x00,
        0x4F,
        0x0D,
        0x0E,
        0x00,
        0x00,
        0x00,
        0x00,
        0x9C,
        0x8E,
        0x8F,
        0x28,
        0x1F,
        0x96,
        0xB9,
        0xA3,
        0xFF,
    };
    for (int i = 0; i < 25; i++)
    {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }

    /* Graphics Controller */
    outb(0x3CE, 0x00);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x01);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x02);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x03);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x10);
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x0E);
    outb(0x3CE, 0x07);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x08);
    outb(0x3CF, 0xFF);

    /* Attribute Controller */
    inb(0x3DA);
    static const uint8_t attr_palette[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
    for (int i = 0; i < 16; i++)
    {
        inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, attr_palette[i]);
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

    /* Reload VGA font into plane 2 */
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x04);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x06);
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x02);
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x00);

    volatile uint8_t *font_dest = (volatile uint8_t *)0xA0000;
    for (int i = 0; i < 8192; i++)
        font_dest[i] = 0;
    for (int ch = 0; ch < 128; ch++)
    {
        volatile uint8_t *dest = font_dest + ch * 32;
        const uint8_t *src = font_8x8[ch];
        for (int row = 0; row < 8; row++)
        {
            dest[row * 2] = src[row];
            dest[row * 2 + 1] = src[row];
        }
    }

    outb(0x3C4, 0x02);
    outb(0x3C5, 0x03);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x02);
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x10);
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x0E);

    /* DAC palette for 16 standard colors */
    static const uint8_t dac_colors[16][3] = {
        {0x00, 0x00, 0x00},
        {0x00, 0x00, 0x2A},
        {0x00, 0x2A, 0x00},
        {0x00, 0x2A, 0x2A},
        {0x2A, 0x00, 0x00},
        {0x2A, 0x00, 0x2A},
        {0x2A, 0x15, 0x00},
        {0x2A, 0x2A, 0x2A},
        {0x15, 0x15, 0x15},
        {0x15, 0x15, 0x3F},
        {0x15, 0x3F, 0x15},
        {0x15, 0x3F, 0x3F},
        {0x3F, 0x15, 0x15},
        {0x3F, 0x15, 0x3F},
        {0x3F, 0x3F, 0x15},
        {0x3F, 0x3F, 0x3F},
    };
    outb(0x3C8, 0x00);
    for (int i = 0; i < 8; i++)
    {
        outb(0x3C9, dac_colors[i][0]);
        outb(0x3C9, dac_colors[i][1]);
        outb(0x3C9, dac_colors[i][2]);
    }
    outb(0x3C8, 0x14);
    outb(0x3C9, dac_colors[6][0]);
    outb(0x3C9, dac_colors[6][1]);
    outb(0x3C9, dac_colors[6][2]);
    outb(0x3C8, 0x38);
    for (int i = 8; i < 16; i++)
    {
        outb(0x3C9, dac_colors[i][0]);
        outb(0x3C9, dac_colors[i][1]);
        outb(0x3C9, dac_colors[i][2]);
    }

    /* Clear text video memory */
    volatile uint16_t *vga_text = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++)
        vga_text[i] = 0x0720;

    /* Position cursor */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0D);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
    outb(0x3D4, 0x0E);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x0F);
    outb(0x3D5, 0x00);

    vbe_active = false;
    vga_fallback = false;
}