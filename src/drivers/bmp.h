#ifndef BMP_H
#define BMP_H

#include <stdint.h>

/* ============================================================================
 * BMP Decoder Library for Custom Kernel OS
 * ============================================================================
 * Full-featured BMP file decoder supporting:
 *   - 1-bit monochrome
 *   - 4-bit indexed (with and without RLE4 compression)
 *   - 8-bit indexed (with and without RLE8 compression)
 *   - 16-bit (RGB555, RGB565 with bitfield masks)
 *   - 24-bit uncompressed
 *   - 32-bit (with and without bitfield masks, alpha channel)
 *   - BITMAPINFOHEADER (40 bytes)
 *   - BITMAPV4HEADER (108 bytes)
 *   - BITMAPV5HEADER (124 bytes)
 *   - BITMAPCOREHEADER / OS/2 1.x (12 bytes)
 *   - Top-down and bottom-up orientations
 *   - Color table / palette decoding
 *   - Bitfield mask decoding for 16-bit and 32-bit
 *   - Image validation and integrity checks
 *   - Pixel format conversion utilities
 *   - Image transformation (flip, rotate, crop, scale)
 *   - Color space information parsing
 *   - ICC profile data extraction
 * ============================================================================ */

/* --------------------------------------------------------------------------
 * Error codes
 * -------------------------------------------------------------------------- */
#define BMP_OK                    0
#define BMP_ERR_NULL_INPUT       -1
#define BMP_ERR_FILE_TOO_SMALL   -2
#define BMP_ERR_BAD_SIGNATURE    -3
#define BMP_ERR_BAD_HEADER_SIZE  -4
#define BMP_ERR_BAD_DIMENSIONS   -5
#define BMP_ERR_UNSUPPORTED_COMP -6
#define BMP_ERR_UNSUPPORTED_BPP  -7
#define BMP_ERR_BAD_DATA_OFFSET  -8
#define BMP_ERR_FILE_TRUNCATED   -9
#define BMP_ERR_OUT_OF_MEMORY    -10
#define BMP_ERR_BAD_PALETTE      -11
#define BMP_ERR_BAD_RLE_DATA     -12
#define BMP_ERR_BAD_BITFIELDS    -13
#define BMP_ERR_OVERFLOW         -14
#define BMP_ERR_BAD_PLANES       -15
#define BMP_ERR_CORRUPT_DATA     -16
#define BMP_ERR_UNSUPPORTED_OS2  -17
#define BMP_ERR_BAD_COLOR_SPACE  -18
#define BMP_ERR_BAD_ICC_PROFILE  -19
#define BMP_ERR_DIMENSION_LIMIT  -20

/* --------------------------------------------------------------------------
 * Compression types
 * -------------------------------------------------------------------------- */
#define BMP_COMPRESS_NONE         0
#define BMP_COMPRESS_RLE8         1
#define BMP_COMPRESS_RLE4         2
#define BMP_COMPRESS_BITFIELDS    3
#define BMP_COMPRESS_JPEG         4
#define BMP_COMPRESS_PNG          5
#define BMP_COMPRESS_ALPHABITS    6
#define BMP_COMPRESS_CMYK         11
#define BMP_COMPRESS_CMYK_RLE8    12
#define BMP_COMPRESS_CMYK_RLE4    13

/* --------------------------------------------------------------------------
 * Header type identifiers
 * -------------------------------------------------------------------------- */
#define BMP_HEADER_CORE          12   /* BITMAPCOREHEADER (OS/2 1.x) */
#define BMP_HEADER_INFO          40   /* BITMAPINFOHEADER */
#define BMP_HEADER_V2            52   /* Undocumented V2 */
#define BMP_HEADER_V3            56   /* Undocumented V3 */
#define BMP_HEADER_V4           108   /* BITMAPV4HEADER */
#define BMP_HEADER_V5           124   /* BITMAPV5HEADER */
#define BMP_HEADER_OS2_V2        64   /* OS/2 2.x BITMAPINFOHEADER2 */

/* --------------------------------------------------------------------------
 * Pixel format identifiers
 * -------------------------------------------------------------------------- */
