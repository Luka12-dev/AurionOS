/*
 * Embedded Desktop Icons for Aurion OS
 * Pre-rendered 32x32 icon bitmaps in ARGB format.
 * Each icon is a 32x32 pixel array stored as a flat uint32_t buffer.
 *
 * Since we cannot load PNG from the host at runtime in a bare-metal OS,
 * these icons are drawn programmatically with clean pixel art.
*/

#include <stdint.h>
#include <stdbool.h>

extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern void gpu_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg);

/* Icon size */
#define ICON_SIZE 32

/* Icon drawing routines */
/* Each draws a 32x32 icon at (x,y) on screen */

static void draw_rounded_rect(int x, int y, int w, int h, uint32_t fill, uint32_t border) {
    gpu_fill_rect(x + 2, y, w - 4, h, fill);
    gpu_fill_rect(x, y + 2, w, h - 4, fill);
    gpu_fill_rect(x + 1, y + 1, w - 2, h - 2, fill);
    /* Border */
    gpu_fill_rect(x + 2, y, w - 4, 1, border);
    gpu_fill_rect(x + 2, y + h - 1, w - 4, 1, border);
    gpu_fill_rect(x, y + 2, 1, h - 4, border);
    gpu_fill_rect(x + w - 1, y + 2, 1, h - 4, border);
}

/* Terminal icon: dark rectangle with green "> _" prompt */
void icon_draw_terminal(int x, int y) {
    draw_rounded_rect(x, y, 32, 32, 0xFF000000, 0xFF707070);
    /* Title bar */
    gpu_fill_rect(x + 2, y + 1, 28, 6, 0xFF333333);
    /* Dots */
    gpu_fill_rect(x + 4, y + 3, 2, 2, 0xFFE04050);
    gpu_fill_rect(x + 8, y + 3, 2, 2, 0xFFFFBB33);
    gpu_fill_rect(x + 12, y + 3, 2, 2, 0xFF4ADE80);
    /* Prompt text - bg must match the body color, not 0 (black) */
    gpu_draw_char(x + 4, y + 12, '>', 0xFF4ADE80, 0xFF000000);
    gpu_draw_char(x + 14, y + 12, '_', 0xFF4ADE80, 0xFF000000);
    gpu_draw_char(x + 4, y + 22, '$', 0xFF808090, 0xFF000000);
}

/* Notepad icon: white page with blue lines */
void icon_draw_notepad(int x, int y) {
    /* Page shadow */
    gpu_fill_rect(x + 4, y + 3, 24, 28, 0xFF505050);
    /* Page body */
    gpu_fill_rect(x + 2, y + 1, 24, 28, 0xFFF8F8FF);
    /* Fold corner */
    gpu_fill_rect(x + 20, y + 1, 6, 6, 0xFFD0D0E0);
    gpu_fill_rect(x + 20, y + 1, 1, 6, 0xFFB0B0C0);
    gpu_fill_rect(x + 20, y + 6, 6, 1, 0xFFB0B0C0);
    /* Lines */
    gpu_fill_rect(x + 6, y + 10, 16, 1, 0xFF707070);
    gpu_fill_rect(x + 6, y + 14, 14, 1, 0xFF707070);
    gpu_fill_rect(x + 6, y + 18, 16, 1, 0xFF707070);
    gpu_fill_rect(x + 6, y + 22, 12, 1, 0xFF707070);
    /* Pencil accent */
    gpu_fill_rect(x + 24, y + 20, 2, 8, 0xFFFFBB33);
    gpu_fill_rect(x + 24, y + 28, 2, 2, 0xFF333333);
}

