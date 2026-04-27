/*
 * boot_screen.c
 *
 * Clean boot screen for AurionOS
 * - Stable spinner animation
 * - Cleaner centered branding
 * - Safer pixel drawing
 * - Less glitchy visuals
 */

#include "boot_screen.h"
#include <stdint.h>
#include <stdbool.h>

/* External GPU functions */
extern uint32_t *gpu_setup_framebuffer(void);
extern int gpu_flush(void);
extern void gpu_clear(uint32_t color);
extern int gpu_get_width(void);
extern int gpu_get_height(void);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_fill_rect_blend(int x, int y, int w, int h, uint32_t c, uint8_t a);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern uint32_t get_ticks(void);
extern void io_wait(void);

/* Constants */

#define BOOT_BG_COLOR         0xFF080808
#define BOOT_ACCENT_COLOR     0xFF808080
#define BOOT_TEXT_COLOR       0xFFFFFFFF
#define BOOT_SUBTEXT_COLOR    0xFF98A2B3
#define BOOT_DIM_TEXT_COLOR   0xFF6B7280

#define FONT_W 8
#define FONT_H 16

/* Small helpers */

static int boot_strlen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void draw_pixel_safe(int x, int y, uint32_t color) {
    int sw = gpu_get_width();
    int sh = gpu_get_height();
    if (x < 0 || y < 0 || x >= sw || y >= sh) return;
    gpu_draw_pixel(x, y, color);
}

static void draw_text_scaled(int x, int y, const char *text, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    while (*text) {
        int glyph_idx = (uint8_t)(*text);
        if (glyph_idx > 127) glyph_idx = '?';
        
        /* Using internal 8x8 font bits */
        extern const uint8_t font_8x8[128][8];
        const uint8_t *glyph = font_8x8[glyph_idx];

        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (glyph[row] & (0x80 >> col)) {
                    gpu_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 8 * scale;
        text++;
    }
}

static void draw_text_centered(int cx, int y, const char *text, uint32_t color, int scale) {
    int len = boot_strlen(text);
    int x = cx - (len * 8 * scale) / 2;
    draw_text_scaled(x, y, text, color, scale);
}

static uint8_t color_r(uint32_t c) { return (uint8_t)((c >> 16) & 0xFF); }
static uint8_t color_g(uint32_t c) { return (uint8_t)((c >> 8) & 0xFF); }
static uint8_t color_b(uint32_t c) { return (uint8_t)(c & 0xFF); }

static uint32_t make_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) |
           ((uint32_t)r << 16) |
           ((uint32_t)g << 8)  |
           ((uint32_t)b);
}

static uint32_t color_scale(uint32_t c, uint8_t scale) {
    uint8_t r = (uint8_t)((color_r(c) * scale) / 255);
    uint8_t g = (uint8_t)((color_g(c) * scale) / 255);
    uint8_t b = (uint8_t)((color_b(c) * scale) / 255);
    return make_argb(0xFF, r, g, b);
}

/* Simple primitive drawing */

static void draw_circle_filled(int cx, int cy, int radius, uint32_t color) {
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= r2) {
                draw_pixel_safe(cx + dx, cy + dy, color);
            }
        }
    }
}

static void draw_glow_line(int x, int y, int w, uint32_t color) {
    gpu_fill_rect(x, y, w, 1, color_scale(color, 220));
    gpu_fill_rect_blend(x, y - 1, w, 1, color, 50);
    gpu_fill_rect_blend(x, y + 1, w, 1, color, 50);
}

/* Logo / branding */

/* External BMP functions */
extern int load_file_content(const char *filename, char *buffer, int max_len);