#define BMP_PIXFMT_ARGB8888       0   /* 32-bit ARGB (default output) */
#define BMP_PIXFMT_RGBA8888       1   /* 32-bit RGBA */
#define BMP_PIXFMT_ABGR8888       2   /* 32-bit ABGR */
#define BMP_PIXFMT_RGB888         3   /* 24-bit RGB */
#define BMP_PIXFMT_BGR888         4   /* 24-bit BGR */
#define BMP_PIXFMT_RGB565         5   /* 16-bit RGB565 */
#define BMP_PIXFMT_ARGB1555       6   /* 16-bit ARGB1555 */
#define BMP_PIXFMT_GRAYSCALE8     7   /* 8-bit grayscale */

/* --------------------------------------------------------------------------
 * Color space types (from V4/V5 headers)
 * -------------------------------------------------------------------------- */
#define BMP_CS_CALIBRATED_RGB     0x00000000
#define BMP_CS_SRGB               0x73524742  /* 'sRGB' */
#define BMP_CS_WINDOWS            0x57696E20  /* 'Win ' */
#define BMP_CS_PROFILE_LINKED     0x4C494E4B  /* 'LINK' */
#define BMP_CS_PROFILE_EMBEDDED   0x4D424544  /* 'MBED' */

/* --------------------------------------------------------------------------
 * Rendering intent (from V5 header)
 * -------------------------------------------------------------------------- */
#define BMP_INTENT_BUSINESS       1   /* Saturation - LCS_GM_BUSINESS */
#define BMP_INTENT_GRAPHICS       2   /* Relative colorimetric */
#define BMP_INTENT_IMAGES         4   /* Perceptual */
#define BMP_INTENT_ABS_COLORIM    8   /* Absolute colorimetric */

/* --------------------------------------------------------------------------
 * Maximum dimension limit to prevent overflow attacks
 * -------------------------------------------------------------------------- */
#define BMP_MAX_DIMENSION         16384
#define BMP_MAX_PIXEL_COUNT       (BMP_MAX_DIMENSION * BMP_MAX_DIMENSION)
#define BMP_MAX_PALETTE_SIZE      256

/* --------------------------------------------------------------------------
 * CIEXYZ triple structure for color space endpoints
 * -------------------------------------------------------------------------- */
typedef struct {
    int32_t x;   /* FXPT2DOT30 fixed-point */
    int32_t y;
    int32_t z;
} BmpCieXyz;

typedef struct {
    BmpCieXyz red;
    BmpCieXyz green;
    BmpCieXyz blue;
} BmpCieXyzTriple;

/* --------------------------------------------------------------------------
 * Color table entry
 * -------------------------------------------------------------------------- */
typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
} BmpColorEntry;

/* --------------------------------------------------------------------------
 * Bitfield mask descriptor
 * -------------------------------------------------------------------------- */
typedef struct {
    uint32_t mask;
    uint32_t shift;
    uint32_t bits;
    uint32_t max_val;
} BmpBitfield;

/* --------------------------------------------------------------------------
 * Parsed BMP header information (all header versions unified)
 * -------------------------------------------------------------------------- */
typedef struct {
    /* File header fields */
    uint16_t signature;
    uint32_t file_size;
    uint32_t data_offset;

    /* Info header fields */
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_ppm;             /* X pixels per meter */
    int32_t  y_ppm;             /* Y pixels per meter */
    uint32_t colors_used;
    uint32_t colors_important;

    /* Bitfield masks (V2/V3/V4/V5 or BI_BITFIELDS) */
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t alpha_mask;

    /* V4 header fields */
    uint32_t cs_type;           /* Color space type */
    BmpCieXyzTriple endpoints;  /* Color space endpoints */
    uint32_t gamma_red;         /* Red gamma (FXPT16DOT16) */
    uint32_t gamma_green;
    uint32_t gamma_blue;

    /* V5 header fields */
    uint32_t intent;            /* Rendering intent */
    uint32_t profile_data;      /* ICC profile data offset */
    uint32_t profile_size;      /* ICC profile data size */

    /* Derived fields */
    int      is_bottom_up;      /* Non-zero if bottom-up orientation */
    int      is_core_header;    /* Non-zero if OS/2 BITMAPCOREHEADER */
    int      has_bitfields;     /* Non-zero if bitfield masks present */
    int      has_alpha_channel; /* Non-zero if alpha channel present */
    uint32_t abs_width;
    uint32_t abs_height;
    uint32_t row_stride;        /* Bytes per row including padding */
    uint32_t palette_count;     /* Actual number of palette entries */
    uint32_t palette_entry_size;/* Bytes per palette entry (3 or 4) */
} BmpHeader;