/* Paint icon: AurionOS logo loaded from BMP */
void icon_draw_paint(int x, int y) {
    static uint8_t logo_data[32768];
    static int logo_loaded = 0;
    static int logo_w = 0, logo_h = 0;
    static uint32_t logo_pixels[32 * 32];
    
    extern int load_file_content(const char *filename, char *buffer, int max_len);
    
    if (!logo_loaded) {
        logo_loaded = 1;
        
        /* Try to load the AurionOS logo BMP */
        int bytes = load_file_content("/icons/AurionOS-logo/AurionOS.bmp", 
                                      (char *)logo_data, sizeof(logo_data));
        
        if (bytes > 54 && logo_data[0] == 'B' && logo_data[1] == 'M') {
            logo_w = *(int32_t *)(logo_data + 18);
            logo_h = *(int32_t *)(logo_data + 22);
            uint16_t bpp = *(uint16_t *)(logo_data + 28);
            uint32_t pix_off = *(uint32_t *)(logo_data + 10);
            
            /* Only support 24-bit or 32-bit BMPs */
            if ((bpp == 24 || bpp == 32) && logo_w > 0 && logo_h > 0 && 
                logo_w <= 256 && logo_h <= 256) {
                
                /* Decode BMP pixels (bottom-up BGR format) into 32x32 buffer */
                int row_stride = ((logo_w * (bpp / 8)) + 3) & ~3;
                uint8_t *pix_data = logo_data + pix_off;
                
                /* Scale to 32x32 */
                for (int row = 0; row < 32; row++) {
                    int src_row = (row * logo_h) / 32;
                    int bmp_row = logo_h - 1 - src_row;  /* BMP is bottom-up */
                    uint8_t *row_ptr = pix_data + bmp_row * row_stride;
                    
                    for (int col = 0; col < 32; col++) {
                        int src_col = (col * logo_w) / 32;
                        uint8_t b = row_ptr[src_col * (bpp / 8) + 0];
                        uint8_t g = row_ptr[src_col * (bpp / 8) + 1];
                        uint8_t r = row_ptr[src_col * (bpp / 8) + 2];
                        uint8_t a = (bpp == 32) ? row_ptr[src_col * (bpp / 8) + 3] : 0xFF;
                        
                        logo_pixels[row * 32 + col] = 
                            ((uint32_t)a << 24) | ((uint32_t)r << 16) | 
                            ((uint32_t)g << 8) | (uint32_t)b;
                    }
                }
            }
        }
    }
    
    /* Draw the logo if loaded */
    if (logo_w > 0 && logo_h > 0) {
        for (int row = 0; row < 32; row++) {
            for (int col = 0; col < 32; col++) {
                uint32_t pixel = logo_pixels[row * 32 + col];
                /* Draw pixel (handle transparency) */
                if ((pixel >> 24) > 128) {  /* Only draw if mostly opaque */
                    gpu_draw_pixel(x + col, y + row, pixel);
                }
            }
        }
    } else {
        /* Fallback: original palette icon */
        draw_rounded_rect(x + 1, y + 2, 30, 28, 0xFFF5E6CC, 0xFFD4A574);
        /* Color blobs */
        gpu_fill_rect(x + 5, y + 6, 5, 5, 0xFFFF4444);
        gpu_fill_rect(x + 12, y + 5, 5, 5, 0xFF808080);
        gpu_fill_rect(x + 20, y + 6, 5, 5, 0xFF44DD44);
        gpu_fill_rect(x + 7, y + 14, 5, 5, 0xFFFFDD33);
        gpu_fill_rect(x + 16, y + 13, 5, 5, 0xFFDD44DD);
        gpu_fill_rect(x + 10, y + 22, 5, 5, 0xFFFF8833);
        /* Brush handle */
        gpu_fill_rect(x + 22, y + 16, 3, 12, 0xFF8B6914);
        gpu_fill_rect(x + 21, y + 14, 5, 3, 0xFF606060);
    }
}

/* Calculator icon: dark calc with buttons */
void icon_draw_calc(int x, int y) {
    draw_rounded_rect(x + 2, y + 1, 28, 30, 0xFF2D2D3F, 0xFF707070);
    /* Display */
    gpu_fill_rect(x + 5, y + 4, 22, 8, 0xFF1A1A2E);
    gpu_draw_char(x + 18, y + 4, '0', 0xFF4ADE80, 0xFF1A1A2E);
    /* Button grid */
    uint32_t btn_colors[] = {0xFF505070, 0xFF505070, 0xFF505070, 0xFFE04050,
                             0xFF505070, 0xFF505070, 0xFF505070, 0xFF707070,
                             0xFF505070, 0xFF505070, 0xFF505070, 0xFF707070};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            gpu_fill_rect(x + 5 + c * 6, y + 14 + r * 6, 4, 4, btn_colors[r * 4 + c]);
        }
    }
}

/* File Browser icon: folder shape */
void icon_draw_files(int x, int y) {
    /* Folder tab */
    gpu_fill_rect(x + 3, y + 5, 12, 4, 0xFFFFBB33);
    /* Folder body */
    draw_rounded_rect(x + 2, y + 8, 28, 20, 0xFFFFCC44, 0xFFDD9911);
    /* Inner shadow */
    gpu_fill_rect(x + 4, y + 10, 24, 1, 0xFFDDAA22);
    /* File peek */
    gpu_fill_rect(x + 8, y + 6, 10, 4, 0xFFF8F8FF);
    gpu_fill_rect(x + 8, y + 6, 10, 1, 0xFFD0D0E0);
}

