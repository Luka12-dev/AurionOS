/*
 * icon_loader.c - Copy pre-decoded 64x64 RLE icon data into draw buffers.
 *
 * Icons are pre-decoded at build time by Python (icons/*.rle).
 * No PNG decoder, no allocator, no runtime risk.
 * Each icon is exactly 64*64 pixels when decompressed.
*/

#include "icon_loader.h"
#include <stdint.h>

/* GPU pixel draw function provided by vbe_graphics.c */
extern void gpu_draw_pixel(int x, int y, uint32_t color);

#define ICON_SIZE   64
#define ICON_PIXELS (ICON_SIZE * ICON_SIZE)  /* 4096 pixels */
#define ICON_BYTES  (ICON_PIXELS * 4)        /* 16384 bytes */

/* Static pixel buffers - one per icon, 96x96 ARGB */
static unsigned int buf_terminal  [ICON_PIXELS];
static unsigned int buf_browser   [ICON_PIXELS];
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

/* Raw icon data embedded via incbin in icon_data.asm - pre-decoded 96x96 BGRA */
extern const unsigned char icon_raw_terminal[];
extern const unsigned char icon_raw_browser[];
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

/* Decode a simple RLE-compressed 96x96 BGRA raw buffer into a 96x96 ARGB pixel buffer.
 * RLE Format: [byte: run_count][byte*4: BGRA pixel] */
static void decode_rle(const unsigned char *src, unsigned int *dst) {
    int pi = 0; /* Current pixel index */
    int si = 0; /* Current src byte index */
    
    while (pi < ICON_PIXELS) {
        unsigned char count = src[si++];
        unsigned char bv = src[si++];
        unsigned char gv = src[si++];
        unsigned char rv = src[si++];
        unsigned char av = src[si++];
        
        unsigned int px = ((unsigned int)av << 24) |
                          ((unsigned int)rv << 16) |
                          ((unsigned int)gv <<  8) |
                           (unsigned int)bv;
        
        for (int j = 0; j < count && pi < ICON_PIXELS; j++) {
            dst[pi++] = px;
        }
    }
}

/* Call once at boot - decompress static RLE data into BSS buffers */
void icons_load_all(void) {
    decode_rle(icon_raw_terminal,   buf_terminal);
    decode_rle(icon_raw_browser,    buf_browser);
    decode_rle(icon_raw_notepad,    buf_notepad);
    decode_rle(icon_raw_calculator, buf_calculator);
    decode_rle(icon_raw_files,      buf_files);
    decode_rle(icon_raw_clock,      buf_clock);
    decode_rle(icon_raw_paint,      buf_paint);
    decode_rle(icon_raw_sysinfo,    buf_sysinfo);
    decode_rle(icon_raw_settings,   buf_settings);
    decode_rle(icon_raw_folder,     buf_folder);
    decode_rle(icon_raw_file,       buf_file);
    decode_rle(icon_raw_snake,      buf_snake);
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
void icon_draw_png_browser   (int x, int y) { draw_buf(buf_browser,    x, y); }
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