/* --------------------------------------------------------------------------
 * Decoded BMP image structure
 * -------------------------------------------------------------------------- */
typedef struct {
    uint32_t  width;            /* Image width in pixels */
    uint32_t  height;           /* Image height in pixels */
    uint32_t *pixels;           /* ARGB pixel data (width * height) */
    uint32_t  pixel_count;      /* Total pixel count */
    uint32_t  stride;           /* Output row stride in bytes */

    /* Metadata */
    uint16_t  original_bpp;     /* Original bits per pixel */
    uint32_t  original_comp;    /* Original compression type */
    int32_t   dpi_x;            /* Horizontal DPI (0 if not specified) */
    int32_t   dpi_y;            /* Vertical DPI (0 if not specified) */
    uint32_t  header_version;   /* Header size/version used */
    uint32_t  color_space;      /* Color space type from header */

    /* Palette (if indexed image) */
    BmpColorEntry palette[256]; /* Color table entries */
    uint32_t  palette_count;    /* Number of valid palette entries */

    /* ICC Profile (if present) */
    uint8_t  *icc_profile;     /* ICC profile data (NULL if none) */
    uint32_t  icc_profile_size;/* ICC profile data size */

    /* Bitfield information */
    BmpBitfield bf_red;
    BmpBitfield bf_green;
    BmpBitfield bf_blue;
    BmpBitfield bf_alpha;
} BmpImage;

/* --------------------------------------------------------------------------
 * Decode options / configuration
 * -------------------------------------------------------------------------- */
typedef struct {
    int      force_alpha_opaque;  /* Force alpha to 0xFF for non-alpha BMPs */
    int      preserve_palette;    /* Keep palette data in output */
    int      extract_icc;         /* Extract ICC profile data */
    uint32_t max_width;           /* Override max width limit (0 = default) */
    uint32_t max_height;          /* Override max height limit (0 = default) */
    int      output_format;       /* Output pixel format (BMP_PIXFMT_*) */
    int      premultiply_alpha;   /* Premultiply alpha in output */
} BmpDecodeOptions;

/* Core API Functions */

/**
 * Decode a BMP file from raw bytes into an image structure.
 * Supports all standard BMP formats and compression types.
 *
 * @param data      Pointer to raw BMP file data
 * @param data_len  Length of the raw data in bytes
 * @param img       Output image structure (caller-allocated)
 * @return BMP_OK on success, negative error code on failure
 *
 * Call bmp_free() when done with the image.
 */
int bmp_decode(const uint8_t *data, uint32_t data_len, BmpImage *img);

/**
 * Decode a BMP file with custom options.
 *
 * @param data      Pointer to raw BMP file data
 * @param data_len  Length of the raw data in bytes
 * @param img       Output image structure (caller-allocated)
 * @param opts      Decode options (NULL for defaults)
 * @return BMP_OK on success, negative error code on failure
 */
int bmp_decode_ex(const uint8_t *data, uint32_t data_len,
                  BmpImage *img, const BmpDecodeOptions *opts);

/**
 * Parse BMP headers without decoding pixel data.
 * Useful for querying image dimensions and format.
 *
 * @param data      Pointer to raw BMP file data
 * @param data_len  Length of the raw data in bytes
 * @param header    Output header structure
 * @return BMP_OK on success, negative error code on failure
 */
int bmp_parse_header(const uint8_t *data, uint32_t data_len,
                     BmpHeader *header);

/**
 * Validate BMP file data without decoding.
 *
 * @param data      Pointer to raw BMP file data
 * @param data_len  Length of the raw data in bytes
 * @return BMP_OK if valid, negative error code describing the issue
 */
int bmp_validate(const uint8_t *data, uint32_t data_len);

/**
 * Free decoded BMP image memory.
 *
 * @param img  Pointer to BmpImage structure to free
 */
void bmp_free(BmpImage *img);

/**
 * Initialize decode options with default values.
 *
 * @param opts  Pointer to options structure to initialize
 */
void bmp_default_options(BmpDecodeOptions *opts);

/* Pixel Format Conversion Functions */

/**
 * Convert ARGB pixel buffer to a different pixel format.
 *
 * @param src        Source ARGB pixel buffer
 * @param dst        Destination buffer (must be pre-allocated)
 * @param count      Number of pixels to convert
 * @param dst_fmt    Destination pixel format (BMP_PIXFMT_*)
 * @return BMP_OK on success, negative error code on failure
 */
