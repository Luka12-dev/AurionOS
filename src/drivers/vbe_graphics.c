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

/* Forward declarations */
void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Detect Bochs VBE and return LFB base address, or 0 if not present */
extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
extern void c_puts(const char *s); /* Use c_puts for debug logging */
extern void c_putc(char c);

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

    /* c_puts("[VBE] Scanning PCI for Display Controller...\n"); */
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
                /* c_puts("[VBE] Found Display Controller at PCI "); */
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

                /* if ((vendor_dev & 0xFFFF) == 0x1234 || (vendor_dev & 0xFFFF) == 0x1AF4)
                    c_puts("[VBE] Found Bochs/VirtIO Graphics!\n"); */

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
    /* c_puts("[VBE] PCI scan found no usable Display Controller BAR.\n"); */
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

    /* Check if resolution values are sane */
    bool have_vesa = (info_width >= 640 && info_width <= 4096 &&
                      info_height >= 480 && info_height <= 4096 &&
                      info_bpp >= 24);

    /* IMPROVED DETECTION: Trust the bootloader's VBE configuration if it exists.
       Many environments (VMware, v86, some QEMU configs) provide a valid LFB address
       in the VBE info structure. We should only perform a PCI scan if the address 
       is missing (0) or if we are explicitly on Bochs and want to verify. */
    if (have_vesa && info_fb != 0)
    {
        /* We have a valid-looking framebuffer address. 
           If it's the standard QEMU address, we can still try to detect Bochs-specific
           features, but we won't fall back to VGA if the scan fails. */
        if (info_fb == 0xFD000000) {
            c_puts("[VBE] Using default LFB address with hardware check...\n");
            uint32_t detected = detect_bochs_vbe_lfb();
            if (detected != 0) info_fb = detected;
        } else {
            /* c_puts("[VBE] Using bootloader VBE configuration\n"); */
        }
    }
    else if (have_vesa && info_fb == 0)
    {
        /* c_puts("[VBE] No LFB address - attempting hardware detection\n"); */
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
        /* c_puts("[VBE] Initializing mode: "); */
        // (Assuming c_puts works, but let's use serial_puts for certainty)
        // Actually c_puts in this kernel usually goes to the screen. 
        // Let's use it anyway.
        
        /* c_puts("Info: W="); */
        /* Simplified debug output since we don't have sprintf easily available here 
           without checking headers. But wait, we have a shell, so we must have some formatters. */
        
        screen_width = info_width;
        screen_height = info_height;
        screen_bpp = info_bpp;

        
        /* Pitch (bytes per scanline) detection.
           The bootloader stores pitch at 0x900A from the VBE mode info.
           Bochs VBE (v86, QEMU, VirtualBox) uses tight pitch = width * bpp/8,
           NOT power-of-2 aligned. Forcing alignment causes diagonal/sheared
           rendering - the classic "glitchy GUI" bug.
           
           Strategy: 
           1. If bootloader gave us a valid pitch, trust it
           2. If Bochs VBE is present, read the virtual width register directly
           3. Fall back to tight pitch (width * bytes_per_pixel) */
        uint32_t min_pitch = screen_width * (screen_bpp / 8);
        screen_pitch = vbe_info->pitch;
        if (vbe_info->lin_pitch != 0 && vbe_info->lin_pitch >= min_pitch) {
            screen_pitch = vbe_info->lin_pitch;
        }
        
        if ((uint32_t)screen_pitch < min_pitch || screen_pitch == 0) {
            /* Bootloader pitch missing or invalid - query Bochs VBE directly */
            if (bochs_vbe_present()) {
                /* Read the actual virtual width from Bochs VBE register.
                   Virtual width * bpp/8 = real pitch the hardware is using */
                outw(0x01CE, 0x06); /* VBE_DISPI_INDEX_VIRT_WIDTH */
                uint16_t virt_w = inw(0x01CF);
                if (virt_w >= (uint16_t)screen_width) {
                    screen_pitch = virt_w * (screen_bpp / 8);
                } else {
                    screen_pitch = min_pitch; /* tight pitch - the safe default */
                }
            } else {
                screen_pitch = min_pitch; /* no Bochs VBE, use tight pitch */
            }
        }
        /* Do NOT force-align pitch. Trust what the hardware reports. */

        /* Re-enable double buffering with heap check */
        uint32_t bb_size = (uint32_t)screen_width * (uint32_t)screen_height * 4;
        backbuffer = (uint32_t *)kmalloc(bb_size);
        
        if (backbuffer)
        {
            double_buffered = true;
            /* c_puts("[VBE] Double buffering active\n"); */

            /* Clear backbuffer */
            uint32_t bb_total = screen_width * screen_height;
            for (uint32_t i = 0; i < bb_total; i++) backbuffer[i] = 0;
        }
        else
        {
            backbuffer = framebuffer;
            double_buffered = false;
            /* c_puts("[VBE] Warning: Direct rendering enabled (no heap)\n"); */
        }


        /* Clear hardware framebuffer using pitch-aware byte-accurate loops */
        uint8_t *fb_ptr = (uint8_t *)framebuffer;
        for (uint32_t y = 0; y < (uint32_t)screen_height; y++)
        {
            uint8_t *row = fb_ptr + (y * screen_pitch);
            /* Clear row bytes */
            for (uint32_t x = 0; x < min_pitch; x++)
            {
                row[x] = 0;
            }
        }

        return backbuffer;
    }

    /* Fallback to VGA 320x200 */
    /* c_puts("[VGA] Falling back to Mode 13h\n"); */
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

    if (!double_buffered && screen_bpp == 32)
    {
        for (int y = 0; y < screen_height; y++)
        {
            uint32_t *row = (uint32_t *)((uint8_t *)framebuffer + y * screen_pitch);
            for (int x = 0; x < screen_width; x++) row[x] = color;
        }
    }
    else
    {
        uint32_t *buf = backbuffer;
        int total = screen_width * screen_height;
        for (int i = 0; i < total; i++)
            buf[i] = color;
    }
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
    if (!double_buffered && screen_bpp == 32)
    {
        uint32_t *target = (uint32_t *)((uint8_t *)framebuffer + y * screen_pitch + x * 4);
        *target = color;
    }
    else
    {
        backbuffer[y * screen_width + x] = color;
    }
}

