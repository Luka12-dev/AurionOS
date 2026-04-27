// WARNING: not fully implemmented (cooming in next versions)

/*
 * PNG Decoder Driver for Aurion OS
 * Minimal PNG parser: decodes PNG files into raw RGBA pixel buffers.
 * Supports 8-bit RGBA (color type 6) and RGB (color type 2).
 * Implements a minimal inflate for zlib/deflate decompression.
*/

#include <stdint.h>
#include <stdbool.h>

extern void c_puts(const char *s);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Minimal memory helpers */
static void png_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void png_memset(void *dst, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = val;
}

/* Endian helpers */
static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* Minimal DEFLATE/Inflate */

/* Fixed Huffman code lengths for literal/length codes 0..287 */
static void build_fixed_huffman(uint16_t *lit_table, uint16_t *dist_table) {
    /* Literal/Length: 0-143=8bits, 144-255=9bits, 256-279=7bits, 280-287=8bits */
    /* Distance: all 5 bits */
    /* We use a simple brute-force decode, not optimal but works for small images */
    (void)lit_table;
    (void)dist_table;
}

/* Bit reader state */
typedef struct {
    const uint8_t *data;
    uint32_t byte_pos;
    uint32_t bit_pos;
    uint32_t data_len;
} BitReader;

static uint32_t read_bits(BitReader *br, int count) {
    uint32_t val = 0;
    for (int i = 0; i < count; i++) {
        if (br->byte_pos >= br->data_len) return 0;
        val |= (uint32_t)((br->data[br->byte_pos] >> br->bit_pos) & 1) << i;
        br->bit_pos++;
        if (br->bit_pos >= 8) {
            br->bit_pos = 0;
            br->byte_pos++;
        }
    }
    return val;
}

static void align_to_byte(BitReader *br) {
    if (br->bit_pos > 0) {
        br->bit_pos = 0;
        br->byte_pos++;
    }
}

/* Length base values for codes 257-285 */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};

/* Distance base values for codes 0-29 */
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* Read a fixed Huffman literal/length code */
static int read_fixed_litlen(BitReader *br) {
    /* Read 7 bits first */
    uint32_t code = 0;
    for (int i = 0; i < 7; i++) {
        code = (code << 1) | read_bits(br, 1);
    }
    /* 256-279: 7-bit codes (0000000 - 0010111) -> values 256-279 */
    if (code <= 23) return 256 + code;

    /* Read 8th bit */
    code = (code << 1) | read_bits(br, 1);
    /* 0-143: 8-bit codes (00110000 - 10111111) -> values 0-143 */
    if (code >= 0x30 && code <= 0xBF) return code - 0x30;
    /* 280-287: 8-bit codes (11000000 - 11000111) -> values 280-287 */
    if (code >= 0xC0 && code <= 0xC7) return 280 + (code - 0xC0);

    /* Read 9th bit */
    code = (code << 1) | read_bits(br, 1);
    /* 144-255: 9-bit codes (110010000 - 111111111) -> values 144-255 */
    if (code >= 0x190 && code <= 0x1FF) return 144 + (code - 0x190);

    return -1; /* Invalid */
}

/* Read a fixed Huffman distance code (5 bits, reversed) */
static int read_fixed_dist(BitReader *br) {
    uint32_t code = 0;
    for (int i = 0; i < 5; i++) {
        code = (code << 1) | read_bits(br, 1);
    }
    return (int)code;
}