int bmp_convert_pixels(const uint32_t *src, void *dst,
                       uint32_t count, int dst_fmt);

/**
 * Get the number of bytes per pixel for a given format.
 *
 * @param fmt  Pixel format identifier
 * @return Bytes per pixel, or 0 if invalid format
 */
uint32_t bmp_bytes_per_pixel(int fmt);

/**
 * Premultiply alpha in an ARGB pixel buffer (in-place).
 *
 * @param pixels  ARGB pixel buffer
 * @param count   Number of pixels
 */
void bmp_premultiply_alpha(uint32_t *pixels, uint32_t count);

/**
 * Unpremultiply alpha in an ARGB pixel buffer (in-place).
 *
 * @param pixels  ARGB pixel buffer
 * @param count   Number of pixels
 */
void bmp_unpremultiply_alpha(uint32_t *pixels, uint32_t count);

/* Image Transformation Functions */

/**
 * Flip image vertically (in-place).
 *
 * @param img  Pointer to decoded BMP image
 * @return BMP_OK on success
 */
int bmp_flip_vertical(BmpImage *img);

/**
 * Flip image horizontally (in-place).
 *
 * @param img  Pointer to decoded BMP image
 * @return BMP_OK on success
 */
int bmp_flip_horizontal(BmpImage *img);

/**
 * Rotate image 90 degrees clockwise.
 * Creates a new pixel buffer; old one is freed.
 *
 * @param img  Pointer to decoded BMP image
 * @return BMP_OK on success
 */
int bmp_rotate_90_cw(BmpImage *img);

/**
 * Rotate image 90 degrees counter-clockwise.
 *
 * @param img  Pointer to decoded BMP image
 * @return BMP_OK on success
 */
int bmp_rotate_90_ccw(BmpImage *img);

/**
 * Rotate image 180 degrees (in-place).
 *
 * @param img  Pointer to decoded BMP image
 * @return BMP_OK on success
 */
int bmp_rotate_180(BmpImage *img);

/**
 * Crop a rectangular region from the image.
 * Creates a new pixel buffer; old one is freed.
 *
 * @param img  Pointer to decoded BMP image
 * @param x    Left edge of crop rectangle
 * @param y    Top edge of crop rectangle
 * @param w    Width of crop rectangle
 * @param h    Height of crop rectangle
 * @return BMP_OK on success
 */
int bmp_crop(BmpImage *img, uint32_t x, uint32_t y,
             uint32_t w, uint32_t h);

/**
 * Scale image using nearest-neighbor interpolation.
 * Creates a new pixel buffer; old one is freed.
 *
 * @param img       Pointer to decoded BMP image
 * @param new_w     New width
 * @param new_h     New height
 * @return BMP_OK on success
 */
int bmp_scale_nearest(BmpImage *img, uint32_t new_w, uint32_t new_h);

/**
 * Scale image using bilinear interpolation.
 * Creates a new pixel buffer; old one is freed.
 *
 * @param img       Pointer to decoded BMP image
 * @param new_w     New width
 * @param new_h     New height
 * @return BMP_OK on success
 */
int bmp_scale_bilinear(BmpImage *img, uint32_t new_w, uint32_t new_h);

/* Color Manipulation Functions */

/**
 * Convert image to grayscale (in-place).
 * Uses luminance formula: Y = 0.299R + 0.587G + 0.114B
 *
 * @param img  Pointer to decoded BMP image
 * @return BMP_OK on success
 */
int bmp_grayscale(BmpImage *img);

/**
 * Invert image colors (in-place). Alpha is preserved.
 *
 * @param img  Pointer to decoded BMP image
 * @return BMP_OK on success
 */
int bmp_invert(BmpImage *img);

/**
 * Adjust image brightness (in-place).
 *
 * @param img     Pointer to decoded BMP image
 * @param delta   Brightness adjustment (-255 to 255)
 * @return BMP_OK on success
 */
int bmp_adjust_brightness(BmpImage *img, int32_t delta);

/**
 * Apply a tint color to the image (in-place).
 * Blends the tint with each pixel using the specified alpha.
 *
 * @param img     Pointer to decoded BMP image
 * @param color   ARGB tint color
 * @param alpha   Blend factor (0-255)
 * @return BMP_OK on success
 */
