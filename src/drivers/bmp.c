/* ============================================================================
 * BMP Decoder Implementation for Custom Kernel OS
 * ============================================================================
 * Complete implementation of all BMP formats with full validation,
 * RLE decompression, bitfield decoding, palette handling, and
 * image manipulation utilities.
 * ============================================================================ */

#include "bmp.h"

/* --------------------------------------------------------------------------
 * External kernel memory allocator
 * -------------------------------------------------------------------------- */
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

/* We don't have string.h in kernel, so provide our own memset/memcpy */
static void bmp_memset(void *dst, uint8_t val, uint32_t count)
{
    uint8_t *d = (uint8_t *)dst;
    /* Fast path: align to 4 bytes then write 32-bit words */
    while (count && ((uint32_t)(uintptr_t)d & 3)) {
        *d++ = val;
        count--;
    }
    if (count >= 4) {
        uint32_t val32 = (uint32_t)val | ((uint32_t)val << 8) |
                         ((uint32_t)val << 16) | ((uint32_t)val << 24);
        uint32_t *d32 = (uint32_t *)d;
        while (count >= 16) {
            d32[0] = val32;
            d32[1] = val32;
            d32[2] = val32;
            d32[3] = val32;
            d32 += 4;
            count -= 16;
        }
        while (count >= 4) {
            *d32++ = val32;
            count -= 4;
        }
        d = (uint8_t *)d32;
    }
    while (count--) {
        *d++ = val;
    }
}

static void bmp_memcpy(void *dst, const void *src, uint32_t count)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    /* Fast path: if both aligned, copy 32-bit words */
    if (count >= 4 &&
        (((uint32_t)(uintptr_t)d | (uint32_t)(uintptr_t)s) & 3) == 0) {
        uint32_t *d32 = (uint32_t *)d;
        const uint32_t *s32 = (const uint32_t *)s;
        while (count >= 16) {
            d32[0] = s32[0];
            d32[1] = s32[1];
            d32[2] = s32[2];
            d32[3] = s32[3];
            d32 += 4;
            s32 += 4;
            count -= 16;
        }
        while (count >= 4) {
            *d32++ = *s32++;
            count -= 4;
        }
        d = (uint8_t *)d32;
        s = (const uint8_t *)s32;
    }
    while (count--) {
        *d++ = *s++;
    }
}

/* --------------------------------------------------------------------------
 * Little-endian byte reading utilities
 * -------------------------------------------------------------------------- */
static inline uint8_t read_u8(const uint8_t *p)
{
    return p[0];
}

static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline int32_t read_le32s(const uint8_t *p)
{
    return (int32_t)read_le32(p);
}

static inline int16_t read_le16s(const uint8_t *p)
{
    return (int16_t)read_le16(p);
}

/* --------------------------------------------------------------------------
 * Clamping and math utilities
 * -------------------------------------------------------------------------- */