/* Fill rectangle with alpha blending (0=transparent, 255=opaque) */
void gpu_fill_rect_blend(int x, int y, int w, int h, uint32_t color, uint8_t alpha)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return;
    if (x >= screen_width || y >= screen_height) return;
    if (x + w > screen_width) w = screen_width - x;
    if (y + h > screen_height) h = screen_height - y;
    if (!backbuffer && !vga_fallback) return;

    if (alpha == 255) { gpu_fill_rect(x, y, w, h, color); return; }
    if (alpha == 0) return;
    if (vga_fallback) { gpu_fill_rect(x, y, w, h, color); return; }

    uint32_t *buf = backbuffer;
    uint32_t r_src = (color >> 16) & 0xFF;
    uint32_t g_src = (color >> 8) & 0xFF;
    uint32_t b_src = color & 0xFF;
    uint32_t inv_alpha = 255 - alpha;

    for (int j = 0; j < h; j++) {
        uint32_t *row = &buf[(y + j) * screen_width + x];
        for (int i = 0; i < w; i++) {
            uint32_t dest = row[i];
            
            uint32_t r_dst = (dest >> 16) & 0xFF;
            uint32_t g_dst = (dest >> 8) & 0xFF;
            uint32_t b_dst = dest & 0xFF;

            uint32_t r = (r_src * alpha + r_dst * inv_alpha) >> 8;
            uint32_t g = (g_src * alpha + g_dst * inv_alpha) >> 8;
            uint32_t b = (b_src * alpha + b_dst * inv_alpha) >> 8;

            row[i] = (r << 16) | (g << 8) | b;
        }
    }
}