int bmp_tint(BmpImage *img, uint32_t color, uint8_t alpha);

/**
 * Replace one color with another throughout the image.
 *
 * @param img       Pointer to decoded BMP image
 * @param old_color Color to replace (ARGB)
 * @param new_color Replacement color (ARGB)
 * @return Number of pixels replaced
 */
uint32_t bmp_replace_color(BmpImage *img, uint32_t old_color,
                           uint32_t new_color);

/**
 * Extract a single color channel as a grayscale image.
 *
 * @param img      Pointer to decoded BMP image
 * @param channel  0=blue, 1=green, 2=red, 3=alpha
 * @return BMP_OK on success
 */
int bmp_extract_channel(BmpImage *img, int channel);

/* Utility / Query Functions */

/**
 * Get a single pixel value from the image.
 *
 * @param img  Pointer to decoded BMP image
 * @param x    X coordinate
 * @param y    Y coordinate
 * @return ARGB pixel value, or 0 if coordinates out of bounds
 */
uint32_t bmp_get_pixel(const BmpImage *img, uint32_t x, uint32_t y);

/**
 * Set a single pixel value in the image.
 *
 * @param img    Pointer to decoded BMP image
 * @param x      X coordinate
 * @param y      Y coordinate
 * @param color  ARGB pixel value
 */
void bmp_set_pixel(BmpImage *img, uint32_t x, uint32_t y, uint32_t color);

/**
 * Fill entire image with a solid color.
 *
 * @param img    Pointer to decoded BMP image
 * @param color  ARGB fill color
 */
void bmp_fill(BmpImage *img, uint32_t color);

/**
 * Fill a rectangular region with a solid color.
 *
 * @param img    Pointer to decoded BMP image
 * @param x      Left edge
 * @param y      Top edge
 * @param w      Width
 * @param h      Height
 * @param color  ARGB fill color
 */
void bmp_fill_rect(BmpImage *img, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color);

/**
 * Copy a rectangular region from one image to another.
 *
 * @param dst     Destination image
 * @param dst_x   Destination X coordinate
 * @param dst_y   Destination Y coordinate
 * @param src     Source image
 * @param src_x   Source X coordinate
 * @param src_y   Source Y coordinate
 * @param w       Width of region
 * @param h       Height of region
 * @return BMP_OK on success
 */
int bmp_blit(BmpImage *dst, uint32_t dst_x, uint32_t dst_y,
             const BmpImage *src, uint32_t src_x, uint32_t src_y,
             uint32_t w, uint32_t h);

/**
 * Alpha-blend a source image onto a destination image.
 *
 * @param dst     Destination image
 * @param dst_x   Destination X coordinate
 * @param dst_y   Destination Y coordinate
 * @param src     Source image (alpha channel used for blending)
 * @param src_x   Source X coordinate
 * @param src_y   Source Y coordinate
 * @param w       Width of region
 * @param h       Height of region
 * @return BMP_OK on success
 */
int bmp_blend(BmpImage *dst, uint32_t dst_x, uint32_t dst_y,
              const BmpImage *src, uint32_t src_x, uint32_t src_y,
              uint32_t w, uint32_t h);

/**
 * Create a deep copy of a BMP image.
 *
 * @param dst  Destination image (will be allocated)
 * @param src  Source image to copy
 * @return BMP_OK on success
 */
int bmp_clone(BmpImage *dst, const BmpImage *src);

/**
 * Create an empty image with the given dimensions.
 * All pixels are initialized to transparent black (0x00000000).
 *
 * @param img    Output image structure
 * @param width  Image width
 * @param height Image height
 * @return BMP_OK on success
 */
int bmp_create(BmpImage *img, uint32_t width, uint32_t height);

/**
 * Get human-readable error string for an error code.
 *
 * @param error_code  Error code returned by bmp_* functions
 * @return Static string describing the error
 */
const char *bmp_error_string(int error_code);

/**
 * Get human-readable compression type name.
 *
 * @param compression  Compression type value
 * @return Static string describing the compression type
 */
const char *bmp_compression_name(uint32_t compression);

/**
 * Get human-readable header version name.
 *
 * @param header_size  Header size value
 * @return Static string describing the header version
 */
const char *bmp_header_name(uint32_t header_size);

#endif /* BMP_H */