static void draw_logo_block(int cx, int y) {
    /*
     * Load and draw AurionOS.bmp logo
     */
    static uint8_t logo_data[32768];  /* Buffer for logo file */
    static int logo_loaded = 0;
    static int logo_w = 0, logo_h = 0;
    static uint32_t *logo_pixels = 0;

    if (!logo_loaded) {
        logo_loaded = 1;
        
        /* Try to load the logo BMP */
        int bytes = load_file_content("/icons/AurionOS-logo/AurionOS.bmp", 
                                      (char *)logo_data, sizeof(logo_data));
        
        if (bytes > 54) {
            /* Parse BMP header */
            if (logo_data[0] == 'B' && logo_data[1] == 'M') {
                logo_w = *(int32_t *)(logo_data + 18);
                logo_h = *(int32_t *)(logo_data + 22);
                uint16_t bpp = *(uint16_t *)(logo_data + 28);
                uint32_t pix_off = *(uint32_t *)(logo_data + 10);
                
                /* Only support 24-bit or 32-bit BMPs for simplicity */
                if ((bpp == 24 || bpp == 32) && logo_w > 0 && logo_h > 0 && 
                    logo_w <= 256 && logo_h <= 256) {
                    
                    /* Allocate pixel buffer */
                    extern void* kmalloc(uint32_t size);
                    logo_pixels = (uint32_t*)kmalloc(logo_w * logo_h * 4);
                    
                    if (logo_pixels) {
                        /* Decode BMP pixels (bottom-up BGR format) */
                        int row_stride = ((logo_w * (bpp / 8)) + 3) & ~3;
                        uint8_t *pix_data = logo_data + pix_off;
                        
                        for (int row = 0; row < logo_h; row++) {
                            int bmp_row = logo_h - 1 - row;  /* BMP is bottom-up */
                            uint8_t *row_ptr = pix_data + bmp_row * row_stride;
                            
                            for (int col = 0; col < logo_w; col++) {
                                uint8_t b = row_ptr[col * (bpp / 8) + 0];
                                uint8_t g = row_ptr[col * (bpp / 8) + 1];
                                uint8_t r = row_ptr[col * (bpp / 8) + 2];
                                uint8_t a = (bpp == 32) ? row_ptr[col * (bpp / 8) + 3] : 0xFF;
                                
                                logo_pixels[row * logo_w + col] = 
                                    ((uint32_t)a << 24) | ((uint32_t)r << 16) | 
                                    ((uint32_t)g << 8) | (uint32_t)b;
                            }
                        }
                    }
                }
            }
        }
    }
    
    /* Draw the logo if loaded, otherwise draw fallback bars */
    if (logo_pixels && logo_w > 0 && logo_h > 0) {
        /* Scale logo to fit nicely (max 64x64) */
        int target_size = 48;
        int draw_w = logo_w;
        int draw_h = logo_h;
        
        if (logo_w > target_size || logo_h > target_size) {
            float scale = (float)target_size / (logo_w > logo_h ? logo_w : logo_h);
            draw_w = (int)(logo_w * scale);
            draw_h = (int)(logo_h * scale);
        }
        
        int logo_x = cx - draw_w / 2;
        int logo_y = y - draw_h / 2 - 10;
        
        /* Draw scaled logo */
        for (int dy = 0; dy < draw_h; dy++) {
            for (int dx = 0; dx < draw_w; dx++) {
                int src_x = (dx * logo_w) / draw_w;
                int src_y = (dy * logo_h) / draw_h;
                uint32_t pixel = logo_pixels[src_y * logo_w + src_x];
                
                /* Skip fully transparent pixels */
                if ((pixel >> 24) > 0) {
                    draw_pixel_safe(logo_x + dx, logo_y + dy, pixel);
                }
            }
        }
    } else {
        /* Fallback: minimal geometric icon */
        int icon_x = cx - 84;
        int bar_w = 8;
        int bar_gap = 5;
        int bar_h1 = 24;
        int bar_h2 = 34;
        int bar_h3 = 18;

        /* glow */
        gpu_fill_rect_blend(icon_x - 2, y - 2, bar_w + 4, bar_h1 + 4, BOOT_ACCENT_COLOR, 35);
        gpu_fill_rect_blend(icon_x + bar_w + bar_gap - 2, y - 6, bar_w + 4, bar_h2 + 4, BOOT_ACCENT_COLOR, 45);
        gpu_fill_rect_blend(icon_x + (bar_w + bar_gap) * 2 - 2, y + 2 - 2, bar_w + 4, bar_h3 + 4, BOOT_ACCENT_COLOR, 28);

        gpu_fill_rect(icon_x, y, bar_w, bar_h1, BOOT_ACCENT_COLOR);
        gpu_fill_rect(icon_x + bar_w + bar_gap, y - 6, bar_w, bar_h2, 0xFF909090);
        gpu_fill_rect(icon_x + (bar_w + bar_gap) * 2, y + 2, bar_w, bar_h3, 0xFF707070);
    }
}