static inline int32_t bmp_clamp_i32(int32_t val, int32_t lo, int32_t hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static inline uint8_t bmp_clamp_u8(int32_t val)
{
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

static inline uint32_t bmp_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static inline uint32_t bmp_max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static inline int32_t bmp_abs_i32(int32_t val)
{
    return val < 0 ? -val : val;
}

/* Safe multiplication check for overflow */
static int bmp_safe_mul(uint32_t a, uint32_t b, uint32_t *result)
{
    if (a == 0 || b == 0) {
        *result = 0;
        return 0;
    }
    if (a > 0xFFFFFFFF / b) {
        return -1; /* overflow */
    }
    *result = a * b;
    return 0;
}

/* Safe addition check for overflow */
static int bmp_safe_add(uint32_t a, uint32_t b, uint32_t *result)
{
    if (a > 0xFFFFFFFF - b) {
        return -1;
    }
    *result = a + b;
    return 0;
}

/* --------------------------------------------------------------------------
 * Bitfield analysis
 * -------------------------------------------------------------------------- */
static uint32_t count_trailing_zeros(uint32_t mask)
{
    uint32_t count = 0;
    if (mask == 0) return 32;
    while ((mask & 1) == 0) {
        count++;
        mask >>= 1;
    }
    return count;
}

static uint32_t count_set_bits(uint32_t mask)
{
    uint32_t count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

static int validate_bitfield_mask(uint32_t mask)
{
    /* A valid mask should be a contiguous run of set bits */
    if (mask == 0) return 1; /* zero mask is allowed (channel not present) */
    uint32_t shifted = mask >> count_trailing_zeros(mask);
    /* Check that shifted value is (2^n - 1) */
    return (shifted & (shifted + 1)) == 0;
}

static void init_bitfield(BmpBitfield *bf, uint32_t mask)
{
    bf->mask = mask;
    if (mask == 0) {
        bf->shift = 0;
        bf->bits = 0;
        bf->max_val = 0;
    } else {
        bf->shift = count_trailing_zeros(mask);
        bf->bits = count_set_bits(mask);
        bf->max_val = (1u << bf->bits) - 1;
    }
}

/* Extract and scale a channel value from a bitfield-encoded pixel */
static inline uint8_t extract_bitfield(uint32_t pixel, const BmpBitfield *bf)
{
    if (bf->bits == 0) return 0;
    uint32_t raw = (pixel & bf->mask) >> bf->shift;
    /* Scale to 8-bit range */
    if (bf->bits == 8) return (uint8_t)raw;
    if (bf->bits < 8) {
        /* Scale up: replicate bits to fill 8 bits */
        uint32_t result = raw << (8 - bf->bits);
        /* Fill remaining bits by repeating the pattern */
        uint32_t remaining = 8 - bf->bits;
        while (remaining > 0) {
            uint32_t copy_bits = bf->bits < remaining ? bf->bits : remaining;
            result |= raw >> (bf->bits - copy_bits);
            remaining -= copy_bits;
            raw <<= copy_bits; /* shift for next iteration if needed */
        }
        return (uint8_t)(result & 0xFF);
    }
    /* More than 8 bits: just take the top 8 */
    return (uint8_t)(raw >> (bf->bits - 8));
}

/* --------------------------------------------------------------------------
 * BMP File Header parsing (14 bytes)
 * -------------------------------------------------------------------------- */
static int parse_file_header(const uint8_t *data, uint32_t data_len,
                              BmpHeader *hdr)
{
    if (data_len < 14) {
        return BMP_ERR_FILE_TOO_SMALL;
    }

    hdr->signature = read_le16(data + 0);
    hdr->file_size = read_le32(data + 2);
    /* bytes 6-9: reserved */
    hdr->data_offset = read_le32(data + 10);

    /* Check BMP signature */
    if (hdr->signature != 0x4D42) { /* 'BM' in little-endian */
        return BMP_ERR_BAD_SIGNATURE;
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * BITMAPCOREHEADER parsing (OS/2 1.x, 12 bytes)
 * -------------------------------------------------------------------------- */
static int parse_core_header(const uint8_t *data, uint32_t data_len,
                              BmpHeader *hdr)
{
    if (data_len < 14 + 12) {
        return BMP_ERR_FILE_TOO_SMALL;
    }

    const uint8_t *h = data + 14;

    hdr->header_size = BMP_HEADER_CORE;
    hdr->width = (int32_t)read_le16(h + 4);
    hdr->height = (int32_t)read_le16(h + 6);
    hdr->planes = read_le16(h + 8);
    hdr->bits_per_pixel = read_le16(h + 10);

    /* Core header has no compression, image_size, resolution, or palette info */
    hdr->compression = BMP_COMPRESS_NONE;
    hdr->image_size = 0;
    hdr->x_ppm = 0;
    hdr->y_ppm = 0;
    hdr->colors_used = 0;
    hdr->colors_important = 0;
    hdr->is_core_header = 1;
    hdr->palette_entry_size = 3; /* Core uses 3-byte palette entries (BGR) */

    /* Core header always bottom-up, width and height are unsigned 16-bit */
    hdr->is_bottom_up = 1;

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * BITMAPINFOHEADER and later version parsing
 * -------------------------------------------------------------------------- */
static int parse_info_header(const uint8_t *data, uint32_t data_len,
                              BmpHeader *hdr)
{
    uint32_t hdr_size = hdr->header_size;

    if (data_len < 14 + hdr_size) {
        return BMP_ERR_FILE_TOO_SMALL;
    }

    const uint8_t *h = data + 14;

    hdr->width           = read_le32s(h + 4);
    hdr->height          = read_le32s(h + 8);
    hdr->planes          = read_le16(h + 12);
    hdr->bits_per_pixel  = read_le16(h + 14);
    hdr->compression     = read_le32(h + 16);
    hdr->image_size      = read_le32(h + 20);
    hdr->x_ppm           = read_le32s(h + 24);
    hdr->y_ppm           = read_le32s(h + 28);
    hdr->colors_used     = read_le32(h + 32);
    hdr->colors_important = read_le32(h + 36);
    hdr->is_core_header  = 0;
    hdr->palette_entry_size = 4; /* Info header uses 4-byte palette entries */

    /* Determine orientation */
    hdr->is_bottom_up = (hdr->height >= 0);

    /* Parse bitfield masks from header or following data */
    hdr->red_mask   = 0;
    hdr->green_mask = 0;
    hdr->blue_mask  = 0;
    hdr->alpha_mask = 0;
    hdr->has_bitfields = 0;

    /* V2+ headers include masks inline */
    if (hdr_size >= 52) {
        hdr->red_mask   = read_le32(h + 40);
        hdr->green_mask = read_le32(h + 44);
        hdr->blue_mask  = read_le32(h + 48);
        hdr->has_bitfields = 1;
    }
    if (hdr_size >= 56) {
        hdr->alpha_mask = read_le32(h + 52);
    }

    /* V4 header fields */
    if (hdr_size >= BMP_HEADER_V4) {
        hdr->cs_type = read_le32(h + 56);
        /* CIEXYZTRIPLE endpoints (9 x 4 bytes = 36 bytes at offset 60) */
        hdr->endpoints.red.x   = read_le32s(h + 60);
        hdr->endpoints.red.y   = read_le32s(h + 64);
        hdr->endpoints.red.z   = read_le32s(h + 68);
        hdr->endpoints.green.x = read_le32s(h + 72);
        hdr->endpoints.green.y = read_le32s(h + 76);
        hdr->endpoints.green.z = read_le32s(h + 80);
        hdr->endpoints.blue.x  = read_le32s(h + 84);
        hdr->endpoints.blue.y  = read_le32s(h + 88);
        hdr->endpoints.blue.z  = read_le32s(h + 92);
        hdr->gamma_red   = read_le32(h + 96);
        hdr->gamma_green = read_le32(h + 100);
        hdr->gamma_blue  = read_le32(h + 104);
    } else {
        hdr->cs_type = 0;
        bmp_memset(&hdr->endpoints, 0, sizeof(hdr->endpoints));
        hdr->gamma_red = 0;
        hdr->gamma_green = 0;
        hdr->gamma_blue = 0;
    }

    /* V5 header fields */
    if (hdr_size >= BMP_HEADER_V5) {
        hdr->intent       = read_le32(h + 108);
        hdr->profile_data = read_le32(h + 112);
        hdr->profile_size = read_le32(h + 116);
        /* reserved at offset 120, 4 bytes */
    } else {
        hdr->intent = 0;
        hdr->profile_data = 0;
        hdr->profile_size = 0;
    }

    /* If compression is BI_BITFIELDS and header didn't include masks,
     * read them from the 12 bytes following the header */
    if (hdr->compression == BMP_COMPRESS_BITFIELDS && !hdr->has_bitfields) {
        uint32_t mask_offset = 14 + hdr_size;
        if (data_len < mask_offset + 12) {
            return BMP_ERR_FILE_TRUNCATED;
        }
        hdr->red_mask   = read_le32(data + mask_offset);
        hdr->green_mask = read_le32(data + mask_offset + 4);
        hdr->blue_mask  = read_le32(data + mask_offset + 8);
        hdr->has_bitfields = 1;

        /* Some files also include alpha mask as 4th DWORD */
        if (data_len >= mask_offset + 16) {
            hdr->alpha_mask = read_le32(data + mask_offset + 12);
        }
    }

    /* BI_ALPHABITFIELDS: same as bitfields but with explicit alpha mask */
    if (hdr->compression == BMP_COMPRESS_ALPHABITS && !hdr->has_bitfields) {
        uint32_t mask_offset = 14 + hdr_size;
        if (data_len < mask_offset + 16) {
            return BMP_ERR_FILE_TRUNCATED;
        }
        hdr->red_mask   = read_le32(data + mask_offset);
        hdr->green_mask = read_le32(data + mask_offset + 4);
        hdr->blue_mask  = read_le32(data + mask_offset + 8);
        hdr->alpha_mask = read_le32(data + mask_offset + 12);
        hdr->has_bitfields = 1;
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * Unified header parsing
 * -------------------------------------------------------------------------- */
static int parse_header(const uint8_t *data, uint32_t data_len,
                         BmpHeader *hdr)
{
    int rc;

    bmp_memset(hdr, 0, sizeof(*hdr));

    /* Parse file header */
    rc = parse_file_header(data, data_len, hdr);
    if (rc != BMP_OK) return rc;

    /* Determine header type */
    if (data_len < 18) {
        return BMP_ERR_FILE_TOO_SMALL;
    }
    hdr->header_size = read_le32(data + 14);

    /* Route to appropriate parser */
    switch (hdr->header_size) {
        case BMP_HEADER_CORE:
            rc = parse_core_header(data, data_len, hdr);
            break;
        case BMP_HEADER_INFO:
        case BMP_HEADER_V2:
        case BMP_HEADER_V3:
        case BMP_HEADER_V4:
        case BMP_HEADER_V5:
            rc = parse_info_header(data, data_len, hdr);
            break;
        case BMP_HEADER_OS2_V2:
            /* OS/2 2.x has same initial layout as BITMAPINFOHEADER */
            rc = parse_info_header(data, data_len, hdr);
            break;
        default:
            /* Try to handle unknown header sizes >= 40 as info headers */
            if (hdr->header_size >= 40 && hdr->header_size <= 256) {
                rc = parse_info_header(data, data_len, hdr);
            } else {
                return BMP_ERR_BAD_HEADER_SIZE;
            }
            break;
    }
    if (rc != BMP_OK) return rc;

    /* Validate planes */
    if (hdr->planes != 1) {
        return BMP_ERR_BAD_PLANES;
    }

    /* Compute absolute dimensions */
    hdr->abs_width = (uint32_t)bmp_abs_i32(hdr->width);
    hdr->abs_height = (uint32_t)bmp_abs_i32(hdr->height);

    /* Width must be positive */
    if (hdr->width <= 0) {
        return BMP_ERR_BAD_DIMENSIONS;
    }
    if (hdr->abs_height == 0) {
        return BMP_ERR_BAD_DIMENSIONS;
    }

    /* Validate bits per pixel */
    switch (hdr->bits_per_pixel) {
        case 1: case 4: case 8: case 16: case 24: case 32:
            break;
        default:
            return BMP_ERR_UNSUPPORTED_BPP;
    }

    /* Validate compression vs. bits_per_pixel compatibility */
    switch (hdr->compression) {
        case BMP_COMPRESS_NONE:
            break;
        case BMP_COMPRESS_RLE8:
            if (hdr->bits_per_pixel != 8) return BMP_ERR_UNSUPPORTED_COMP;
            break;
        case BMP_COMPRESS_RLE4:
            if (hdr->bits_per_pixel != 4) return BMP_ERR_UNSUPPORTED_COMP;
            break;
        case BMP_COMPRESS_BITFIELDS:
        case BMP_COMPRESS_ALPHABITS:
            if (hdr->bits_per_pixel != 16 && hdr->bits_per_pixel != 32) {
                return BMP_ERR_UNSUPPORTED_COMP;
            }
            break;
        case BMP_COMPRESS_JPEG:
        case BMP_COMPRESS_PNG:
            return BMP_ERR_UNSUPPORTED_COMP;
        default:
            return BMP_ERR_UNSUPPORTED_COMP;
    }

    /* Calculate palette count */
    if (hdr->bits_per_pixel <= 8) {
        uint32_t max_colors = 1u << hdr->bits_per_pixel;
        if (hdr->colors_used == 0 || hdr->colors_used > max_colors) {
            hdr->palette_count = max_colors;
        } else {
            hdr->palette_count = hdr->colors_used;
        }
    } else {
        hdr->palette_count = hdr->colors_used; /* may be 0 */
    }

    /* Calculate row stride */
    uint32_t bits_per_row;
    if (bmp_safe_mul(hdr->abs_width, (uint32_t)hdr->bits_per_pixel,
                     &bits_per_row) != 0) {
        return BMP_ERR_OVERFLOW;
    }
    hdr->row_stride = ((bits_per_row + 31) / 32) * 4;

    /* Set default bitfield masks if not explicitly provided */
    if (!hdr->has_bitfields) {
        if (hdr->bits_per_pixel == 16) {
            /* Default 16-bit: RGB555 */
            hdr->red_mask   = 0x7C00;
            hdr->green_mask = 0x03E0;
            hdr->blue_mask  = 0x001F;
            hdr->alpha_mask = 0x0000;
            hdr->has_bitfields = 1;
        } else if (hdr->bits_per_pixel == 32) {
            /* Default 32-bit: ARGB8888 or XRGB8888 */
            hdr->red_mask   = 0x00FF0000;
            hdr->green_mask = 0x0000FF00;
            hdr->blue_mask  = 0x000000FF;
            hdr->alpha_mask = 0xFF000000;
            hdr->has_bitfields = 1;
        }
    }

    /* Determine if alpha channel is present */
    hdr->has_alpha_channel = (hdr->alpha_mask != 0);

    /* Validate data offset */
    if (hdr->data_offset < 14 + hdr->header_size) {
        /* Data offset should be at least past file+info headers */
        /* Some files have extra data between header and pixels */
    }
    if (hdr->data_offset >= data_len) {
        return BMP_ERR_BAD_DATA_OFFSET;
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * Palette reading
 * -------------------------------------------------------------------------- */
static int read_palette(const uint8_t *data, uint32_t data_len,
                         const BmpHeader *hdr, BmpColorEntry *palette)
{
    if (hdr->palette_count == 0) return BMP_OK;

    /* Palette starts right after the info header */
    uint32_t palette_offset = 14 + hdr->header_size;

    /* Account for bitfield masks stored after header (not in header) */
    if (hdr->header_size == BMP_HEADER_INFO) {
        if (hdr->compression == BMP_COMPRESS_BITFIELDS) {
            palette_offset += 12;
        } else if (hdr->compression == BMP_COMPRESS_ALPHABITS) {
            palette_offset += 16;
        }
    }

    uint32_t entry_size = hdr->palette_entry_size;
    uint32_t palette_size;
    if (bmp_safe_mul(hdr->palette_count, entry_size, &palette_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t palette_end;
    if (bmp_safe_add(palette_offset, palette_size, &palette_end) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    if (palette_end > data_len) {
        return BMP_ERR_BAD_PALETTE;
    }

    const uint8_t *pal = data + palette_offset;
    for (uint32_t i = 0; i < hdr->palette_count; i++) {
        const uint8_t *entry = pal + i * entry_size;
        palette[i].blue  = entry[0];
        palette[i].green = entry[1];
        palette[i].red   = entry[2];
        if (entry_size == 4) {
            palette[i].alpha = entry[3];
        } else {
            palette[i].alpha = 0xFF;
        }
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * 1-bit pixel decoding (monochrome)
 * -------------------------------------------------------------------------- */
static int decode_1bit(const uint8_t *data, uint32_t data_len,
                        const BmpHeader *hdr, const BmpColorEntry *palette,
                        uint32_t *pixels)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = hdr->is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = src + src_y * hdr->row_stride;

        /* Bounds check */
        if ((uint32_t)(row - data) + ((w + 7) / 8) > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }

        uint32_t dst_idx = y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t byte_idx = x / 8;
            uint32_t bit_idx = 7 - (x % 8);
            uint8_t bit = (row[byte_idx] >> bit_idx) & 1;

            if (bit < hdr->palette_count) {
                const BmpColorEntry *c = &palette[bit];
                pixels[dst_idx + x] = (0xFF000000) |
                                       ((uint32_t)c->red << 16) |
                                       ((uint32_t)c->green << 8) |
                                       (uint32_t)c->blue;
            } else {
                pixels[dst_idx + x] = 0xFF000000; /* black fallback */
            }
        }
    }
    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * 4-bit pixel decoding (uncompressed)
 * -------------------------------------------------------------------------- */
static int decode_4bit(const uint8_t *data, uint32_t data_len,
                        const BmpHeader *hdr, const BmpColorEntry *palette,
                        uint32_t *pixels)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = hdr->is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = src + src_y * hdr->row_stride;

        if ((uint32_t)(row - data) + ((w + 1) / 2) > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }

        uint32_t dst_idx = y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t byte_idx = x / 2;
            uint8_t nibble;
            if (x % 2 == 0) {
                nibble = (row[byte_idx] >> 4) & 0x0F;
            } else {
                nibble = row[byte_idx] & 0x0F;
            }

            if (nibble < hdr->palette_count) {
                const BmpColorEntry *c = &palette[nibble];
                pixels[dst_idx + x] = (0xFF000000) |
                                       ((uint32_t)c->red << 16) |
                                       ((uint32_t)c->green << 8) |
                                       (uint32_t)c->blue;
            } else {
                pixels[dst_idx + x] = 0xFF000000;
            }
        }
    }
    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * 8-bit pixel decoding (uncompressed)
 * -------------------------------------------------------------------------- */
static int decode_8bit(const uint8_t *data, uint32_t data_len,
                        const BmpHeader *hdr, const BmpColorEntry *palette,
                        uint32_t *pixels)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = hdr->is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = src + src_y * hdr->row_stride;

        if ((uint32_t)(row - data) + w > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }

        uint32_t dst_idx = y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t idx = row[x];
            if (idx < hdr->palette_count) {
                const BmpColorEntry *c = &palette[idx];
                pixels[dst_idx + x] = (0xFF000000) |
                                       ((uint32_t)c->red << 16) |
                                       ((uint32_t)c->green << 8) |
                                       (uint32_t)c->blue;
            } else {
                pixels[dst_idx + x] = 0xFF000000;
            }
        }
    }
    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * RLE8 decompression
 * -------------------------------------------------------------------------- */
static int decode_rle8(const uint8_t *data, uint32_t data_len,
                        const BmpHeader *hdr, const BmpColorEntry *palette,
                        uint32_t *pixels)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;
    uint32_t src_len = data_len - hdr->data_offset;
    uint32_t si = 0;

    /* RLE is always bottom-up */
    int32_t x = 0;
    int32_t y = (int32_t)h - 1;

    /* Initialize to transparent */
    bmp_memset(pixels, 0, w * h * sizeof(uint32_t));

    while (si + 1 < src_len && y >= 0) {
        uint8_t count = src[si];
        uint8_t value = src[si + 1];
        si += 2;

        if (count > 0) {
            /* Encoded run: repeat 'value' count times */
            for (uint8_t i = 0; i < count && x < (int32_t)w; i++) {
                if (y >= 0 && y < (int32_t)h && x >= 0) {
                    uint32_t dst_idx = (h - 1 - (uint32_t)y) * w + (uint32_t)x;
                    if (value < hdr->palette_count) {
                        const BmpColorEntry *c = &palette[value];
                        pixels[dst_idx] = (0xFF000000) |
                                           ((uint32_t)c->red << 16) |
                                           ((uint32_t)c->green << 8) |
                                           (uint32_t)c->blue;
                    }
                }
                x++;
            }
        } else {
            /* Escape sequences */
            switch (value) {
                case 0: /* End of line */
                    x = 0;
                    y--;
                    break;
                case 1: /* End of bitmap */
                    return BMP_OK;
                case 2: /* Delta: move cursor */
                    if (si + 1 >= src_len) return BMP_ERR_BAD_RLE_DATA;
                    x += src[si];
                    y -= src[si + 1];
                    si += 2;
                    break;
                default: {
                    /* Absolute mode: 'value' literal pixels follow */
                    uint8_t abs_count = value;
                    if (si + abs_count > src_len) {
                        return BMP_ERR_BAD_RLE_DATA;
                    }
                    for (uint8_t i = 0; i < abs_count; i++) {
                        if (y >= 0 && y < (int32_t)h &&
                            x >= 0 && x < (int32_t)w) {
                            uint32_t dst_idx = (h - 1 - (uint32_t)y) * w +
                                               (uint32_t)x;
                            uint8_t idx = src[si];
                            if (idx < hdr->palette_count) {
                                const BmpColorEntry *c = &palette[idx];
                                pixels[dst_idx] = (0xFF000000) |
                                                   ((uint32_t)c->red << 16) |
                                                   ((uint32_t)c->green << 8) |
                                                   (uint32_t)c->blue;
                            }
                        }
                        si++;
                        x++;
                    }
                    /* Absolute runs are padded to word boundary */
                    if (abs_count & 1) {
                        si++;
                    }
                    break;
                }
            }
        }
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * RLE4 decompression
 * -------------------------------------------------------------------------- */
static int decode_rle4(const uint8_t *data, uint32_t data_len,
                        const BmpHeader *hdr, const BmpColorEntry *palette,
                        uint32_t *pixels)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;
    uint32_t src_len = data_len - hdr->data_offset;
    uint32_t si = 0;

    int32_t x = 0;
    int32_t y = (int32_t)h - 1;

    bmp_memset(pixels, 0, w * h * sizeof(uint32_t));

    while (si + 1 < src_len && y >= 0) {
        uint8_t count = src[si];
        uint8_t value = src[si + 1];
        si += 2;

        if (count > 0) {
            /* Encoded run: alternate between high and low nibbles */
            uint8_t hi = (value >> 4) & 0x0F;
            uint8_t lo = value & 0x0F;
            for (uint8_t i = 0; i < count && x < (int32_t)w; i++) {
                uint8_t idx = (i & 1) ? lo : hi;
                if (y >= 0 && y < (int32_t)h && x >= 0) {
                    uint32_t dst_idx = (h - 1 - (uint32_t)y) * w +
                                       (uint32_t)x;
                    if (idx < hdr->palette_count) {
                        const BmpColorEntry *c = &palette[idx];
                        pixels[dst_idx] = (0xFF000000) |
                                           ((uint32_t)c->red << 16) |
                                           ((uint32_t)c->green << 8) |
                                           (uint32_t)c->blue;
                    }
                }
                x++;
            }
        } else {
            switch (value) {
                case 0: /* End of line */
                    x = 0;
                    y--;
                    break;
                case 1: /* End of bitmap */
                    return BMP_OK;
                case 2: /* Delta */
                    if (si + 1 >= src_len) return BMP_ERR_BAD_RLE_DATA;
                    x += src[si];
                    y -= src[si + 1];
                    si += 2;
                    break;
                default: {
                    /* Absolute mode for 4-bit */
                    uint8_t abs_count = value;
                    uint32_t bytes_needed = (abs_count + 1) / 2;
                    if (si + bytes_needed > src_len) {
                        return BMP_ERR_BAD_RLE_DATA;
                    }
                    for (uint8_t i = 0; i < abs_count; i++) {
                        uint8_t byte = src[si + i / 2];
                        uint8_t idx;
                        if (i % 2 == 0) {
                            idx = (byte >> 4) & 0x0F;
                        } else {
                            idx = byte & 0x0F;
                        }
                        if (y >= 0 && y < (int32_t)h &&
                            x >= 0 && x < (int32_t)w) {
                            uint32_t dst_idx = (h - 1 - (uint32_t)y) * w +
                                               (uint32_t)x;
                            if (idx < hdr->palette_count) {
                                const BmpColorEntry *c = &palette[idx];
                                pixels[dst_idx] = (0xFF000000) |
                                                   ((uint32_t)c->red << 16) |
                                                   ((uint32_t)c->green << 8) |
                                                   (uint32_t)c->blue;
                            }
                        }
                        x++;
                    }
                    si += bytes_needed;
                    /* Pad to word boundary */
                    if (bytes_needed & 1) {
                        si++;
                    }
                    break;
                }
            }
        }
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * 16-bit pixel decoding (with bitfield masks)
 * -------------------------------------------------------------------------- */
static int decode_16bit(const uint8_t *data, uint32_t data_len,
                         const BmpHeader *hdr, uint32_t *pixels,
                         const BmpBitfield *bf_r, const BmpBitfield *bf_g,
                         const BmpBitfield *bf_b, const BmpBitfield *bf_a)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;
    int has_alpha = (bf_a->mask != 0);

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = hdr->is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = src + src_y * hdr->row_stride;

        if ((uint32_t)(row - data) + w * 2 > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }

        uint32_t dst_idx = y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint16_t pixel16 = read_le16(row + x * 2);
            uint32_t pixel32 = (uint32_t)pixel16;

            uint8_t r = extract_bitfield(pixel32, bf_r);
            uint8_t g = extract_bitfield(pixel32, bf_g);
            uint8_t b = extract_bitfield(pixel32, bf_b);
            uint8_t a = has_alpha ? extract_bitfield(pixel32, bf_a) : 0xFF;

            pixels[dst_idx + x] = ((uint32_t)a << 24) |
                                   ((uint32_t)r << 16) |
                                   ((uint32_t)g << 8) |
                                   (uint32_t)b;
        }
    }
    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * 24-bit pixel decoding (BGR, uncompressed)
 * -------------------------------------------------------------------------- */
static int decode_24bit(const uint8_t *data, uint32_t data_len,
                         const BmpHeader *hdr, uint32_t *pixels)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = hdr->is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = src + src_y * hdr->row_stride;

        if ((uint32_t)(row - data) + w * 3 > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }

        uint32_t dst_idx = y * w;
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *p = row + x * 3;
            uint8_t b_val = p[0];
            uint8_t g_val = p[1];
            uint8_t r_val = p[2];

            pixels[dst_idx + x] = 0xFF000000 |
                                   ((uint32_t)r_val << 16) |
                                   ((uint32_t)g_val << 8) |
                                   (uint32_t)b_val;
        }
    }
    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * 32-bit pixel decoding (with bitfield masks)
 * -------------------------------------------------------------------------- */
static int decode_32bit(const uint8_t *data, uint32_t data_len,
                         const BmpHeader *hdr, uint32_t *pixels,
                         const BmpBitfield *bf_r, const BmpBitfield *bf_g,
                         const BmpBitfield *bf_b, const BmpBitfield *bf_a)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;
    int has_alpha = (bf_a->mask != 0);

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = hdr->is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = src + src_y * hdr->row_stride;

        if ((uint32_t)(row - data) + w * 4 > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }

        uint32_t dst_idx = y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t pixel = read_le32(row + x * 4);

            uint8_t r = extract_bitfield(pixel, bf_r);
            uint8_t g = extract_bitfield(pixel, bf_g);
            uint8_t b = extract_bitfield(pixel, bf_b);
            uint8_t a;

            if (has_alpha) {
                a = extract_bitfield(pixel, bf_a);
            } else {
                a = 0xFF;
            }

            pixels[dst_idx + x] = ((uint32_t)a << 24) |
                                   ((uint32_t)r << 16) |
                                   ((uint32_t)g << 8) |
                                   (uint32_t)b;
        }
    }
    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * 32-bit fast path for standard BGRA layout (most common case)
 * -------------------------------------------------------------------------- */
static int decode_32bit_bgra_fast(const uint8_t *data, uint32_t data_len,
                                   const BmpHeader *hdr, uint32_t *pixels,
                                   int has_alpha)
{
    uint32_t w = hdr->abs_width;
    uint32_t h = hdr->abs_height;
    const uint8_t *src = data + hdr->data_offset;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = hdr->is_bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = src + src_y * hdr->row_stride;

        if ((uint32_t)(row - data) + w * 4 > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }

        uint32_t dst_idx = y * w;
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *p = row + x * 4;
            uint8_t b_val = p[0];
            uint8_t g_val = p[1];
            uint8_t r_val = p[2];
            uint8_t a_val = has_alpha ? p[3] : 0xFF;

            pixels[dst_idx + x] = ((uint32_t)a_val << 24) |
                                   ((uint32_t)r_val << 16) |
                                   ((uint32_t)g_val << 8) |
                                   (uint32_t)b_val;
        }
    }
    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * Check if standard BGRA mask layout for fast path
 * -------------------------------------------------------------------------- */
static int is_standard_bgra_masks(const BmpHeader *hdr)
{
    return (hdr->red_mask   == 0x00FF0000 &&
            hdr->green_mask == 0x0000FF00 &&
            hdr->blue_mask  == 0x000000FF &&
            (hdr->alpha_mask == 0xFF000000 || hdr->alpha_mask == 0x00000000));
}

/* --------------------------------------------------------------------------
 * Detect if all alpha values in a 32-bit image are zero
 * (common case where alpha should be treated as opaque)
 * -------------------------------------------------------------------------- */
static void fix_zero_alpha(uint32_t *pixels, uint32_t count)
{
    /* Check if ALL alpha values are zero */
    int all_zero = 1;
    for (uint32_t i = 0; i < count; i++) {
        if (pixels[i] & 0xFF000000) {
            all_zero = 0;
            break;
        }
    }

    /* If all alpha is zero, set all to opaque */
    if (all_zero) {
        for (uint32_t i = 0; i < count; i++) {
            pixels[i] |= 0xFF000000;
        }
    }
}

/* --------------------------------------------------------------------------
 * Main decode dispatch
 * -------------------------------------------------------------------------- */
static int decode_pixels(const uint8_t *data, uint32_t data_len,
                          const BmpHeader *hdr, const BmpColorEntry *palette,
                          uint32_t *pixels, const BmpDecodeOptions *opts)
{
    int rc = BMP_OK;

    /* Set up bitfields for 16/32 bit modes */
    BmpBitfield bf_r, bf_g, bf_b, bf_a;
    if (hdr->has_bitfields) {
        init_bitfield(&bf_r, hdr->red_mask);
        init_bitfield(&bf_g, hdr->green_mask);
        init_bitfield(&bf_b, hdr->blue_mask);
        init_bitfield(&bf_a, hdr->alpha_mask);

        /* Validate masks for bitfield modes */
        if (hdr->compression == BMP_COMPRESS_BITFIELDS ||
            hdr->compression == BMP_COMPRESS_ALPHABITS) {
            if (!validate_bitfield_mask(hdr->red_mask) ||
                !validate_bitfield_mask(hdr->green_mask) ||
                !validate_bitfield_mask(hdr->blue_mask) ||
                !validate_bitfield_mask(hdr->alpha_mask)) {
                return BMP_ERR_BAD_BITFIELDS;
            }
            /* Check for overlapping masks */
            uint32_t all_masks = hdr->red_mask | hdr->green_mask |
                                  hdr->blue_mask;
            uint32_t mask_sum = count_set_bits(hdr->red_mask) +
                                 count_set_bits(hdr->green_mask) +
                                 count_set_bits(hdr->blue_mask);
            if (count_set_bits(all_masks) != mask_sum) {
                return BMP_ERR_BAD_BITFIELDS;
            }
        }
    }

    switch (hdr->bits_per_pixel) {
        case 1:
            rc = decode_1bit(data, data_len, hdr, palette, pixels);
            break;

        case 4:
            if (hdr->compression == BMP_COMPRESS_RLE4) {
                rc = decode_rle4(data, data_len, hdr, palette, pixels);
            } else {
                rc = decode_4bit(data, data_len, hdr, palette, pixels);
            }
            break;

        case 8:
            if (hdr->compression == BMP_COMPRESS_RLE8) {
                rc = decode_rle8(data, data_len, hdr, palette, pixels);
            } else {
                rc = decode_8bit(data, data_len, hdr, palette, pixels);
            }
            break;

        case 16:
            rc = decode_16bit(data, data_len, hdr, pixels,
                              &bf_r, &bf_g, &bf_b, &bf_a);
            break;

        case 24:
            rc = decode_24bit(data, data_len, hdr, pixels);
            break;

        case 32:
            /* Use fast path for standard BGRA layout */
            if (is_standard_bgra_masks(hdr) &&
                hdr->compression != BMP_COMPRESS_BITFIELDS) {
                rc = decode_32bit_bgra_fast(data, data_len, hdr, pixels,
                                            hdr->has_alpha_channel);
            } else {
                rc = decode_32bit(data, data_len, hdr, pixels,
                                  &bf_r, &bf_g, &bf_b, &bf_a);
            }
            break;

        default:
            rc = BMP_ERR_UNSUPPORTED_BPP;
            break;
    }

    if (rc != BMP_OK) return rc;

    /* Fix zero alpha for 32-bit images where alpha mask exists but all zero */
    if (hdr->bits_per_pixel == 32 && hdr->has_alpha_channel) {
        fix_zero_alpha(pixels, hdr->abs_width * hdr->abs_height);
    }

    /* Force alpha opaque if requested */
    if (opts && opts->force_alpha_opaque) {
        uint32_t count = hdr->abs_width * hdr->abs_height;
        for (uint32_t i = 0; i < count; i++) {
            pixels[i] |= 0xFF000000;
        }
    }

    /* Premultiply alpha if requested */
    if (opts && opts->premultiply_alpha && hdr->has_alpha_channel) {
        bmp_premultiply_alpha(pixels, hdr->abs_width * hdr->abs_height);
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * DPI calculation from pixels-per-meter
 * -------------------------------------------------------------------------- */
static int32_t ppm_to_dpi(int32_t ppm)
{
    if (ppm <= 0) return 0;
    /* DPI = PPM * 25.4 / 1000 = PPM * 127 / 5000 (integer approximation) */
    return (int32_t)(((int64_t)ppm * 127 + 2500) / 5000);
}

/* PUBLIC API IMPLEMENTATION */

/* --------------------------------------------------------------------------
 * bmp_default_options
 * -------------------------------------------------------------------------- */
void bmp_default_options(BmpDecodeOptions *opts)
{
    if (!opts) return;
    bmp_memset(opts, 0, sizeof(*opts));
    opts->force_alpha_opaque = 0;
    opts->preserve_palette = 1;
    opts->extract_icc = 0;
    opts->max_width = BMP_MAX_DIMENSION;
    opts->max_height = BMP_MAX_DIMENSION;
    opts->output_format = BMP_PIXFMT_ARGB8888;
    opts->premultiply_alpha = 0;
}

/* --------------------------------------------------------------------------
 * bmp_parse_header
 * -------------------------------------------------------------------------- */
int bmp_parse_header(const uint8_t *data, uint32_t data_len,
                     BmpHeader *header)
{
    if (!data || !header) return BMP_ERR_NULL_INPUT;
    return parse_header(data, data_len, header);
}

/* --------------------------------------------------------------------------
 * bmp_validate
 * -------------------------------------------------------------------------- */
int bmp_validate(const uint8_t *data, uint32_t data_len)
{
    if (!data) return BMP_ERR_NULL_INPUT;
    BmpHeader hdr;
    int rc = parse_header(data, data_len, &hdr);
    if (rc != BMP_OK) return rc;

    /* Check dimension limits */
    if (hdr.abs_width > BMP_MAX_DIMENSION ||
        hdr.abs_height > BMP_MAX_DIMENSION) {
        return BMP_ERR_DIMENSION_LIMIT;
    }

    /* Check pixel count overflow */
    uint32_t pixel_count;
    if (bmp_safe_mul(hdr.abs_width, hdr.abs_height, &pixel_count) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    /* For non-RLE, verify we have enough data */
    if (hdr.compression == BMP_COMPRESS_NONE ||
        hdr.compression == BMP_COMPRESS_BITFIELDS ||
        hdr.compression == BMP_COMPRESS_ALPHABITS) {
        uint32_t required;
        if (bmp_safe_mul(hdr.row_stride, hdr.abs_height, &required) != 0) {
            return BMP_ERR_OVERFLOW;
        }
        uint32_t total;
        if (bmp_safe_add(hdr.data_offset, required, &total) != 0) {
            return BMP_ERR_OVERFLOW;
        }
        if (total > data_len) {
            return BMP_ERR_FILE_TRUNCATED;
        }
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * bmp_decode
 * -------------------------------------------------------------------------- */
int bmp_decode(const uint8_t *data, uint32_t data_len, BmpImage *img)
{
    return bmp_decode_ex(data, data_len, img, (const BmpDecodeOptions *)0);
}

/* --------------------------------------------------------------------------
 * bmp_decode_ex
 * -------------------------------------------------------------------------- */
int bmp_decode_ex(const uint8_t *data, uint32_t data_len,
                  BmpImage *img, const BmpDecodeOptions *opts)
{
    if (!data || !img) return BMP_ERR_NULL_INPUT;

    /* Zero out the image structure */
    bmp_memset(img, 0, sizeof(*img));

    /* Use default options if none provided */
    BmpDecodeOptions default_opts;
    if (!opts) {
        bmp_default_options(&default_opts);
        opts = &default_opts;
    }

    /* Parse headers */
    BmpHeader hdr;
    int rc = parse_header(data, data_len, &hdr);
    if (rc != BMP_OK) return rc;

    /* Apply dimension limits */
    uint32_t max_w = opts->max_width ? opts->max_width : BMP_MAX_DIMENSION;
    uint32_t max_h = opts->max_height ? opts->max_height : BMP_MAX_DIMENSION;
    if (hdr.abs_width > max_w || hdr.abs_height > max_h) {
        return BMP_ERR_DIMENSION_LIMIT;
    }

    /* Calculate pixel buffer size with overflow check */
    uint32_t pixel_count;
    if (bmp_safe_mul(hdr.abs_width, hdr.abs_height, &pixel_count) != 0) {
        return BMP_ERR_OVERFLOW;
    }
    if (pixel_count > BMP_MAX_PIXEL_COUNT) {
        return BMP_ERR_DIMENSION_LIMIT;
    }

    uint32_t pixel_buf_size;
    if (bmp_safe_mul(pixel_count, (uint32_t)sizeof(uint32_t),
                     &pixel_buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    /* Read palette if needed */
    BmpColorEntry palette[256];
    bmp_memset(palette, 0, sizeof(palette));

    if (hdr.bits_per_pixel <= 8) {
        rc = read_palette(data, data_len, &hdr, palette);
        if (rc != BMP_OK) return rc;
    }

    /* Allocate pixel buffer */
    uint32_t *pixels = (uint32_t *)kmalloc(pixel_buf_size);
    if (!pixels) {
        return BMP_ERR_OUT_OF_MEMORY;
    }

    /* Initialize pixels to transparent black */
    bmp_memset(pixels, 0, pixel_buf_size);

    /* Decode pixels */
    rc = decode_pixels(data, data_len, &hdr, palette, pixels, opts);
    if (rc != BMP_OK) {
        kfree(pixels);
        return rc;
    }

    /* Fill output image structure */
    img->width = hdr.abs_width;
    img->height = hdr.abs_height;
    img->pixels = pixels;
    img->pixel_count = pixel_count;
    img->stride = hdr.abs_width * sizeof(uint32_t);
    img->original_bpp = hdr.bits_per_pixel;
    img->original_comp = hdr.compression;
    img->dpi_x = ppm_to_dpi(hdr.x_ppm);
    img->dpi_y = ppm_to_dpi(hdr.y_ppm);
    img->header_version = hdr.header_size;
    img->color_space = hdr.cs_type;

    /* Copy palette if requested and available */
    if (opts->preserve_palette && hdr.palette_count > 0) {
        uint32_t copy_count = bmp_min_u32(hdr.palette_count, 256);
        bmp_memcpy(img->palette, palette, copy_count * sizeof(BmpColorEntry));
        img->palette_count = copy_count;
    }

    /* Store bitfield info */
    if (hdr.has_bitfields) {
        init_bitfield(&img->bf_red,   hdr.red_mask);
        init_bitfield(&img->bf_green, hdr.green_mask);
        init_bitfield(&img->bf_blue,  hdr.blue_mask);
        init_bitfield(&img->bf_alpha, hdr.alpha_mask);
    }

    /* Extract ICC profile if requested and available */
    if (opts->extract_icc && hdr.profile_size > 0 &&
        hdr.header_size >= BMP_HEADER_V5) {
        uint32_t prof_offset = 14 + hdr.profile_data;
        uint32_t prof_end;
        if (bmp_safe_add(prof_offset, hdr.profile_size, &prof_end) == 0 &&
            prof_end <= data_len) {
            uint8_t *icc = (uint8_t *)kmalloc(hdr.profile_size);
            if (icc) {
                bmp_memcpy(icc, data + prof_offset, hdr.profile_size);
                img->icc_profile = icc;
                img->icc_profile_size = hdr.profile_size;
            }
        }
    }

    return BMP_OK;
}

/* --------------------------------------------------------------------------
 * bmp_free
 * -------------------------------------------------------------------------- */
void bmp_free(BmpImage *img)
{
    if (!img) return;
    if (img->pixels) {
        kfree(img->pixels);
        img->pixels = (uint32_t *)0;
    }
    if (img->icc_profile) {
        kfree(img->icc_profile);
        img->icc_profile = (uint8_t *)0;
    }
    img->width = 0;
    img->height = 0;
    img->pixel_count = 0;
    img->stride = 0;
    img->palette_count = 0;
    img->icc_profile_size = 0;
}

/* Pixel Format Conversion */

uint32_t bmp_bytes_per_pixel(int fmt)
{
    switch (fmt) {
        case BMP_PIXFMT_ARGB8888:
        case BMP_PIXFMT_RGBA8888:
        case BMP_PIXFMT_ABGR8888:
            return 4;
        case BMP_PIXFMT_RGB888:
        case BMP_PIXFMT_BGR888:
            return 3;
        case BMP_PIXFMT_RGB565:
        case BMP_PIXFMT_ARGB1555:
            return 2;
        case BMP_PIXFMT_GRAYSCALE8:
            return 1;
        default:
            return 0;
    }
}

int bmp_convert_pixels(const uint32_t *src, void *dst,
                       uint32_t count, int dst_fmt)
{
    if (!src || !dst) return BMP_ERR_NULL_INPUT;

    uint8_t *d8 = (uint8_t *)dst;
    uint16_t *d16 = (uint16_t *)dst;
    uint32_t *d32 = (uint32_t *)dst;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = src[i];
        uint8_t a = (pixel >> 24) & 0xFF;
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;

        switch (dst_fmt) {
            case BMP_PIXFMT_ARGB8888:
                d32[i] = pixel;
                break;

            case BMP_PIXFMT_RGBA8888:
                d32[i] = ((uint32_t)r << 24) | ((uint32_t)g << 16) |
                          ((uint32_t)b << 8) | a;
                break;

            case BMP_PIXFMT_ABGR8888:
                d32[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                          ((uint32_t)g << 8) | r;
                break;

            case BMP_PIXFMT_RGB888:
                d8[i * 3 + 0] = r;
                d8[i * 3 + 1] = g;
                d8[i * 3 + 2] = b;
                break;

            case BMP_PIXFMT_BGR888:
                d8[i * 3 + 0] = b;
                d8[i * 3 + 1] = g;
                d8[i * 3 + 2] = r;
                break;

            case BMP_PIXFMT_RGB565:
                d16[i] = ((uint16_t)(r >> 3) << 11) |
                          ((uint16_t)(g >> 2) << 5) |
                          (uint16_t)(b >> 3);
                break;

            case BMP_PIXFMT_ARGB1555:
                d16[i] = ((uint16_t)(a >= 128 ? 1 : 0) << 15) |
                          ((uint16_t)(r >> 3) << 10) |
                          ((uint16_t)(g >> 3) << 5) |
                          (uint16_t)(b >> 3);
                break;

            case BMP_PIXFMT_GRAYSCALE8: {
                /* ITU-R BT.601 luminance */
                uint32_t lum = (uint32_t)r * 299 +
                               (uint32_t)g * 587 +
                               (uint32_t)b * 114;
                d8[i] = (uint8_t)((lum + 500) / 1000);
                break;
            }

            default:
                return BMP_ERR_UNSUPPORTED_BPP;
        }
    }

    return BMP_OK;
}

void bmp_premultiply_alpha(uint32_t *pixels, uint32_t count)
{
    if (!pixels) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = pixels[i];
        uint32_t a = (pixel >> 24) & 0xFF;
        if (a == 0xFF) continue;
        if (a == 0x00) {
            pixels[i] = 0;
            continue;
        }
        uint32_t r = ((pixel >> 16) & 0xFF) * a / 255;
        uint32_t g = ((pixel >> 8) & 0xFF) * a / 255;
        uint32_t b = (pixel & 0xFF) * a / 255;
        pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

void bmp_unpremultiply_alpha(uint32_t *pixels, uint32_t count)
{
    if (!pixels) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = pixels[i];
        uint32_t a = (pixel >> 24) & 0xFF;
        if (a == 0xFF || a == 0x00) continue;
        uint32_t r = ((pixel >> 16) & 0xFF) * 255 / a;
        uint32_t g = ((pixel >> 8) & 0xFF) * 255 / a;
        uint32_t b = (pixel & 0xFF) * 255 / a;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

/* Image Transformation Functions */

int bmp_flip_vertical(BmpImage *img)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t w = img->width;
    uint32_t h = img->height;
    uint32_t half = h / 2;

    for (uint32_t y = 0; y < half; y++) {
        uint32_t *row_top = img->pixels + y * w;
        uint32_t *row_bot = img->pixels + (h - 1 - y) * w;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t tmp = row_top[x];
            row_top[x] = row_bot[x];
            row_bot[x] = tmp;
        }
    }

    return BMP_OK;
}

int bmp_flip_horizontal(BmpImage *img)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t w = img->width;
    uint32_t h = img->height;
    uint32_t half = w / 2;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t *row = img->pixels + y * w;
        for (uint32_t x = 0; x < half; x++) {
            uint32_t tmp = row[x];
            row[x] = row[w - 1 - x];
            row[w - 1 - x] = tmp;
        }
    }

    return BMP_OK;
}

int bmp_rotate_90_cw(BmpImage *img)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t old_w = img->width;
    uint32_t old_h = img->height;
    uint32_t new_w = old_h;
    uint32_t new_h = old_w;

    uint32_t buf_size;
    if (bmp_safe_mul(new_w * new_h, sizeof(uint32_t), &buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t *new_pixels = (uint32_t *)kmalloc(buf_size);
    if (!new_pixels) return BMP_ERR_OUT_OF_MEMORY;

    for (uint32_t y = 0; y < old_h; y++) {
        for (uint32_t x = 0; x < old_w; x++) {
            /* (x, y) in old -> (old_h - 1 - y, x) in new */
            uint32_t new_x = old_h - 1 - y;
            uint32_t new_y = x;
            new_pixels[new_y * new_w + new_x] = img->pixels[y * old_w + x];
        }
    }

    kfree(img->pixels);
    img->pixels = new_pixels;
    img->width = new_w;
    img->height = new_h;
    img->pixel_count = new_w * new_h;
    img->stride = new_w * sizeof(uint32_t);

    return BMP_OK;
}

int bmp_rotate_90_ccw(BmpImage *img)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t old_w = img->width;
    uint32_t old_h = img->height;
    uint32_t new_w = old_h;
    uint32_t new_h = old_w;

    uint32_t buf_size;
    if (bmp_safe_mul(new_w * new_h, sizeof(uint32_t), &buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t *new_pixels = (uint32_t *)kmalloc(buf_size);
    if (!new_pixels) return BMP_ERR_OUT_OF_MEMORY;

    for (uint32_t y = 0; y < old_h; y++) {
        for (uint32_t x = 0; x < old_w; x++) {
            /* (x, y) in old -> (y, old_w - 1 - x) in new */
            uint32_t new_x = y;
            uint32_t new_y = old_w - 1 - x;
            new_pixels[new_y * new_w + new_x] = img->pixels[y * old_w + x];
        }
    }

    kfree(img->pixels);
    img->pixels = new_pixels;
    img->width = new_w;
    img->height = new_h;
    img->pixel_count = new_w * new_h;
    img->stride = new_w * sizeof(uint32_t);

    return BMP_OK;
}

int bmp_rotate_180(BmpImage *img)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t count = img->pixel_count;
    uint32_t half = count / 2;

    for (uint32_t i = 0; i < half; i++) {
        uint32_t tmp = img->pixels[i];
        img->pixels[i] = img->pixels[count - 1 - i];
        img->pixels[count - 1 - i] = tmp;
    }

    return BMP_OK;
}

int bmp_crop(BmpImage *img, uint32_t x, uint32_t y,
             uint32_t w, uint32_t h)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;
    if (w == 0 || h == 0) return BMP_ERR_BAD_DIMENSIONS;
    if (x + w > img->width || y + h > img->height) {
        return BMP_ERR_BAD_DIMENSIONS;
    }

    uint32_t buf_size;
    if (bmp_safe_mul(w * h, sizeof(uint32_t), &buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t *new_pixels = (uint32_t *)kmalloc(buf_size);
    if (!new_pixels) return BMP_ERR_OUT_OF_MEMORY;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t *dst_row = new_pixels + row * w;
        uint32_t *src_row = img->pixels + (y + row) * img->width + x;
        bmp_memcpy(dst_row, src_row, w * sizeof(uint32_t));
    }

    kfree(img->pixels);
    img->pixels = new_pixels;
    img->width = w;
    img->height = h;
    img->pixel_count = w * h;
    img->stride = w * sizeof(uint32_t);

    return BMP_OK;
}

int bmp_scale_nearest(BmpImage *img, uint32_t new_w, uint32_t new_h)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;
    if (new_w == 0 || new_h == 0) return BMP_ERR_BAD_DIMENSIONS;
    if (new_w > BMP_MAX_DIMENSION || new_h > BMP_MAX_DIMENSION) {
        return BMP_ERR_DIMENSION_LIMIT;
    }

    uint32_t new_count;
    if (bmp_safe_mul(new_w, new_h, &new_count) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t buf_size;
    if (bmp_safe_mul(new_count, sizeof(uint32_t), &buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t *new_pixels = (uint32_t *)kmalloc(buf_size);
    if (!new_pixels) return BMP_ERR_OUT_OF_MEMORY;

    uint32_t old_w = img->width;
    uint32_t old_h = img->height;

    for (uint32_t y = 0; y < new_h; y++) {
        uint32_t src_y = y * old_h / new_h;
        if (src_y >= old_h) src_y = old_h - 1;

        for (uint32_t x = 0; x < new_w; x++) {
            uint32_t src_x = x * old_w / new_w;
            if (src_x >= old_w) src_x = old_w - 1;

            new_pixels[y * new_w + x] = img->pixels[src_y * old_w + src_x];
        }
    }

    kfree(img->pixels);
    img->pixels = new_pixels;
    img->width = new_w;
    img->height = new_h;
    img->pixel_count = new_count;
    img->stride = new_w * sizeof(uint32_t);

    return BMP_OK;
}

int bmp_scale_bilinear(BmpImage *img, uint32_t new_w, uint32_t new_h)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;
    if (new_w == 0 || new_h == 0) return BMP_ERR_BAD_DIMENSIONS;
    if (new_w > BMP_MAX_DIMENSION || new_h > BMP_MAX_DIMENSION) {
        return BMP_ERR_DIMENSION_LIMIT;
    }

    uint32_t new_count;
    if (bmp_safe_mul(new_w, new_h, &new_count) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t buf_size;
    if (bmp_safe_mul(new_count, sizeof(uint32_t), &buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t *new_pixels = (uint32_t *)kmalloc(buf_size);
    if (!new_pixels) return BMP_ERR_OUT_OF_MEMORY;

    uint32_t old_w = img->width;
    uint32_t old_h = img->height;

    /* Use fixed-point 16.16 arithmetic for bilinear interpolation */
    for (uint32_t y = 0; y < new_h; y++) {
        /* Map destination y to source y (fixed point) */
        uint32_t src_y_fp = (y * ((old_h - 1) << 16)) /
                            (new_h > 1 ? new_h - 1 : 1);
        uint32_t sy = src_y_fp >> 16;
        uint32_t fy = src_y_fp & 0xFFFF;
        uint32_t sy1 = sy + 1;
        if (sy1 >= old_h) sy1 = old_h - 1;

        for (uint32_t x = 0; x < new_w; x++) {
            uint32_t src_x_fp = (x * ((old_w - 1) << 16)) /
                                (new_w > 1 ? new_w - 1 : 1);
            uint32_t sx = src_x_fp >> 16;
            uint32_t fx = src_x_fp & 0xFFFF;
            uint32_t sx1 = sx + 1;
            if (sx1 >= old_w) sx1 = old_w - 1;

            /* Sample four surrounding pixels */
            uint32_t p00 = img->pixels[sy  * old_w + sx];
            uint32_t p10 = img->pixels[sy  * old_w + sx1];
            uint32_t p01 = img->pixels[sy1 * old_w + sx];
            uint32_t p11 = img->pixels[sy1 * old_w + sx1];

            /* Bilinear interpolation for each channel */
            uint32_t result = 0;
            for (int ch = 0; ch < 4; ch++) {
                uint32_t shift = ch * 8;
                uint32_t c00 = (p00 >> shift) & 0xFF;
                uint32_t c10 = (p10 >> shift) & 0xFF;
                uint32_t c01 = (p01 >> shift) & 0xFF;
                uint32_t c11 = (p11 >> shift) & 0xFF;

                /* Interpolate horizontally, then vertically */
                uint32_t top = (c00 * (0x10000 - fx) + c10 * fx) >> 16;
                uint32_t bot = (c01 * (0x10000 - fx) + c11 * fx) >> 16;
                uint32_t val = (top * (0x10000 - fy) + bot * fy) >> 16;

                if (val > 255) val = 255;
                result |= val << shift;
            }

            new_pixels[y * new_w + x] = result;
        }
    }

    kfree(img->pixels);
    img->pixels = new_pixels;
    img->width = new_w;
    img->height = new_h;
    img->pixel_count = new_count;
    img->stride = new_w * sizeof(uint32_t);

    return BMP_OK;
}

/* Color Manipulation Functions */

int bmp_grayscale(BmpImage *img)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t count = img->pixel_count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = img->pixels[i];
        uint32_t a = pixel & 0xFF000000;
        uint32_t r = (pixel >> 16) & 0xFF;
        uint32_t g = (pixel >> 8) & 0xFF;
        uint32_t b = pixel & 0xFF;

        /* ITU-R BT.601 luminance formula (integer arithmetic) */
        uint32_t lum = (r * 299 + g * 587 + b * 114 + 500) / 1000;
        if (lum > 255) lum = 255;

        img->pixels[i] = a | (lum << 16) | (lum << 8) | lum;
    }

    return BMP_OK;
}

int bmp_invert(BmpImage *img)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t count = img->pixel_count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = img->pixels[i];
        uint32_t a = pixel & 0xFF000000;
        img->pixels[i] = a | (~pixel & 0x00FFFFFF);
    }

    return BMP_OK;
}

int bmp_adjust_brightness(BmpImage *img, int32_t delta)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    delta = bmp_clamp_i32(delta, -255, 255);

    uint32_t count = img->pixel_count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = img->pixels[i];
        uint32_t a = pixel & 0xFF000000;
        int32_t r = ((pixel >> 16) & 0xFF) + delta;
        int32_t g = ((pixel >> 8) & 0xFF) + delta;
        int32_t b = (pixel & 0xFF) + delta;

        img->pixels[i] = a |
                          ((uint32_t)bmp_clamp_u8(r) << 16) |
                          ((uint32_t)bmp_clamp_u8(g) << 8) |
                          (uint32_t)bmp_clamp_u8(b);
    }

    return BMP_OK;
}

int bmp_tint(BmpImage *img, uint32_t color, uint8_t alpha)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;

    uint32_t tr = (color >> 16) & 0xFF;
    uint32_t tg = (color >> 8) & 0xFF;
    uint32_t tb = color & 0xFF;
    uint32_t a32 = (uint32_t)alpha;
    uint32_t inv_a = 255 - a32;

    uint32_t count = img->pixel_count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = img->pixels[i];
        uint32_t pa = pixel & 0xFF000000;
        uint32_t pr = (pixel >> 16) & 0xFF;
        uint32_t pg = (pixel >> 8) & 0xFF;
        uint32_t pb = pixel & 0xFF;

        uint32_t r = (pr * inv_a + tr * a32) / 255;
        uint32_t g = (pg * inv_a + tg * a32) / 255;
        uint32_t b = (pb * inv_a + tb * a32) / 255;

        img->pixels[i] = pa | (r << 16) | (g << 8) | b;
    }

    return BMP_OK;
}

uint32_t bmp_replace_color(BmpImage *img, uint32_t old_color,
                           uint32_t new_color)
{
    if (!img || !img->pixels) return 0;

    uint32_t replaced = 0;
    uint32_t count = img->pixel_count;
    for (uint32_t i = 0; i < count; i++) {
        if (img->pixels[i] == old_color) {
            img->pixels[i] = new_color;
            replaced++;
        }
    }

    return replaced;
}

int bmp_extract_channel(BmpImage *img, int channel)
{
    if (!img || !img->pixels) return BMP_ERR_NULL_INPUT;
    if (channel < 0 || channel > 3) return BMP_ERR_BAD_DIMENSIONS;

    uint32_t shift = (uint32_t)channel * 8;
    uint32_t count = img->pixel_count;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t val = (img->pixels[i] >> shift) & 0xFF;
        img->pixels[i] = 0xFF000000 | (val << 16) | (val << 8) | val;
    }

    return BMP_OK;
}

/* Utility / Query Functions */

uint32_t bmp_get_pixel(const BmpImage *img, uint32_t x, uint32_t y)
{
    if (!img || !img->pixels) return 0;
    if (x >= img->width || y >= img->height) return 0;
    return img->pixels[y * img->width + x];
}

void bmp_set_pixel(BmpImage *img, uint32_t x, uint32_t y, uint32_t color)
{
    if (!img || !img->pixels) return;
    if (x >= img->width || y >= img->height) return;
    img->pixels[y * img->width + x] = color;
}

void bmp_fill(BmpImage *img, uint32_t color)
{
    if (!img || !img->pixels) return;

    uint32_t count = img->pixel_count;

    /* Optimize for common fill colors */
    if (color == 0x00000000) {
        bmp_memset(img->pixels, 0, count * sizeof(uint32_t));
        return;
    }
    if (color == 0xFFFFFFFF) {
        bmp_memset(img->pixels, 0xFF, count * sizeof(uint32_t));
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        img->pixels[i] = color;
    }
}

void bmp_fill_rect(BmpImage *img, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color)
{
    if (!img || !img->pixels) return;

    /* Clip rectangle to image bounds */
    if (x >= img->width || y >= img->height) return;
    if (x + w > img->width) w = img->width - x;
    if (y + h > img->height) h = img->height - y;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t *dst = img->pixels + (y + row) * img->width + x;
        for (uint32_t col = 0; col < w; col++) {
            dst[col] = color;
        }
    }
}

int bmp_blit(BmpImage *dst, uint32_t dst_x, uint32_t dst_y,
             const BmpImage *src, uint32_t src_x, uint32_t src_y,
             uint32_t w, uint32_t h)
{
    if (!dst || !dst->pixels || !src || !src->pixels) {
        return BMP_ERR_NULL_INPUT;
    }

    /* Clip source */
    if (src_x >= src->width || src_y >= src->height) return BMP_OK;
    if (src_x + w > src->width) w = src->width - src_x;
    if (src_y + h > src->height) h = src->height - src_y;

    /* Clip destination */
    if (dst_x >= dst->width || dst_y >= dst->height) return BMP_OK;
    if (dst_x + w > dst->width) w = dst->width - dst_x;
    if (dst_y + h > dst->height) h = dst->height - dst_y;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t *d = dst->pixels + (dst_y + row) * dst->width + dst_x;
        const uint32_t *s = src->pixels + (src_y + row) * src->width + src_x;
        bmp_memcpy(d, s, w * sizeof(uint32_t));
    }

    return BMP_OK;
}

int bmp_blend(BmpImage *dst, uint32_t dst_x, uint32_t dst_y,
              const BmpImage *src, uint32_t src_x, uint32_t src_y,
              uint32_t w, uint32_t h)
{
    if (!dst || !dst->pixels || !src || !src->pixels) {
        return BMP_ERR_NULL_INPUT;
    }

    /* Clip source */
    if (src_x >= src->width || src_y >= src->height) return BMP_OK;
    if (src_x + w > src->width) w = src->width - src_x;
    if (src_y + h > src->height) h = src->height - src_y;

    /* Clip destination */
    if (dst_x >= dst->width || dst_y >= dst->height) return BMP_OK;
    if (dst_x + w > dst->width) w = dst->width - dst_x;
    if (dst_y + h > dst->height) h = dst->height - dst_y;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t *d = dst->pixels + (dst_y + row) * dst->width + dst_x;
        const uint32_t *s = src->pixels + (src_y + row) * src->width + src_x;

        for (uint32_t col = 0; col < w; col++) {
            uint32_t sp = s[col];
            uint32_t sa = (sp >> 24) & 0xFF;

            if (sa == 0xFF) {
                /* Fully opaque: just copy */
                d[col] = sp;
            } else if (sa == 0x00) {
                /* Fully transparent: skip */
            } else {
                /* Alpha blend: out = src * sa + dst * (1 - sa) */
                uint32_t dp = d[col];
                uint32_t inv_sa = 255 - sa;

                uint32_t sr = (sp >> 16) & 0xFF;
                uint32_t sg = (sp >> 8) & 0xFF;
                uint32_t sb = sp & 0xFF;

                uint32_t dr = (dp >> 16) & 0xFF;
                uint32_t dg = (dp >> 8) & 0xFF;
                uint32_t db = dp & 0xFF;
                uint32_t da = (dp >> 24) & 0xFF;

                uint32_t or_val = (sr * sa + dr * inv_sa + 127) / 255;
                uint32_t og = (sg * sa + dg * inv_sa + 127) / 255;
                uint32_t ob = (sb * sa + db * inv_sa + 127) / 255;
                /* Output alpha: sa + da * (1 - sa) */
                uint32_t oa = sa + (da * inv_sa + 127) / 255;
                if (oa > 255) oa = 255;

                d[col] = (oa << 24) | (or_val << 16) | (og << 8) | ob;
            }
        }
    }

    return BMP_OK;
}

int bmp_clone(BmpImage *dst, const BmpImage *src)
{
    if (!dst || !src) return BMP_ERR_NULL_INPUT;

    bmp_memset(dst, 0, sizeof(*dst));

    if (!src->pixels || src->pixel_count == 0) {
        return BMP_OK;
    }

    uint32_t buf_size;
    if (bmp_safe_mul(src->pixel_count, sizeof(uint32_t), &buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t *pixels = (uint32_t *)kmalloc(buf_size);
    if (!pixels) return BMP_ERR_OUT_OF_MEMORY;

    bmp_memcpy(pixels, src->pixels, buf_size);

    /* Copy all metadata */
    bmp_memcpy(dst, src, sizeof(*dst));
    dst->pixels = pixels;

    /* Clone ICC profile if present */
    dst->icc_profile = (uint8_t *)0;
    dst->icc_profile_size = 0;
    if (src->icc_profile && src->icc_profile_size > 0) {
        uint8_t *icc = (uint8_t *)kmalloc(src->icc_profile_size);
        if (icc) {
            bmp_memcpy(icc, src->icc_profile, src->icc_profile_size);
            dst->icc_profile = icc;
            dst->icc_profile_size = src->icc_profile_size;
        }
    }

    return BMP_OK;
}

int bmp_create(BmpImage *img, uint32_t width, uint32_t height)
{
    if (!img) return BMP_ERR_NULL_INPUT;
    if (width == 0 || height == 0) return BMP_ERR_BAD_DIMENSIONS;
    if (width > BMP_MAX_DIMENSION || height > BMP_MAX_DIMENSION) {
        return BMP_ERR_DIMENSION_LIMIT;
    }

    bmp_memset(img, 0, sizeof(*img));

    uint32_t count;
    if (bmp_safe_mul(width, height, &count) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t buf_size;
    if (bmp_safe_mul(count, sizeof(uint32_t), &buf_size) != 0) {
        return BMP_ERR_OVERFLOW;
    }

    uint32_t *pixels = (uint32_t *)kmalloc(buf_size);
    if (!pixels) return BMP_ERR_OUT_OF_MEMORY;

    bmp_memset(pixels, 0, buf_size);

    img->width = width;
    img->height = height;
    img->pixels = pixels;
    img->pixel_count = count;
    img->stride = width * sizeof(uint32_t);
    img->original_bpp = 32;

    return BMP_OK;
}

/* Error / Info String Functions */

const char *bmp_error_string(int error_code)
{
    switch (error_code) {
        case BMP_OK:                  return "Success";
        case BMP_ERR_NULL_INPUT:      return "Null input pointer";
        case BMP_ERR_FILE_TOO_SMALL:  return "File too small to be a BMP";
        case BMP_ERR_BAD_SIGNATURE:   return "Invalid BMP signature (not 'BM')";
        case BMP_ERR_BAD_HEADER_SIZE: return "Unsupported BMP header size";
        case BMP_ERR_BAD_DIMENSIONS:  return "Invalid image dimensions";
        case BMP_ERR_UNSUPPORTED_COMP:return "Unsupported compression type";
        case BMP_ERR_UNSUPPORTED_BPP: return "Unsupported bits per pixel";
        case BMP_ERR_BAD_DATA_OFFSET: return "Invalid pixel data offset";
        case BMP_ERR_FILE_TRUNCATED:  return "File data is truncated";
        case BMP_ERR_OUT_OF_MEMORY:   return "Out of memory";
        case BMP_ERR_BAD_PALETTE:     return "Invalid or corrupt palette";
        case BMP_ERR_BAD_RLE_DATA:    return "Corrupt RLE compressed data";
        case BMP_ERR_BAD_BITFIELDS:   return "Invalid bitfield masks";
        case BMP_ERR_OVERFLOW:        return "Integer overflow in calculation";
        case BMP_ERR_BAD_PLANES:      return "Invalid plane count (must be 1)";
        case BMP_ERR_CORRUPT_DATA:    return "Corrupt image data";
        case BMP_ERR_UNSUPPORTED_OS2: return "Unsupported OS/2 BMP variant";
        case BMP_ERR_BAD_COLOR_SPACE: return "Invalid color space information";
        case BMP_ERR_BAD_ICC_PROFILE: return "Invalid ICC profile data";
        case BMP_ERR_DIMENSION_LIMIT: return "Image dimensions exceed limits";
        default:                      return "Unknown error";
    }
}

const char *bmp_compression_name(uint32_t compression)
{
    switch (compression) {
        case BMP_COMPRESS_NONE:       return "Uncompressed (BI_RGB)";
        case BMP_COMPRESS_RLE8:       return "RLE 8-bit (BI_RLE8)";
        case BMP_COMPRESS_RLE4:       return "RLE 4-bit (BI_RLE4)";
        case BMP_COMPRESS_BITFIELDS:  return "Bitfields (BI_BITFIELDS)";
        case BMP_COMPRESS_JPEG:       return "JPEG (BI_JPEG)";
        case BMP_COMPRESS_PNG:        return "PNG (BI_PNG)";
        case BMP_COMPRESS_ALPHABITS:  return "Alpha bitfields (BI_ALPHABITFIELDS)";
        case BMP_COMPRESS_CMYK:       return "CMYK uncompressed";
        case BMP_COMPRESS_CMYK_RLE8:  return "CMYK RLE-8";
        case BMP_COMPRESS_CMYK_RLE4:  return "CMYK RLE-4";
        default:                      return "Unknown compression";
    }
}

const char *bmp_header_name(uint32_t header_size)
{
    switch (header_size) {
        case BMP_HEADER_CORE:  return "BITMAPCOREHEADER (OS/2 1.x)";
        case BMP_HEADER_INFO:  return "BITMAPINFOHEADER";
        case BMP_HEADER_V2:    return "BITMAPV2INFOHEADER (undocumented)";
        case BMP_HEADER_V3:    return "BITMAPV3INFOHEADER (undocumented)";
        case BMP_HEADER_OS2_V2:return "OS/2 2.x BITMAPINFOHEADER2";
        case BMP_HEADER_V4:    return "BITMAPV4HEADER";
        case BMP_HEADER_V5:    return "BITMAPV5HEADER";
        default:               return "Unknown header type";
    }
}