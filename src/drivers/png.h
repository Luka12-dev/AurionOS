#ifndef PNG_H
#define PNG_H

#include <stdint.h>

/* Decoded PNG image structure */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels; /* ARGB pixel data */
} PngImage;

/* Decode a PNG file from raw bytes into an image structure.
 * Returns 0 on success, negative on error.
 * Call png_free() when done with the image. */
int png_decode(const uint8_t *data, uint32_t data_len, PngImage *img);

/* Free decoded PNG image memory */
void png_free(PngImage *img);

/* Draw a pre-decoded icon at screen coordinates, scaled to dest_w x dest_h.
 * Uses nearest-neighbor scaling. Skips transparent pixels. */
void gpu_draw_icon(const uint32_t *pixels, int img_w, int img_h,
                   int dest_x, int dest_y, int dest_w, int dest_h);

/* Draw a pre-decoded icon within a window's client area */
void wm_draw_icon(void *win, const uint32_t *pixels, int img_w, int img_h,
                  int x, int y, int w, int h);

#endif