static void draw_boot_brand(int sw, int sh) {
    int cx = sw / 2;
    int title_y = sh / 2 - 54;

    draw_logo_block(cx, title_y - 6);

    /* shadow */
    draw_text_centered(cx + 2, title_y + 1, "AurionOS", 0xFF000000, 3);
    /* title */
    draw_text_centered(cx, title_y, "AurionOS", BOOT_TEXT_COLOR, 3);

    draw_text_centered(cx, title_y + 32, "Starting system services", BOOT_SUBTEXT_COLOR, 1);

    int line_w = 180;
    int line_x = cx - line_w / 2;
    draw_glow_line(line_x, title_y + 48, line_w, BOOT_ACCENT_COLOR);
}

/* Spinner */

/*
 * Avoid buggy trig by using a precomputed 12-point circle.
 * These values approximate a circle of radius 24.
 */
static const int spinner_offsets[12][2] = {
    {  0, -24 },
    { 12, -21 },
    { 21, -12 },
    { 24,   0 },
    { 21,  12 },
    { 12,  21 },
    {  0,  24 },
    {-12,  21 },
    {-21,  12 },
    {-24,   0 },
    {-21, -12 },
    {-12, -21 }
};

static void draw_spinner(int cx, int cy, uint32_t tick) {
    int active = (tick / 6) % 12;

    for (int i = 0; i < 12; i++) {
        int idx = (i + active) % 12;
        int x = cx + spinner_offsets[idx][0];
        int y = cy + spinner_offsets[idx][1];

        /*
         * Brighter head, dimmer tail.
         */
        int trail = 11 - i;
        uint8_t scale = (uint8_t)(55 + trail * 18);
        uint32_t c = color_scale(0xFFFFFFFF, scale);

        int radius = (i == 0) ? 4 : ((i < 4) ? 3 : 2);
        draw_circle_filled(x, y, radius, c);
    }
}

/* Progress dots */

static void draw_loading_dots(int cx, int y, uint32_t tick) {
    int phase = (tick / 20) % 4;
    char text[16];
    text[0] = 'L';
    text[1] = 'o';
    text[2] = 'a';
    text[3] = 'd';
    text[4] = 'i';
    text[5] = 'n';
    text[6] = 'g';
    text[7] = 0;

    draw_text_centered(cx, y, text, BOOT_DIM_TEXT_COLOR, 1);

    int base_x = cx + 32;
    for (int i = 0; i < 3; i++) {
        uint32_t c = (i < phase) ? BOOT_TEXT_COLOR : 0xFF3A4250;
        draw_circle_filled(base_x + i * 12, y + 7, 2, c);
    }
}

/* Background styling */

static void draw_boot_background(int sw, int sh) {
    /* Base dark color */
    gpu_clear(BOOT_BG_COLOR);

    /* Vertical subtle gradient / vignette */
    for (int y = 0; y < sh / 2; y++) {
        uint8_t a = (uint8_t)(25 - (y * 25) / (sh / 2));
        if (a > 0) gpu_fill_rect_blend(0, y, sw, 1, 0xFFFFFFFF, a);
    }

    /* Center radial glow effect - simulated with layered rects */
    int cx = sw / 2;
    int cy = sh / 2;
    for (int r = 0; r < 200; r += 20) {
        uint8_t a = (uint8_t)(15 - (r * 15) / 200);
        if (a > 0) {
            int wr = 400 - r * 2;
            int hr = 240 - r * 1.2;
            gpu_fill_rect_blend(cx - wr / 2, cy - hr / 2, wr, hr, 0xFF404040, a);
        }
    }

    /* Bottom subtle shadow */
    gpu_fill_rect_blend(0, sh - 120, sw, 120, 0xFF000000, 40);
}

/* Boot screen main */

void boot_screen_show(void) {
    int sw = gpu_get_width();
    int sh = gpu_get_height();

    uint32_t start_tick = get_ticks();
    uint32_t duration = 200; /* ~2 seconds if 100Hz */

    while ((get_ticks() - start_tick) < duration) {
        uint32_t now = get_ticks() - start_tick;

        draw_boot_background(sw, sh);
        draw_boot_brand(sw, sh);

        draw_spinner(sw / 2, sh / 2 + 88, now);
        draw_loading_dots(sw / 2, sh / 2 + 126, now);

        gpu_flush();

        for (int i = 0; i < 120; i++) {
            io_wait();
        }
    }
}