/* Clock icon: circular clock face */
void icon_draw_clock(int x, int y) {
    /* Circle background */
    int cx = x + 16, cy = y + 16, r = 14;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                uint32_t color = (dx * dx + dy * dy <= (r - 2) * (r - 2))
                                     ? 0xFF1A1A2E : 0xFF707070;
                gpu_draw_pixel(cx + dx, cy + dy, color);
            }
        }
    }
    /* Hour marks */
    gpu_fill_rect(cx - 1, cy - 12, 2, 3, 0xFFE0E0F0); /* 12 */
    gpu_fill_rect(cx - 1, cy + 9, 2, 3, 0xFFE0E0F0);  /* 6 */
    gpu_fill_rect(cx + 9, cy - 1, 3, 2, 0xFFE0E0F0);  /* 3 */
    gpu_fill_rect(cx - 12, cy - 1, 3, 2, 0xFFE0E0F0); /* 9 */
    /* Hands */
    gpu_fill_rect(cx, cy - 8, 1, 9, 0xFFFFFFFF);  /* Hour */
    gpu_fill_rect(cx, cy, 7, 1, 0xFF4ADE80);      /* Minute */
    /* Center dot */
    gpu_fill_rect(cx - 1, cy - 1, 2, 2, 0xFFE04050);
}

/* System Info icon: chip/CPU */
void icon_draw_sysinfo(int x, int y) {
    /* CPU body */
    gpu_fill_rect(x + 8, y + 8, 16, 16, 0xFF2D2D3F);
    gpu_fill_rect(x + 9, y + 9, 14, 14, 0xFF707070);
    /* Inner die */
    gpu_fill_rect(x + 12, y + 12, 8, 8, 0xFF1A1A2E);
    /* Pins (top, bottom, left, right) */
    for (int i = 0; i < 4; i++) {
        int px = x + 10 + i * 4;
        gpu_fill_rect(px, y + 4, 2, 4, 0xFF808090);  /* Top */
        gpu_fill_rect(px, y + 24, 2, 4, 0xFF808090); /* Bottom */
    }
    for (int i = 0; i < 4; i++) {
        int py = y + 10 + i * 4;
        gpu_fill_rect(x + 4, py, 4, 2, 0xFF808090);  /* Left */
        gpu_fill_rect(x + 24, py, 4, 2, 0xFF808090); /* Right */
    }
}

/* File on desktop icon: generic document */
void icon_draw_file(int x, int y) {
    /* Page shadow */
    gpu_fill_rect(x + 6, y + 3, 20, 26, 0xFF404060);
    /* Page body */
    gpu_fill_rect(x + 4, y + 1, 20, 26, 0xFFF0F0FF);
    /* Fold corner */
    gpu_fill_rect(x + 18, y + 1, 6, 6, 0xFFD0D0E0);
    gpu_fill_rect(x + 18, y + 1, 1, 6, 0xFFB0B0C0);
    gpu_fill_rect(x + 18, y + 6, 6, 1, 0xFFB0B0C0);
    /* Text lines */
    gpu_fill_rect(x + 8, y + 10, 12, 1, 0xFF808090);
    gpu_fill_rect(x + 8, y + 14, 10, 1, 0xFF808090);
    gpu_fill_rect(x + 8, y + 18, 12, 1, 0xFF808090);
    gpu_fill_rect(x + 8, y + 22, 8, 1, 0xFF808090);
}

/* Folder icon: classic folder shape */
void icon_draw_folder(int x, int y) {
    /* Tab */
    gpu_fill_rect(x + 3, y + 4, 10, 4, 0xFFFFBB33);
    /* Body */
    gpu_fill_rect(x + 2, y + 7, 28, 20, 0xFFFFCC44);
    /* Top edge */
    gpu_fill_rect(x + 2, y + 7, 28, 2, 0xFFFFDD55);
    /* Shadow line */
    gpu_fill_rect(x + 2, y + 26, 28, 1, 0xFFDD9911);
    /* Side edges */
    gpu_fill_rect(x + 2, y + 7, 1, 20, 0xFFDD9911);
    gpu_fill_rect(x + 29, y + 7, 1, 20, 0xFFDD9911);
}

/* Icon draw function type */
typedef void (*icon_draw_fn)(int x, int y);

/* Icon lookup table: maps app names to their draw functions.
 * Used by the desktop and taskbar to render proper icons. */
typedef struct {
    const char *app_name;
    icon_draw_fn draw;
} IconEntry;

static const IconEntry icon_table[] = {
    {"Terminal",    icon_draw_terminal},
    {"Notepad",     icon_draw_notepad},
    {"Paint",       icon_draw_paint},
    {"Calculator",  icon_draw_calc},
    {"File Explorer", icon_draw_files},
    {"Clock",       icon_draw_clock},
    {"System Info", icon_draw_sysinfo},
    {0, 0}
};

/* Look up an icon draw function by app name */
icon_draw_fn icon_get_draw_fn(const char *name) {
    for (int i = 0; icon_table[i].app_name; i++) {
        const char *a = icon_table[i].app_name;
        const char *b = name;
        bool match = true;
        while (*a && *b) {
            if (*a != *b) { match = false; break; }
            a++; b++;
        }
        if (match && *a == 0 && *b == 0) return icon_table[i].draw;
    }
    return 0;
}