/* Perform a 3x3 box blur on a rectangle - Optimized C */
void gpu_blur_rect(int x, int y, int w, int h) {
    if (vga_fallback || !backbuffer) return;

    /* Clip */
    if (x < 1) { w += (x - 1); x = 1; }
    if (y < 1) { h += (y - 1); y = 1; }
    if (x + w >= screen_width - 1) w = screen_width - 1 - x;
    if (y + h >= screen_height - 1) h = screen_height - 1 - y;
    if (w <= 0 || h <= 0) return;

    uint8_t *base = (uint8_t *)backbuffer;
    int stride = screen_width * 4;

    if (!double_buffered && screen_bpp == 32) {
        base = (uint8_t *)framebuffer;
        stride = screen_pitch;
    }

    for (int j = 0; j < h; j++) {
        uint32_t *row = (uint32_t *)(base + (y + j) * stride + x * 4);
        for (int i = 0; i < w; i++) {
            uint32_t r = 0, g = 0, b = 0;
            
            /* Unrolled 3x3 loop using GPRs */
            for (int dy = -1; dy <= 1; dy++) {
                uint32_t *sample_row = (uint32_t *)(base + (y + j + dy) * stride + (x + i) * 4);
                
                uint32_t px1 = sample_row[-1];
                r += (px1 >> 16) & 0xFF;
                g += (px1 >> 8) & 0xFF;
                b += px1 & 0xFF;
                
                uint32_t px2 = sample_row[0];
                r += (px2 >> 16) & 0xFF;
                g += (px2 >> 8) & 0xFF;
                b += px2 & 0xFF;
                
                uint32_t px3 = sample_row[1];
                r += (px3 >> 16) & 0xFF;
                g += (px3 >> 8) & 0xFF;
                b += px3 & 0xFF;
            }
            
            row[i] = (0xFF << 24) | ((r / 9) << 16) | ((g / 9) << 8) | (b / 9);
        }
    }
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

    uint8_t *base = (uint8_t *)backbuffer;
    int stride = screen_width * 4;

    if (!double_buffered && screen_bpp == 32)
    {
        base = (uint8_t *)framebuffer;
        stride = screen_pitch;
    }

    for (int dy = 0; dy < h; dy++)
    {
        uint32_t *row = (uint32_t *)(base + (y + dy) * stride + x * 4);
        for (int dx = 0; dx < w; dx++)
        {
            row[dx] = color;
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

    /* Byte-accurate pitch check for fast path */
    uint32_t bytes_per_pixel = (screen_bpp / 8);
    if (screen_bpp == 32)
    {
        /* Fast path: pitch matches width perfectly, do one big copy */
        if ((uint32_t)screen_pitch == (uint32_t)screen_width * bytes_per_pixel)
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
            /* Slow path: copy row by row, respecting byte-accurate pitch */
            for (int y = 0; y < screen_height; y++)
            {
                uint32_t *src = backbuffer + y * screen_width;
                uint32_t *dst = (uint32_t *)((uint8_t *)framebuffer + y * screen_pitch);
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
/* Set text mode (VGA mode 3) */
void set_text_mode(void)
{
    /* Step 0: Disable VMware SVGA if present */
    extern void vmware_svga_disable(void);
    vmware_svga_disable();

    /* Step 1: Disable Bochs VBE if present */
    if (bochs_vbe_present())
    {
        outw(0x01CE, 4);  /* VBE_DISPI_INDEX_ENABLE */
        outw(0x01CF, 0);  /* VBE_DISPI_DISABLED */
    }
    
    /* Step 1.5: Force VGA mode 3 via BIOS-style register write */
    outb(0x3C2, 0x67);  /* Misc Output Register */

    /* Step 2: Standard VGA Mode 03h register programming
     * 
     * The key insight: after running in a VESA/VBE linear framebuffer mode,
     * the VGA subsystem's internal state is completely trashed. We need to
     * reprogram EVERY register group in the correct order:
     *   1. Misc Output Register
     *   2. Sequencer (reset, clocking, map mask, char map, memory mode)
     *   3. Unlock and program CRTC
     *   4. Graphics Controller
     *   5. Attribute Controller
     *   6. Reload the VGA font into plane 2
     *   7. Clear video memory at 0xB8000
     */

    /* Misc Output Register */
    /* Bit 0: I/O address select (1 = 0x3D4/0x3DA, color mode)
     * Bit 1: Enable RAM (1)
     * Bit 2-3: Clock select (01 = 28.322 MHz for 80-col)
     * Bit 5: Page bit for odd/even (0)
     * Bit 6: Horizontal sync polarity (1 = negative)
     * Bit 7: Vertical sync polarity (1 = negative) */
    outb(0x3C2, 0x67);

    /* Sequencer Registers */
    /* Reset sequencer first (synchronous reset) */
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); /* Assert sync reset */
    
    /* SR1: Clocking Mode - 8-dot character clock */
    outb(0x3C4, 0x01); outb(0x3C5, 0x00); /* 0x00 = 9-dot clock (standard VGA text) */
    
    /* SR2: Map Mask - enable planes 0 and 1 (text + attribute) */
    outb(0x3C4, 0x02); outb(0x3C5, 0x03);
    
    /* SR3: Character Map Select - both maps point to map 0 */
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    
    /* SR4: Memory Mode - Odd/Even addressing, NOT chain-4 */
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);
    
    /* Release reset */
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); /* Clear sync reset */

    /* CRTC Registers */
    /* Unlock CRTC (clear protect bit in CR11) */
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);

    /* Standard VGA Mode 03h (720x400 text, 80x25) CRTC values */
    static const uint8_t crtc_regs[25] = {
        0x5F, /* CR00: Horizontal Total */
        0x4F, /* CR01: Horizontal Display End */
        0x50, /* CR02: Start Horizontal Blanking */
        0x82, /* CR03: End Horizontal Blanking */
        0x55, /* CR04: Start Horizontal Retrace */
        0x81, /* CR05: End Horizontal Retrace */
        0xBF, /* CR06: Vertical Total */
        0x1F, /* CR07: Overflow */
        0x00, /* CR08: Preset Row Scan */
        0x4F, /* CR09: Maximum Scan Line (16-1=15 for 8x16, but 0x4F=double scan) */
        0x0D, /* CR0A: Cursor Start (scanline 13) */
        0x0E, /* CR0B: Cursor End (scanline 14) */
        0x00, /* CR0C: Start Address High */
        0x00, /* CR0D: Start Address Low */
        0x00, /* CR0E: Cursor Location High */
        0x00, /* CR0F: Cursor Location Low */
        0x9C, /* CR10: Vertical Retrace Start */
        0x8E, /* CR11: Vertical Retrace End (bit 7=0 means unlocked) */
        0x8F, /* CR12: Vertical Display End */
        0x28, /* CR13: Offset (logical width = 80 words) */
        0x1F, /* CR14: Underline Location */
        0x96, /* CR15: Start Vertical Blanking */
        0xB9, /* CR16: End Vertical Blanking */
        0xA3, /* CR17: CRTC Mode Control - byte mode, word addressing */
        0xFF, /* CR18: Line Compare */
    };
    for (int i = 0; i < 25; i++)
    {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }

    /* Graphics Controller Registers */
    /* GR0: Set/Reset - all zeros */
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    /* GR1: Enable Set/Reset - disabled */
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    /* GR2: Color Compare - all zeros */
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    /* GR3: Data Rotate - no rotation, no function */
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    /* GR4: Read Map Select - plane 0 */
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    /* GR5: Graphics Mode - Odd/Even mode for text, NOT interleaved shift */
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    /* GR6: Miscellaneous Graphics - text mode memory map at B8000-BFFFF
     *   Bit 0: 0 = text mode (alpha)
     *   Bit 1: 1 = chain odd/even
     *   Bits 2-3: 11 = B8000h-BFFFFh (32KB) */
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
    /* GR7: Color Don't Care - all planes */
    outb(0x3CE, 0x07); outb(0x3CF, 0x00);
    /* GR8: Bit Mask - all bits */
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);

    /* Attribute Controller Registers */
    /* Reading 0x3DA resets the AC flip-flop to "index" state */
    inb(0x3DA);

    /* AR00-AR0F: Standard CGA/EGA 16-color palette */
    static const uint8_t attr_palette[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
    };
    for (int i = 0; i < 16; i++)
    {
        inb(0x3DA);      /* Reset flip-flop */
        outb(0x3C0, i);  /* Index */
        outb(0x3C0, attr_palette[i]); /* Data */
    }

    /* AR10: Attribute Mode Control
     *   Bit 0: 0 = text mode (not graphics)
     *   Bit 1: 0 = mono emulation off
     *   Bit 2: 1 = line graphics enable (for box-drawing chars)
     *   Bit 3: 1 = blink enable */
    inb(0x3DA); outb(0x3C0, 0x10); outb(0x3C0, 0x0C);

    /* AR11: Overscan (border) Color */
    inb(0x3DA); outb(0x3C0, 0x11); outb(0x3C0, 0x00);

    /* AR12: Color Plane Enable - all 4 planes */
    inb(0x3DA); outb(0x3C0, 0x12); outb(0x3C0, 0x0F);

    /* AR13: Horizontal Pixel Panning - 8 for 9-dot mode */
    inb(0x3DA); outb(0x3C0, 0x13); outb(0x3C0, 0x08);

    /* AR14: Color Select */
    inb(0x3DA); outb(0x3C0, 0x14); outb(0x3C0, 0x00);

    /* Enable video output (set bit 5 of AC index register) */
    inb(0x3DA);
    outb(0x3C0, 0x20);

    /* Step 3: Reload VGA 8x16 Font into Plane 2 */
    /* This is CRITICAL. After VBE mode, the font data in plane 2 is
     * completely destroyed. Without reloading it, text mode will show
     * garbage characters (vertical lines, random patterns).
     *
     * We use the standard VGA font stored in the Video BIOS ROM at
     * the VGA's internal character generator. Since we can't call
     * INT 10h from protected mode, we manually load a basic 8x16 font
     * by accessing plane 2 directly. */

    /* Put sequencer into font-loading mode:
     * - Write to plane 2 only (map mask = 0x04)
     * - Sequential addressing (memory mode = 0x06) */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04); /* Map Mask: plane 2 only */
    outb(0x3C4, 0x04); outb(0x3C5, 0x06); /* Memory Mode: sequential, no odd/even */

    /* Graphics controller: sequential access to plane 2 */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02); /* Read Map Select: plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00); /* Graphics Mode: write mode 0, read mode 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x00); /* Misc: map at A0000, no odd/even */

    /* 8x16 VGA font - we need to write 256 characters × 32 bytes each
     * (even though each glyph is only 16 rows, VGA allocates 32 bytes per char).
     * We'll use our built-in 8x8 font and double each row for 8x16 appearance,
     * then pad the remaining 16 bytes with zeros. */
    volatile uint8_t *font_dest = (volatile uint8_t *)0xA0000;

    /* Zero entire font area first (256 chars × 32 bytes = 8192 bytes) */
    for (int i = 0; i < 8192; i++)
        font_dest[i] = 0;

    /* Load each character from our 8x8 font, doubled to 8x16 */
    for (int ch = 0; ch < 128; ch++)
    {
        volatile uint8_t *dest = font_dest + ch * 32;
        const uint8_t *src = font_8x8[ch];
        for (int row = 0; row < 8; row++)
        {
            dest[row * 2]     = src[row]; /* Even scanline */
            dest[row * 2 + 1] = src[row]; /* Odd scanline (double) */
        }
        /* Rows 16-31 are already zeroed */
    }

    /* Restore sequencer and GC back to text mode settings */
    outb(0x3C4, 0x02); outb(0x3C5, 0x03); /* Map Mask: planes 0,1 */
    outb(0x3C4, 0x04); outb(0x3C5, 0x02); /* Memory Mode: odd/even */

    outb(0x3CE, 0x04); outb(0x3CF, 0x00); /* Read Map Select: plane 0 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x10); /* Graphics Mode: odd/even */
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E); /* Misc: B8000, odd/even */

    /* Step 4: Set up the DAC color palette for standard 16 colors */
    /* The DAC (Digital-to-Analog Converter) maps the 6-bit palette indices
     * from the Attribute Controller to actual RGB values.
     * After VBE mode the DAC contains whatever palette was used for graphics. */
    static const uint8_t dac_colors[16][3] = {
        {0x00, 0x00, 0x00}, /* 0: Black */
        {0x00, 0x00, 0x2A}, /* 1: Blue */
        {0x00, 0x2A, 0x00}, /* 2: Green */
        {0x00, 0x2A, 0x2A}, /* 3: Cyan */
        {0x2A, 0x00, 0x00}, /* 4: Red */
        {0x2A, 0x00, 0x2A}, /* 5: Magenta */
        {0x2A, 0x15, 0x00}, /* 6: Brown */
        {0x2A, 0x2A, 0x2A}, /* 7: Light Gray */
        {0x15, 0x15, 0x15}, /* 8: Dark Gray */
        {0x15, 0x15, 0x3F}, /* 9: Light Blue */
        {0x15, 0x3F, 0x15}, /* 10: Light Green */
        {0x15, 0x3F, 0x3F}, /* 11: Light Cyan */
        {0x3F, 0x15, 0x15}, /* 12: Light Red */
        {0x3F, 0x15, 0x3F}, /* 13: Light Magenta */
        {0x3F, 0x3F, 0x15}, /* 14: Yellow */
        {0x3F, 0x3F, 0x3F}, /* 15: White */
    };

    /* The Attribute Controller palette entries map to DAC indices.
     * Entries 0-5 map directly (0-5), entry 6 maps to 0x14 (20),
     * entry 7 maps to 7, then entries 8-15 map to 0x38-0x3F (56-63).
     * We need to program those specific DAC entries. */
    
    /* Program DAC entries 0-7 */
    outb(0x3C8, 0x00); /* Start at DAC index 0 */
    for (int i = 0; i < 8; i++) {
        outb(0x3C9, dac_colors[i][0]);
        outb(0x3C9, dac_colors[i][1]);
        outb(0x3C9, dac_colors[i][2]);
    }

    /* Program DAC entry 0x14 (20) for brown (AR06 maps to 0x14) */
    outb(0x3C8, 0x14);
    outb(0x3C9, dac_colors[6][0]);
    outb(0x3C9, dac_colors[6][1]);
    outb(0x3C9, dac_colors[6][2]);

    /* Program DAC entries 0x38-0x3F (56-63) for bright colors */
    outb(0x3C8, 0x38);
    for (int i = 8; i < 16; i++) {
        outb(0x3C9, dac_colors[i][0]);
        outb(0x3C9, dac_colors[i][1]);
        outb(0x3C9, dac_colors[i][2]);
    }

    /* Step 5: Clear the text mode video memory */
    volatile uint16_t *vga_text = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++)
        vga_text[i] = 0x0720; /* Space character, light gray on black */

    /* Step 6: Set cursor to top-left and make it visible */
    /* Set cursor shape (start scanline 13, end scanline 15 for underline cursor) */
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0D); /* Cursor start scanline */
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F); /* Cursor end scanline */
    
    /* Set cursor position to 0,0 */
    outb(0x3D4, 0x0E); outb(0x3D5, 0x00); /* Cursor high byte */
    outb(0x3D4, 0x0F); outb(0x3D5, 0x00); /* Cursor low byte */

    /* Update driver state */
    vbe_active = false;
    vga_fallback = false;
}