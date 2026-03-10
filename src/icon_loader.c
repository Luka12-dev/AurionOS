/*
 * icon_loader.c - Copy pre-decoded 48x48 BGRA raw icon data into draw buffers.
 *
 * Icons are pre-decoded at build time by Python (icons/*.raw).
 * No PNG decoder, no allocator, no runtime risk - just a memcpy at boot.
 * Each icon is exactly 48*48*4 = 9216 bytes of BGRA pixels.
*/

#include "icon_loader.h"
#include <stdint.h>

/* GPU pixel draw function provided by vbe_graphics.c */
extern void gpu_draw_pixel(int x, int y, uint32_t color);

#define ICON_SIZE   48
#define ICON_PIXELS (ICON_SIZE * ICON_SIZE)  /* 2304 pixels */
#define ICON_BYTES  (ICON_PIXELS * 4)        /* 9216 bytes */

/* Static pixel buffers - one per icon, 48x48 ARGB */
static unsigned int buf_terminal  [ICON_PIXELS];
static unsigned int buf_notepad   [ICON_PIXELS];
static unsigned int buf_calculator[ICON_PIXELS];
static unsigned int buf_files     [ICON_PIXELS];
static unsigned int buf_clock     [ICON_PIXELS];
static unsigned int buf_paint     [ICON_PIXELS];
static unsigned int buf_sysinfo   [ICON_PIXELS];
static unsigned int buf_settings  [ICON_PIXELS];
static unsigned int buf_folder    [ICON_PIXELS];
static unsigned int buf_file      [ICON_PIXELS];
static unsigned int buf_snake     [ICON_PIXELS];

/* Raw icon data embedded via incbin in icon_data.asm - pre-decoded 48x48 BGRA */
extern const unsigned char icon_raw_terminal[];
extern const unsigned char icon_raw_notepad[];
extern const unsigned char icon_raw_calculator[];
extern const unsigned char icon_raw_files[];
extern const unsigned char icon_raw_clock[];
extern const unsigned char icon_raw_paint[];
extern const unsigned char icon_raw_sysinfo[];
extern const unsigned char icon_raw_settings[];
extern const unsigned char icon_raw_folder[];
extern const unsigned char icon_raw_file[];
extern const unsigned char icon_raw_snake[];

/* Copy a pre-decoded 48x48 BGRA raw buffer into a 48x48 ARGB pixel buffer.
 * The .raw files are exactly ICON_BYTES (9216 bytes) - no scaling needed. */
static void copy_icon(const unsigned char *src, unsigned int *dst) {
    for (int i = 0; i < ICON_PIXELS; i++) {
        unsigned char bv = src[i * 4 + 0];
        unsigned char gv = src[i * 4 + 1];
        unsigned char rv = src[i * 4 + 2];
        unsigned char av = src[i * 4 + 3];
        dst[i] = ((unsigned int)av << 24) |
                 ((unsigned int)rv << 16) |
                 ((unsigned int)gv <<  8) |
                  (unsigned int)bv;
    }
}

/* Call once at boot - just copies static data, cannot crash */
void icons_load_all(void) {
    copy_icon(icon_raw_terminal,   buf_terminal);
    copy_icon(icon_raw_notepad,    buf_notepad);
    copy_icon(icon_raw_calculator, buf_calculator);
    copy_icon(icon_raw_files,      buf_files);
    copy_icon(icon_raw_clock,      buf_clock);
    copy_icon(icon_raw_paint,      buf_paint);
    copy_icon(icon_raw_sysinfo,    buf_sysinfo);
    copy_icon(icon_raw_settings,   buf_settings);
    copy_icon(icon_raw_folder,     buf_folder);
    copy_icon(icon_raw_file,       buf_file);
    copy_icon(icon_raw_snake,      buf_snake);
}

/* Draw icon buffer at (x, y) - respects alpha, skips fully transparent pixels */
static void draw_buf(const unsigned int *buf, int x, int y) {
    for (int row = 0; row < ICON_SIZE; row++) {
        for (int col = 0; col < ICON_SIZE; col++) {
            unsigned int px = buf[row * ICON_SIZE + col];
            unsigned char a = (px >> 24) & 0xFF;
            if (a < 32) continue; /* skip transparent */
            gpu_draw_pixel(x + col, y + row, px);
        }
    }
}

void icon_draw_png_terminal  (int x, int y) { draw_buf(buf_terminal,   x, y); }
void icon_draw_png_notepad   (int x, int y) { draw_buf(buf_notepad,    x, y); }
void icon_draw_png_calculator(int x, int y) { draw_buf(buf_calculator, x, y); }
void icon_draw_png_files     (int x, int y) { draw_buf(buf_files,      x, y); }
void icon_draw_png_clock     (int x, int y) { draw_buf(buf_clock,      x, y); }
void icon_draw_png_paint     (int x, int y) { draw_buf(buf_paint,      x, y); }
void icon_draw_png_sysinfo   (int x, int y) { draw_buf(buf_sysinfo,    x, y); }
void icon_draw_png_settings  (int x, int y) { draw_buf(buf_settings,   x, y); }
void icon_draw_png_folder    (int x, int y) { draw_buf(buf_folder,     x, y); }
void icon_draw_png_file      (int x, int y) { draw_buf(buf_file,       x, y); }
void icon_draw_png_snake     (int x, int y) { draw_buf(buf_snake,      x, y); }