/* Inflate a zlib stream (skip 2-byte zlib header, decode deflate blocks) */
static int inflate_data(const uint8_t *compressed, uint32_t comp_len,
                        uint8_t *output, uint32_t out_max) {
    if (comp_len < 2) return -1;

    BitReader br;
    br.data = compressed + 2; /* Skip zlib header (CMF + FLG) */
    br.byte_pos = 0;
    br.bit_pos = 0;
    br.data_len = comp_len - 2;

    uint32_t out_pos = 0;
    int bfinal;

    do {
        bfinal = read_bits(&br, 1);
        int btype = read_bits(&br, 2);

        if (btype == 0) {
            /* Uncompressed block */
            align_to_byte(&br);
            if (br.byte_pos + 4 > br.data_len) return -1;
            uint16_t len = br.data[br.byte_pos] | (br.data[br.byte_pos + 1] << 8);
            br.byte_pos += 4; /* Skip len and nlen */
            if (br.byte_pos + len > br.data_len) return -1;
            if (out_pos + len > out_max) return -1;
            png_memcpy(output + out_pos, br.data + br.byte_pos, len);
            out_pos += len;
            br.byte_pos += len;
        } else if (btype == 1) {
            /* Fixed Huffman */
            while (1) {
                int sym = read_fixed_litlen(&br);
                if (sym < 0) return -1;
                if (sym == 256) break; /* End of block */
                if (sym < 256) {
                    if (out_pos >= out_max) return -1;
                    output[out_pos++] = (uint8_t)sym;
                } else {
                    /* Length code */
                    int li = sym - 257;
                    if (li < 0 || li >= 29) return -1;
                    uint32_t length = len_base[li] + read_bits(&br, len_extra[li]);

                    int di = read_fixed_dist(&br);
                    if (di < 0 || di >= 30) return -1;
                    uint32_t distance = dist_base[di] + read_bits(&br, dist_extra[di]);

                    if (distance > out_pos) return -1;
                    for (uint32_t k = 0; k < length; k++) {
                        if (out_pos >= out_max) return -1;
                        output[out_pos] = output[out_pos - distance];
                        out_pos++;
                    }
                }
            }
        } else if (btype == 2) {
            /* Dynamic Huffman -- complex, skip for now: treat as uncompressed */
            /* For a minimal OS, we can re-encode icons as uncompressed PNGs */
            /* or use a simpler format. Return error for now. */
            return -2; /* Dynamic Huffman not supported */
        } else {
            return -1; /* Invalid block type */
        }
    } while (!bfinal);

    return (int)out_pos;
}

/* PNG Filter reconstruction */
static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int png_unfilter(uint8_t *raw, uint32_t width, uint32_t height,
                        int bpp, uint8_t *out) {
    uint32_t stride = width * bpp;
    uint32_t raw_stride = stride + 1; /* +1 for filter type byte */

    for (uint32_t y = 0; y < height; y++) {
        uint8_t filter = raw[y * raw_stride];
        uint8_t *scanline = raw + y * raw_stride + 1;
        uint8_t *out_row = out + y * stride;
        uint8_t *prev_row = (y > 0) ? out + (y - 1) * stride : 0;

        for (uint32_t x = 0; x < stride; x++) {
            uint8_t raw_byte = scanline[x];
            uint8_t a = (x >= (uint32_t)bpp) ? out_row[x - bpp] : 0;
            uint8_t b = prev_row ? prev_row[x] : 0;
            uint8_t c = (prev_row && x >= (uint32_t)bpp) ? prev_row[x - bpp] : 0;
            uint8_t result;

            switch (filter) {
                case 0: result = raw_byte; break;
                case 1: result = raw_byte + a; break;
                case 2: result = raw_byte + b; break;
                case 3: result = raw_byte + (uint8_t)(((uint16_t)a + (uint16_t)b) / 2); break;
                case 4: result = raw_byte + paeth_predictor(a, b, c); break;
                default: result = raw_byte; break;
            }
            out_row[x] = result;
        }
    }
    return 0;
}

/* PNG Decoder */

/* Decoded icon structure stored in memory */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels; /* ARGB pixel data */
} PngImage;

#define PNG_MAX_CACHED 16
static PngImage png_cache[PNG_MAX_CACHED];
static int png_cache_count = 0;

/* PNG signature: 137 80 78 71 13 10 26 10 */
static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

int png_decode(const uint8_t *data, uint32_t data_len, PngImage *img) {
    if (data_len < 8) return -1;

    /* Check signature */
    for (int i = 0; i < 8; i++) {
        if (data[i] != png_sig[i]) return -1;
    }

    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0;
    int bpp = 4; /* bytes per pixel */

    /* Collect all IDAT data */
    uint8_t *idat_buf = 0;
    uint32_t idat_len = 0;
    uint32_t idat_capacity = 0;

    uint32_t pos = 8;
    bool ihdr_found = false;

    while (pos + 12 <= data_len) {
        uint32_t chunk_len = read_be32(data + pos);
        const uint8_t *chunk_type = data + pos + 4;
        const uint8_t *chunk_data = data + pos + 8;

        if (pos + 12 + chunk_len > data_len) break;

        /* IHDR */
        if (chunk_type[0] == 'I' && chunk_type[1] == 'H' &&
            chunk_type[2] == 'D' && chunk_type[3] == 'R') {
            if (chunk_len < 13) return -1;
            width = read_be32(chunk_data);
            height = read_be32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            ihdr_found = true;

            if (bit_depth != 8) {
                return -3; /* Only 8-bit supported */
            }
            if (color_type == 2) bpp = 3;      /* RGB */
            else if (color_type == 6) bpp = 4;  /* RGBA */
            else {
                return -4; /* Unsupported color type */
            }

            /* Allocate IDAT buffer (generous estimate) */
            idat_capacity = width * height * bpp + height + 4096;
            idat_buf = (uint8_t *)kmalloc(idat_capacity);
            if (!idat_buf) return -5;
        }

        /* IDAT */
        if (chunk_type[0] == 'I' && chunk_type[1] == 'D' &&
            chunk_type[2] == 'A' && chunk_type[3] == 'T') {
            if (idat_buf && idat_len + chunk_len <= idat_capacity) {
                png_memcpy(idat_buf + idat_len, chunk_data, chunk_len);
                idat_len += chunk_len;
            }
        }

        /* IEND */
        if (chunk_type[0] == 'I' && chunk_type[1] == 'E' &&
            chunk_type[2] == 'N' && chunk_type[3] == 'D') {
            break;
        }

        pos += 12 + chunk_len;
    }

    if (!ihdr_found || !idat_buf || idat_len == 0) {
        if (idat_buf) kfree(idat_buf);
        return -2;
    }

    /* Decompress IDAT data */
    uint32_t raw_size = (width * bpp + 1) * height;
    uint8_t *raw = (uint8_t *)kmalloc(raw_size + 1024);
    if (!raw) {
        kfree(idat_buf);
        return -5;
    }

    int decompressed = inflate_data(idat_buf, idat_len, raw, raw_size + 1024);
    kfree(idat_buf);

    if (decompressed < 0) {
        kfree(raw);
        return -6; /* Decompression failed */
    }

    /* Unfilter */
    uint32_t pixel_data_size = width * height * bpp;
    uint8_t *unfiltered = (uint8_t *)kmalloc(pixel_data_size);
    if (!unfiltered) {
        kfree(raw);
        return -5;
    }

    png_unfilter(raw, width, height, bpp, unfiltered);
    kfree(raw);

    /* Convert to ARGB pixel buffer */
    img->width = width;
    img->height = height;
    img->pixels = (uint32_t *)kmalloc(width * height * 4);
    if (!img->pixels) {
        kfree(unfiltered);
        return -5;
    }

    for (uint32_t i = 0; i < width * height; i++) {
        uint8_t r, g, b, a;
        if (bpp == 4) {
            r = unfiltered[i * 4 + 0];
            g = unfiltered[i * 4 + 1];
            b = unfiltered[i * 4 + 2];
            a = unfiltered[i * 4 + 3];
        } else {
            r = unfiltered[i * 3 + 0];
            g = unfiltered[i * 3 + 1];
            b = unfiltered[i * 3 + 2];
            a = 255;
        }
        img->pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                          ((uint32_t)g << 8)  | (uint32_t)b;
    }

    kfree(unfiltered);
    return 0;
}

/* Free a decoded PNG image */
void png_free(PngImage *img) {
    if (img && img->pixels) {
        kfree(img->pixels);
        img->pixels = 0;
    }
}

/* Draw a PNG image scaled to fit a target rectangle.
 * Uses nearest-neighbor sampling for speed. Skips fully transparent pixels. */
void gpu_draw_icon(const uint32_t *pixels, int img_w, int img_h,
                   int dest_x, int dest_y, int dest_w, int dest_h) {
    if (!pixels || img_w <= 0 || img_h <= 0 || dest_w <= 0 || dest_h <= 0)
        return;

    for (int dy = 0; dy < dest_h; dy++) {
        int src_y = (dy * img_h) / dest_h;
        if (src_y >= img_h) src_y = img_h - 1;
        for (int dx = 0; dx < dest_w; dx++) {
            int src_x = (dx * img_w) / dest_w;
            if (src_x >= img_w) src_x = img_w - 1;
            uint32_t pixel = pixels[src_y * img_w + src_x];
            uint8_t alpha = (pixel >> 24) & 0xFF;
            if (alpha < 128) continue; /* Skip mostly transparent */
            /* Draw as opaque (alpha already handled by skip) */
            uint32_t color = 0xFF000000 | (pixel & 0x00FFFFFF);
            gpu_draw_pixel(dest_x + dx, dest_y + dy, color);
        }
    }
}

/* Draw icon within a window's client area (coordinates relative to client) */
extern int wm_client_x(void *win);
extern int wm_client_y(void *win);
void wm_draw_icon(void *win, const uint32_t *pixels, int img_w, int img_h,
                  int x, int y, int w, int h) {
    int cx = wm_client_x(win);
    int cy = wm_client_y(win);
    gpu_draw_icon(pixels, img_w, img_h, cx + x, cy + y, w, h);
}