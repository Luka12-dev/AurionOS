#include "window_manager.h"
#include "drivers/mouse.h"
#include "../include/fs.h"

/* External GPU */
extern void   gpu_clear(uint32_t color);
extern void   gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void   gpu_fill_rect_blend(int x, int y, int w, int h,
                                  uint32_t color, uint8_t alpha);
extern void   gpu_blur_rect(int x, int y, int w, int h);
extern void   gpu_draw_pixel(int x, int y, uint32_t color);
extern void   gpu_draw_char(int x, int y, uint8_t c,
                            uint32_t fg, uint32_t bg);
extern void   gpu_draw_string(int x, int y, const uint8_t *str,
                              uint32_t fg, uint32_t bg);
extern int    gpu_flush(void);
extern int    gpu_get_width(void);
extern int    gpu_get_height(void);
extern uint32_t* gpu_get_backbuffer(void);

/* PREMIUM COLOR PALETTE — macOS Sequoia inspired */
#define ACCENT_BLUE       0xFF0A84FF  /* macOS accent */
#define ACCENT_BLUE_DIM   0xFF0066CC
#define ACCENT_GLOW       0xFF4DA6FF

#define MAT_DARK_BASE     0xFF1C1C1E  /* material base */
#define MAT_DARK_TINT     0xFF2C2C2E  /* material tint */
#define MAT_DARK_ELEVATED 0xFF3A3A3C  /* raised surfaces */
#define MAT_SEPARATOR     0xFF48484A

#define TL_CLOSE_TOP      0xFFFF6159
#define TL_CLOSE_BOT      0xFFE8453E
#define TL_MIN_TOP        0xFFFFBD2E
#define TL_MIN_BOT        0xFFE8A317
#define TL_MAX_TOP        0xFF28C93F
#define TL_MAX_BOT        0xFF1FA82F
#define TL_INACTIVE       0xFF4A4A4C

#define TEXT_PRIMARY      0xFFF5F5F7
#define TEXT_SECONDARY    0xFFAEAEB2
#define TEXT_TERTIARY     0xFF6E6E73

/* PERFORMANCE OPTIMIZATIONS - Dirty flag system */

static bool inst_mode = false;
static int scr_w = 1024, scr_h = 768;

bool wm_bg_dirty = true;
bool wm_dock_dirty = true;
bool wm_windows_dirty = true;
bool wm_cursor_dirty = true;
bool wm_full_redraw = true;

/* Dock background cache */
#define MAX_DOCK_CACHE_W 800
#define MAX_DOCK_CACHE_H 120
static uint32_t wm_dock_bg_cache[MAX_DOCK_CACHE_W * MAX_DOCK_CACHE_H];
static int wm_dock_bg_x = 0, wm_dock_bg_y = 0;
static int wm_dock_bg_w = 0, wm_dock_bg_h = 0;
static bool wm_dock_bg_cached = false;

static int wm_prev_cursor_x = -1;
static int wm_prev_cursor_y = -1;

/* Animation counter for smooth effects */
static uint32_t wm_anim_tick = 0;

/* WALLPAPER SUPPORT */

extern void c_puts(const char *s);
#define WALLPAPER_LBA  10000
#define INSTALLER_WALLPAPER_LBA 12000
#define WALLPAPER_NAME "WP.BMP"

static char g_wallpaper_fs_path[FS_MAX_FILENAME];

typedef struct {
    bool     valid;
    int32_t  width;
    int32_t  height;
    uint8_t  bpp;
    uint32_t pixel_data_lba;
    int32_t  row_stride;
    uint32_t pixel_offset;
} WallpaperInfo;

static WallpaperInfo g_wallpaper = {0};

extern bool iso9660_find_file(const char *filename, uint32_t *out_lba, uint32_t *out_size);
extern int  fs_get_stored_content_bytes(const char *filename);
extern int  load_file_content_range(const char *filename, char *buffer, uint32_t offset, int max_len);
extern int  disk_read_lba_cdrom(uint32_t lba, uint32_t count, void *buffer);
extern int  disk_read_lba_hdd(uint32_t lba, uint32_t count, void *buffer);

typedef enum {
    WALLPAPER_DISK_FS = 0,
    WALLPAPER_DISK_ISO,
    WALLPAPER_DISK_HDD
} WallpaperDiskKind;

static WallpaperDiskKind g_wallpaper_disk = WALLPAPER_DISK_FS;

static const char *const g_installer_wallpaper_fs_candidates[] = {
    "/Wallpaper/Background_installer.bmp",
    "/Wallpapers/Background_installer.bmp",
    NULL
};

static const char *const g_desktop_wallpaper_fs_candidates[] = {
    "/Wallpaper/Wallpaper1.bmp",
    "/System/wallpaper.bmp",
    "/Wallpaper/wallpaper.bmp",
    "/Wallpapers/wallpaper.bmp",
    NULL
};

static bool wm_load_wallpaper_from_fs(void) {
    extern int load_file_content(const char *filename, char *buffer, int max_len);

    g_wallpaper_fs_path[0] = '\0';

    const char *const *candidates = inst_mode
        ? g_installer_wallpaper_fs_candidates
        : g_desktop_wallpaper_fs_candidates;

    for (int pi = 0; candidates[pi]; pi++) {
        const char *path = candidates[pi];
        char sector[54];
        int read = load_file_content(path, sector, 54);
        if (read < 54) continue;

        if (sector[0] != 'B' || sector[1] != 'M') continue;

        int32_t  w        = *(int32_t *)(sector + 18);
        int32_t  h        = *(int32_t *)(sector + 22);
        uint16_t bpp      = *(uint16_t *)(sector + 28);
        uint32_t pix_off  = *(uint32_t *)(sector + 10);
        uint32_t bf_size  = *(uint32_t *)(sector + 2);

        if (w <= 0 || h <= 0 || (bpp != 24 && bpp != 32)) continue;

        int32_t row_stride = (w * (bpp / 8) + 3) & ~3;
        uint32_t need = pix_off + (uint32_t)row_stride * (uint32_t)h;
        if (need < pix_off) continue;

        uint32_t min_stored = need;
        if (bf_size >= 54u && bf_size >= need && bf_size <= 128u * 1024u * 1024u)
            min_stored = bf_size;

        int stored = fs_get_stored_content_bytes(path);
        if (stored < 0) continue;
        if ((uint32_t)stored < min_stored) continue;

        g_wallpaper.valid          = true;
        g_wallpaper.width          = w;
        g_wallpaper.height         = h;
        g_wallpaper.bpp            = (uint8_t)bpp;
        g_wallpaper.pixel_data_lba = 0xFFFFFFFF;
        g_wallpaper.row_stride     = row_stride;
        g_wallpaper.pixel_offset   = (int)pix_off;

        {
            int i = 0;
            for (; path[i] && i < FS_MAX_FILENAME - 1; i++)
                g_wallpaper_fs_path[i] = path[i];
            g_wallpaper_fs_path[i] = '\0';
        }
        g_wallpaper_disk = WALLPAPER_DISK_FS;
        c_puts("[WM] Found wallpaper in filesystem\n");
        return true;
    }
    return false;
}

static bool wm_find_wallpaper_from_iso(void) {
    extern bool ata_cdrom_available(void);
    if (!ata_cdrom_available()) {
        c_puts("[WM] CD-ROM drive not found. ISO wallpaper probe skipped.\n");
        return false;
    }

    uint32_t iso_lba, iso_size;
    bool found = false;

    if (inst_mode) {
        if (iso9660_find_file("Wallpaper/Background_installer.bmp", &iso_lba, &iso_size) ||
            iso9660_find_file("BACKGROUND_INSTALLER.BMP", &iso_lba, &iso_size) ||
            iso9660_find_file("Background_installer.bmp", &iso_lba, &iso_size) ||
            iso9660_find_file("BG_INST.BMP", &iso_lba, &iso_size)) {
            found = true;
        }
    }

    if (!found && !inst_mode) {
        if (iso9660_find_file("Wallpaper/Wallpaper1.bmp", &iso_lba, &iso_size) ||
            iso9660_find_file("WP.BMP", &iso_lba, &iso_size)) {
            found = true;
        }
    }

    if (!found) return false;

    char sector[512];
    if (disk_read_lba_cdrom(iso_lba * 4, 1, sector) != 0) return false;
    if (sector[0] != 'B' || sector[1] != 'M') return false;

    int32_t  w        = *(int32_t *)(sector + 18);
    int32_t  h        = *(int32_t *)(sector + 22);
    uint16_t bpp      = *(uint16_t *)(sector + 28);
    uint32_t pix_off  = *(uint32_t *)(sector + 10);

    if (w <= 0 || h <= 0 || (bpp != 24 && bpp != 32)) return false;

    g_wallpaper.valid          = true;
    g_wallpaper.width          = w;
    g_wallpaper.height         = h;
    g_wallpaper.bpp            = (uint8_t)bpp;
    g_wallpaper.pixel_data_lba = iso_lba * 4;
    g_wallpaper.row_stride     = (w * (bpp/8) + 3) & ~3;
    g_wallpaper.pixel_offset   = (int)pix_off;
    g_wallpaper_disk           = WALLPAPER_DISK_ISO;
    c_puts("[WM] Found wallpaper in ISO\n");
    return true;
}

static bool wm_find_wallpaper_at_lba(uint32_t lba) {
    char sector[512];
    if (disk_read_lba_hdd(lba, 1, sector) != 0) return false;
    if (sector[0] != 'B' || sector[1] != 'M') return false;

    int32_t  w        = *(int32_t *)(sector + 18);
    int32_t  h        = *(int32_t *)(sector + 22);
    uint16_t bpp      = *(uint16_t *)(sector + 28);
    uint32_t pix_off  = *(uint32_t *)(sector + 10);

    if (w <= 0 || h <= 0 || (bpp != 24 && bpp != 32)) return false;

    g_wallpaper.valid          = true;
    g_wallpaper.width          = w;
    g_wallpaper.height         = h;
    g_wallpaper.bpp            = (uint8_t)bpp;
    g_wallpaper.pixel_data_lba = lba;
    g_wallpaper.row_stride     = (w * (bpp/8) + 3) & ~3;
    g_wallpaper.pixel_offset   = (int)pix_off;
    g_wallpaper_disk           = WALLPAPER_DISK_HDD;
    c_puts("[WM] Found wallpaper at raw LBA\n");
    return true;
}

static bool wm_find_wallpaper(void) {
    return wm_find_wallpaper_at_lba(WALLPAPER_LBA);
}

/* ═══════════════════════════════════════════════════════════════════════
   WALLPAPER RENDER
   ═══════════════════════════════════════════════════════════════════════ */

bool wallpaper_rendered = false;
static uint32_t *g_wallpaper_cache = 0;
static int g_wallpaper_cache_w = 0;
static int g_wallpaper_cache_h = 0;

static void wm_render_wallpaper(int screen_w, int screen_h)
{
    if (!g_wallpaper.valid) return;

    extern void* kmalloc(uint32_t size);
    uint32_t *bb = gpu_get_backbuffer();
    if (!bb) return;

    if (g_wallpaper_cache) {
        if (g_wallpaper_cache_w != screen_w || g_wallpaper_cache_h != screen_h) {
            extern void kfree(void *ptr);
            kfree(g_wallpaper_cache);
            g_wallpaper_cache = 0;
            g_wallpaper_cache_w = 0;
            g_wallpaper_cache_h = 0;
            wallpaper_rendered = false;
        } else {
            uint32_t *src = g_wallpaper_cache;
            uint32_t *dst = bb;
            uint32_t count = screen_w * screen_h;
            for (uint32_t i = 0; i < count; i++) dst[i] = src[i];
            return;
        }
    }

    if (wallpaper_rendered) return;
    wallpaper_rendered = true;

    c_puts("[WM] Creating wallpaper cache in RAM...\n");
    g_wallpaper_cache = (uint32_t*)kmalloc(screen_w * screen_h * 4);
    if (!g_wallpaper_cache) {
        c_puts("[WM] ERROR: Failed to allocate wallpaper cache\n");
        return;
    }

    g_wallpaper_cache_w = screen_w;
    g_wallpaper_cache_h = screen_h;

    for (int i = 0; i < screen_w * screen_h; i++) g_wallpaper_cache[i] = 0xFF000000;

    int32_t  bmp_w        = g_wallpaper.width;
    int32_t  bmp_h        = g_wallpaper.height;
    uint8_t  bmp_bpp      = g_wallpaper.bpp;
    int32_t  row_stride    = g_wallpaper.row_stride;
    uint32_t bytes_per_pix = bmp_bpp / 8;

    static char row_buffer[65536];

    int32_t draw_w, draw_h;
    if ((uint64_t)screen_w * bmp_h > (uint64_t)screen_h * bmp_w) {
        draw_w = screen_w;
        draw_h = (int32_t)(((uint64_t)bmp_h * screen_w) / bmp_w);
    } else {
        draw_h = screen_h;
        draw_w = (int32_t)(((uint64_t)bmp_w * screen_h) / bmp_h);
    }

    int32_t offset_x = (screen_w - draw_w) / 2;
    int32_t offset_y = (screen_h - draw_h) / 2;

    uint32_t scale_fp_x = ((uint64_t)bmp_w << 16) / draw_w;
    uint32_t scale_fp_y = ((uint64_t)bmp_h << 16) / draw_h;

    for (int32_t screen_row = screen_h - 1; screen_row >= 0; screen_row--) {
        uint32_t *cache_row = g_wallpaper_cache + screen_row * screen_w;

        int32_t s_off_y = screen_row - offset_y;
        if (s_off_y < 0 || s_off_y >= draw_h) {
            for (int32_t x = 0; x < screen_w; x++) cache_row[x] = 0xFF000000;
            continue;
        }

        uint32_t bmp_row_fp = s_off_y * scale_fp_y;
        int32_t  bmp_row    = (bmp_h - 1) - (int32_t)(bmp_row_fp >> 16);

        if (bmp_row < 0 || bmp_row >= bmp_h) {
            for (int32_t x = 0; x < screen_w; x++) cache_row[x] = 0xFF000000;
            continue;
        }

        uint32_t row_data_start = g_wallpaper.pixel_offset + (uint32_t)(bmp_row * row_stride);

        if (g_wallpaper.pixel_data_lba == 0xFFFFFFFF) {
            int got = load_file_content_range(g_wallpaper_fs_path, row_buffer,
                                              row_data_start, (int)row_stride);
            if (got < (int)row_stride) {
                for (int32_t x = 0; x < screen_w; x++) cache_row[x] = 0xFF000000;
                continue;
            }
        } else {
            uint32_t row_lba = g_wallpaper.pixel_data_lba + (row_data_start / 512);
            uint32_t row_internal_off = (uint32_t)(row_data_start % 512);

            uint32_t sectors_needed = (row_internal_off + row_stride + 511) / 512;
            if (sectors_needed > sizeof(row_buffer)/512) sectors_needed = sizeof(row_buffer)/512;

            int dr = (g_wallpaper_disk == WALLPAPER_DISK_ISO)
                         ? disk_read_lba_cdrom(row_lba, sectors_needed, row_buffer)
                         : disk_read_lba_hdd(row_lba, sectors_needed, row_buffer);
            if (dr != 0) {
                for (int32_t x = 0; x < screen_w; x++) cache_row[x] = 0xFF000000;
                continue;
            }
        }

        uint32_t row_internal_off = (g_wallpaper.pixel_data_lba == 0xFFFFFFFF)
                                        ? 0u
                                        : (uint32_t)(row_data_start % 512);

        for (int32_t screen_col = 0; screen_col < screen_w; screen_col++) {
            int32_t s_off_x = screen_col - offset_x;
            if (s_off_x < 0 || s_off_x >= draw_w) {
                cache_row[screen_col] = 0xFF000000;
                continue;
            }

            uint32_t bmp_col_fp = s_off_x * scale_fp_x;
            int32_t  bmp_col    = (int32_t)(bmp_col_fp >> 16);

            if (bmp_col < 0 || bmp_col >= bmp_w) {
                cache_row[screen_col] = 0xFF000000;
                continue;
            }

            int32_t pix_off = row_internal_off + (bmp_col * bytes_per_pix);
            if (pix_off + 2 >= (int32_t)sizeof(row_buffer)) {
                 cache_row[screen_col] = 0xFF000000;
                 continue;
            }

            uint8_t b = (uint8_t)row_buffer[pix_off];
            uint8_t g = (uint8_t)row_buffer[pix_off + 1];
            uint8_t r = (uint8_t)row_buffer[pix_off + 2];

            cache_row[screen_col] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    c_puts("[WM] Wallpaper cached!\n");

    uint32_t *src = g_wallpaper_cache;
    uint32_t *dst = bb;
    for (int i = 0; i < screen_w * screen_h; i++) dst[i] = src[i];
}

/* ═══════════════════ External kernel ════════════════════════════════ */
extern uint16_t sys_getkey(void);
extern uint16_t c_getkey_nonblock(void);
extern char     keyboard_remap(char c);
extern int      sys_kb_hit(void);
extern uint32_t get_ticks(void);
extern int      sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);
extern bool     vmware_svga_available(void);

extern int  load_file_content(const char *fn, char *buf, int max);
extern int  save_file_content(const char *fn, const char *data, int len);
extern int  fs_save_to_disk(void);

/* ═══════════════════════════════════════════════════════════════════════
   GLOBAL SETTINGS
   ═══════════════════════════════════════════════════════════════════════ */
OSSettings g_settings = {
    .bg_color            = 0,
    .window_style        = 0,
    .startup_app_idx     = 0,
    .dock_magnification  = 1,
    .dock_transparent    = 1,
    .resolution          = 1
};

/* ═══════════════════════════════════════════════════════════════════════
   MODULE STATE
   ═══════════════════════════════════════════════════════════════════════ */
volatile int  desktop_exit_reason = -1;
int           dock_clicked_slot   = -1;

static Window  windows[WM_MAX_WINDOWS];
static int     win_count  = 0;
static Window *focus_win  = 0;

static bool    res_changed = false;
static bool    bg_dirty    = true;

static bool    dragging = false;
static int     drag_ox, drag_oy;
static Window *drag_win = 0;
static int     drag_tx, drag_ty;

static bool    resizing  = false;
static int     rsz_edge  = 0;
static Window *rsz_win   = 0;

static bool    prev_lmb = false;
static bool    prev_rmb = false;

static uint32_t dbl_tick = 0;
static Window  *dbl_win  = 0;

static icon_draw_fn dock_fns[WM_MAX_DOCK_SLOTS];
static char         dock_names[WM_MAX_DOCK_SLOTS][64];
static int          dock_cnt   = 0;
static int          dock_hover = -1;
static int          dock_sz[WM_MAX_DOCK_SLOTS];
static int          dock_bounce[WM_MAX_DOCK_SLOTS]; /* NEW: bounce animation */

static int      tip_slot = -1;
static uint32_t tip_tick = 0;
static bool     tip_show = false;

/* ═══════════════════════════════════════════════════════════════════════
   UTILITY
   ═══════════════════════════════════════════════════════════════════════ */
static int mn(int a, int b) { return a < b ? a : b; }
static int mx(int a, int b) { return a > b ? a : b; }
static int cl(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int ab(int v) { return v < 0 ? -v : v; }

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, int m) {
    int i = 0;
    while (i < m - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
bool wm_string_compare(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}
static bool seq(const char *a, const char *b) { return wm_string_compare(a, b); }

/* Color interpolation for gradients */
static uint32_t lerp_color(uint32_t c1, uint32_t c2, uint8_t t) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + ((r2 - r1) * t) / 255;
    uint8_t g = g1 + ((g2 - g1) * t) / 255;
    uint8_t b = b1 + ((b2 - b1) * t) / 255;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

/* ═══════════════════════════════════════════════════════════════════════
   ROUNDED RECT PRIMITIVES — with better anti-aliasing feel
   ═══════════════════════════════════════════════════════════════════════ */
static int corner_inset(int r, int row)
{
    int dy = r - row;
    int dx = 0;
    int r2 = r * r;
    while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
    return r - dx;
}

static void rrect(int x, int y, int w, int h, uint32_t c, int r)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) { gpu_fill_rect(x, y, w, h, c); return; }
    r = mn(r, mn(w / 2, h / 2));
    for (int i = 0; i < r; i++) {
        int ins = corner_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0) {
            gpu_fill_rect(x + ins, y + i,         sw, 1, c);
            gpu_fill_rect(x + ins, y + h - 1 - i, sw, 1, c);
        }
    }
    if (h - 2 * r > 0) gpu_fill_rect(x, y + r, w, h - 2 * r, c);
}

static void rrect_a(int x, int y, int w, int h, uint32_t c, int r, uint8_t a)
{
    if (w <= 0 || h <= 0 || a == 0) return;
    if (r < 1) { gpu_fill_rect_blend(x, y, w, h, c, a); return; }
    r = mn(r, mn(w / 2, h / 2));
    for (int i = 0; i < r; i++) {
        int ins = corner_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0) {
            gpu_fill_rect_blend(x + ins, y + i,         sw, 1, c, a);
            gpu_fill_rect_blend(x + ins, y + h - 1 - i, sw, 1, c, a);
        }
    }
    if (h - 2 * r > 0)
        gpu_fill_rect_blend(x, y + r, w, h - 2 * r, c, a);
}

static void rrect_top(int x, int y, int w, int h, uint32_t c, int r)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) { gpu_fill_rect(x, y, w, h, c); return; }
    r = mn(r, mn(w / 2, h));
    for (int i = 0; i < r; i++) {
        int ins = corner_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0) gpu_fill_rect(x + ins, y + i, sw, 1, c);
    }
    if (h - r > 0) gpu_fill_rect(x, y + r, w, h - r, c);
}

static void rrect_bot(int x, int y, int w, int h, uint32_t c, int r)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) { gpu_fill_rect(x, y, w, h, c); return; }
    r = mn(r, mn(w / 2, h));
    if (h - r > 0) gpu_fill_rect(x, y, w, h - r, c);
    for (int i = 0; i < r; i++) {
        int ins = corner_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0) gpu_fill_rect(x + ins, y + h - 1 - i, sw, 1, c);
    }
}

static void rrect_top_a(int x, int y, int w, int h, uint32_t c, int r, uint8_t a)
{
    if (w <= 0 || h <= 0 || a == 0) return;
    if (r < 1) { gpu_fill_rect_blend(x, y, w, h, c, a); return; }
    r = mn(r, mn(w / 2, h));
    for (int i = 0; i < r; i++) {
        int ins = corner_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0)
            gpu_fill_rect_blend(x + ins, y + i, sw, 1, c, a);
    }
    if (h - r > 0)
        gpu_fill_rect_blend(x, y + r, w, h - r, c, a);
}

static void rrect_bot_a(int x, int y, int w, int h, uint32_t c, int r, uint8_t a)
{
    if (w <= 0 || h <= 0 || a == 0) return;
    if (r < 1) { gpu_fill_rect_blend(x, y, w, h, c, a); return; }
    r = mn(r, mn(w / 2, h));
    if (h - r > 0)
        gpu_fill_rect_blend(x, y, w, h - r, c, a);
    for (int i = 0; i < r; i++) {
        int ins = corner_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0)
            gpu_fill_rect_blend(x + ins, y + h - 1 - i, sw, 1, c, a);
    }
}

/* Gradient rounded rect - NEW premium effect */
static void rrect_gradient(int x, int y, int w, int h,
                           uint32_t top_color, uint32_t bot_color, int r)
{
    if (w <= 0 || h <= 0) return;
    if (r < 1) r = 0;
    r = mn(r, mn(w / 2, h / 2));

    for (int row = 0; row < h; row++) {
        uint8_t t = (row * 255) / (h > 1 ? h - 1 : 1);
        uint32_t c = lerp_color(top_color, bot_color, t);
        int ins = 0;
        if (row < r) ins = corner_inset(r, row);
        else if (row >= h - r) ins = corner_inset(r, h - 1 - row);
        int sw = w - 2 * ins;
        if (sw > 0) gpu_fill_rect(x + ins, y + row, sw, 1, c);
    }
}

static void circ(int cx, int cy, int r, uint32_t c)
{
    int r2 = r * r + r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        if (dx > 0)
            gpu_fill_rect(cx - dx, cy + dy, dx * 2 + 1, 1, c);
        else if (dy * dy <= r2)
            gpu_draw_pixel(cx, cy + dy, c);
    }
}

/* Gradient circle - NEW premium traffic lights */
static void circ_gradient(int cx, int cy, int r, uint32_t top, uint32_t bot)
{
    int r2 = r * r + r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        uint8_t t = ((dy + r) * 255) / (2 * r);
        uint32_t c = lerp_color(top, bot, t);
        if (dx > 0)
            gpu_fill_rect(cx - dx, cy + dy, dx * 2 + 1, 1, c);
        else if (dy * dy <= r2)
            gpu_draw_pixel(cx, cy + dy, c);
    }
}

/* Alpha-blended circle */
static void circ_a(int cx, int cy, int r, uint32_t c, uint8_t a)
{
    int r2 = r * r + r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        if (dx > 0)
            gpu_fill_rect_blend(cx - dx, cy + dy, dx * 2 + 1, 1, c, a);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   PREMIUM SHADOW — Multi-layer soft shadow with offset
   Uses alpha blending to create genuine depth
   ═══════════════════════════════════════════════════════════════════════ */
static void draw_shadow(int x, int y, int w, int h, bool focused, int cr)
{
    if (w <= 0 || h <= 0) return;

    /* A smooth, premium multi-layer shadow using rounded rects for all layers */
    if (focused) {
        /* Layer 1: Ambient wide shadow */
        rrect_a(x - 12, y - 5, w + 24, h + 22, 0xFF000000, cr + 8, 6);
        /* Layer 2: Medium soft shadow */
        rrect_a(x - 8, y - 3,  w + 16, h + 14, 0xFF000000, cr + 6, 10);
        /* Layer 3: Defined shadow */
        rrect_a(x - 4,  y - 1,  w + 8, h + 7, 0xFF000000, cr + 3,  20);
        /* Layer 4: Close contact shadow */
        rrect_a(x - 1,  y,      w + 2,  h + 2,  0xFF000000, cr + 1,  40);
    } else {
        /* Layer 1: Ambient wide shadow */
        rrect_a(x - 6, y - 2, w + 12, h + 10, 0xFF000000, cr + 4, 6);
        /* Layer 2: Medium defined shadow */
        rrect_a(x - 3,  y - 1, w + 6, h + 5,  0xFF000000, cr + 2, 12);
        /* Layer 3: Close contact shadow */
        rrect_a(x - 1,  y,     w + 2,  h + 2,  0xFF000000, cr + 1, 30);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   PREMIUM TRAFFIC-LIGHT BUTTONS — with gradient + inner glow
   ═══════════════════════════════════════════════════════════════════════ */
static void tl_btn(int cx, int cy, int r, uint32_t top, uint32_t bot,
                   bool show_sym, int sym, bool inactive)
{
    /* Outer subtle rim shadow */
    circ_a(cx, cy + 1, r, 0xFF000000, 80);

    if (inactive) {
        /* Inactive: subtle gray */
        circ(cx, cy, r, 0xFF4A4A4C);
        circ(cx, cy, r - 1, 0xFF3A3A3C);
    } else {
        /* Active: vibrant gradient */
        circ_gradient(cx, cy, r, top, bot);

        /* Top shine - stronger highlight */
        gpu_fill_rect_blend(cx - r + 2, cy - r + 1, r * 2 - 4, 1,
                           0xFFFFFFFF, 120);
        gpu_fill_rect_blend(cx - r + 3, cy - r + 2, r * 2 - 6, 1,
                           0xFFFFFFFF, 60);
        gpu_draw_pixel(cx, cy - r + 1, 0xFFFFFFFF);
        
        /* Bottom shadow for depth */
        gpu_fill_rect_blend(cx - r + 3, cy + r - 2, r * 2 - 6, 1,
                           0xFF000000, 40);
    }

    if (!show_sym) return;

    /* Symbol color - dark for contrast */
    uint32_t sc = inactive ? 0xFF1A1A1C : 0xFF2A0A00;

    switch (sym) {
    case 0: /* × close - thicker X */
        for (int i = -3; i <= 3; i++) {
            gpu_draw_pixel(cx + i, cy + i, sc);
            gpu_draw_pixel(cx + i, cy - i, sc);
            if (i != 0) {
                gpu_draw_pixel(cx + i, cy + i - 1, sc);
                gpu_draw_pixel(cx + i, cy - i + 1, sc);
            }
        }
        break;
    case 1: /* − minimize - thicker line */
        gpu_fill_rect(cx - 4, cy, 9, 2, sc);
        break;
    case 2: /* fullscreen - double arrows */
        /* Top-left arrow */
        gpu_fill_rect(cx - 3, cy - 3, 4, 1, sc);
        gpu_fill_rect(cx - 3, cy - 2, 3, 1, sc);
        gpu_fill_rect(cx - 3, cy - 1, 2, 1, sc);
        gpu_fill_rect(cx - 3, cy,     1, 1, sc);
        /* Bottom-right arrow */
        gpu_fill_rect(cx,     cy + 3, 4, 1, sc);
        gpu_fill_rect(cx + 1, cy + 2, 3, 1, sc);
        gpu_fill_rect(cx + 2, cy + 1, 2, 1, sc);
        gpu_fill_rect(cx + 3, cy,     1, 1, sc);
        break;
    }
}

static void draw_traffic_lights(Window *win, int hmx, int hmy)
{
    int cy  = win->y + WM_TITLEBAR_HEIGHT / 2;
    int cx0 = win->x + WM_TL_LEFT_PAD;
    int cx1 = cx0 + WM_TL_SPACING;
    int cx2 = cx1 + WM_TL_SPACING;
    int hr  = WM_TL_RADIUS + 4;

    bool near = false, h0 = false, h1 = false, h2 = false;
    if (hmx >= 0 && hmy >= 0 &&
        hmx >= cx0 - hr && hmx <= cx2 + hr &&
        hmy >= cy  - hr && hmy <= cy  + hr)
    {
        near = true;
        #define TH(px, py, ccx, ccy) \
            (((px)-(ccx))*((px)-(ccx)) + ((py)-(ccy))*((py)-(ccy)) <= (hr)*(hr))
        h0 = TH(hmx, hmy, cx0, cy);
        h1 = TH(hmx, hmy, cx1, cy);
        h2 = TH(hmx, hmy, cx2, cy);
        #undef TH
    }

    bool colorize = win->focused || near;

    tl_btn(cx0, cy, WM_TL_RADIUS, TL_CLOSE_TOP, TL_CLOSE_BOT,
           near && h0, 0, !colorize);
    tl_btn(cx1, cy, WM_TL_RADIUS, TL_MIN_TOP, TL_MIN_BOT,
           near && h1, 1, !colorize);
    tl_btn(cx2, cy, WM_TL_RADIUS, TL_MAX_TOP, TL_MAX_BOT,
           near && h2, 2, !colorize);
}

static void draw_win_buttons(Window *win)
{
    int bw = WM_WIN_BTN_W, bh = WM_WIN_BTN_H;
    int by = win->y + (WM_TITLEBAR_HEIGHT - bh) / 2;
    int cx = win->x + win->w - bw - 4;

    /* Close button - modern flat red */
    gpu_fill_rect(cx, by, bw, bh, 0xFFE81123);
    gpu_draw_char(cx + 10, by + 7, 'X', 0xFFFFFFFF, 0);
    cx -= bw + 2;

    /* Maximize - flat gray */
    gpu_fill_rect(cx, by, bw, bh, 0xFF2D2D2D);
    gpu_fill_rect(cx + 8, by + 5,     12, 1,       0xFFFFFFFF);
    gpu_fill_rect(cx + 8, by + bh - 6, 12, 1,       0xFFFFFFFF);
    gpu_fill_rect(cx + 8, by + 5,      1,  bh - 10, 0xFFFFFFFF);
    gpu_fill_rect(cx + 19,by + 5,      1,  bh - 10, 0xFFFFFFFF);
    cx -= bw + 2;

    /* Minimize - flat gray */
    gpu_fill_rect(cx, by, bw, bh, 0xFF2D2D2D);
    gpu_fill_rect(cx + 8, by + bh / 2, 12, 2, 0xFFFFFFFF);
}

/* ═══════════════════════════════════════════════════════════════════════
   SETTINGS
   ═══════════════════════════════════════════════════════════════════════ */
void wm_load_settings(void)
{
    OSSettings ld;
    int n = load_file_content("/Settings.dat",
                              (char *)&ld, sizeof(OSSettings));
    if (n == sizeof(OSSettings)) {
        g_settings = ld;
        if (!g_settings.dock_magnification && !g_settings.dock_transparent) {
            g_settings.dock_magnification = 1;
            g_settings.dock_transparent   = 1;
            wm_save_settings();
        }
    } else {
        wm_save_settings();
    }
}

void wm_save_settings(void)
{
    save_file_content("/Settings.dat",
                      (const char *)&g_settings, sizeof(OSSettings));
    fs_save_to_disk();

    static const uint16_t ws[] = {800,1024,1280,1280,1440,1600,1920,2560};
    static const uint16_t hs[] = {600,768, 720, 1024,900, 900, 1080,1440};
    int i = g_settings.resolution;
    if (i >= 0 && i < 8) {
        char b[512];
        for (int j = 0; j < 512; j++) b[j] = 0;
        b[0] = 'B'; b[1] = 'O'; b[2] = 'O'; b[3] = 'T'; b[4] = (char)i;
        *(uint16_t *)(b + 8)  = ws[i];
        *(uint16_t *)(b + 10) = hs[i];
        extern int disk_write_lba(uint32_t, uint32_t, const void *);
        disk_write_lba(999, 1, b);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   INSTALLER MODE
   ═══════════════════════════════════════════════════════════════════════ */
void wm_set_installer_mode(bool e) {
    if (inst_mode != e) {
        inst_mode = e;
        g_wallpaper.valid = false;
        if (wm_load_wallpaper_from_fs()) {
            c_puts("[WM] Wallpaper loaded from filesystem.\n");
        } else {
            c_puts("[WM] Probing ISO for wallpaper...\n");
            if (wm_find_wallpaper_from_iso()) {
                c_puts("[WM] Wallpaper loaded from ISO.\n");
            } else if (inst_mode) {
                wm_find_wallpaper_at_lba(INSTALLER_WALLPAPER_LBA);
            } else {
                wm_find_wallpaper();
            }
        }
        if (g_wallpaper_cache) {
            extern void kfree(void *ptr);
            kfree(g_wallpaper_cache);
            g_wallpaper_cache = 0;
            wallpaper_rendered = false;
        }
        wm_invalidate_all();
    }
}
bool wm_is_installer_mode(void) { return inst_mode; }

/* ═══════════════════════════════════════════════════════════════════════
   INITIALIZATION
   ═══════════════════════════════════════════════════════════════════════ */
void wm_init(int sw, int sh)
{
    scr_w = sw;  scr_h = sh;
    win_count = 0;  focus_win = 0;
    dragging = false;  resizing = false;
    bg_dirty = true;
    desktop_exit_reason = -1;
    dock_cnt = 0;  dock_hover = -1;
    tip_slot = -1;  tip_show = false;
    prev_lmb = false;  prev_rmb = false;
    wm_anim_tick = 0;

    if (!wm_find_wallpaper()) {
        if (!wm_find_wallpaper_from_iso()) {
            wm_load_wallpaper_from_fs();
        }
    }

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].visible      = false;
        windows[i].focused      = false;
        windows[i].state        = WM_STATE_NORMAL;
        windows[i].z_order      = 0;
        windows[i].on_draw      = 0;
        windows[i].on_key       = 0;
        windows[i].on_mouse     = 0;
        windows[i].on_close     = 0;
        windows[i].user_data    = 0;
        windows[i].is_chromeless = false;
        windows[i].needs_redraw = true;
        windows[i].anim_phase   = 0;
        windows[i].opacity      = 255;
    }
    for (int i = 0; i < WM_MAX_DOCK_SLOTS; i++) {
        dock_sz[i] = WM_DOCK_ICON_BASE;
        dock_bounce[i] = 0;
    }
}

void wm_set_screen_size(int sw, int sh)
{
    scr_w = sw;  scr_h = sh;
    res_changed = true;  bg_dirty = true;

    extern void kfree(void *ptr);
    if (g_wallpaper_cache) {
        kfree(g_wallpaper_cache);
        g_wallpaper_cache = 0;
        g_wallpaper_cache_w = 0;
        g_wallpaper_cache_h = 0;
        wallpaper_rendered = false;
    }

    /* Invalidate dock background cache on resolution change */
    wm_dock_bg_cached = false;
    wm_dock_bg_x = 0;
    wm_dock_bg_y = 0;
    wm_dock_bg_w = 0;
    wm_dock_bg_h = 0;

    int mxy = sh - WM_TASKBAR_HEIGHT;
    for (int i = 0; i < win_count; i++) {
        Window *w = &windows[i];
        if (!w->visible) continue;
        if (w->x + w->w > sw) w->x = mx(0, sw - w->w);
        if (w->y + w->h > mxy) w->y = mx(0, mxy - w->h);
        if (w->x < 0) w->x = 0;
        if (w->y < 0) w->y = 0;
    }
}

void wm_set_resolution(int w, int h) {
    wm_set_screen_size(w, h);
    mouse_set_bounds(w, h);
}

bool wm_resolution_changed(void)
{
    bool r = res_changed;
    res_changed = false;
    return r;
}

/* ═══════════════════════════════════════════════════════════════════════
   WINDOW LIFECYCLE
   ═══════════════════════════════════════════════════════════════════════ */
Window *wm_create_window(const char *title, int x, int y, int w, int h)
{
    if (win_count >= WM_MAX_WINDOWS) return 0;
    Window *win = &windows[win_count];
    scpy(win->title, title, 64);
    win->x = x;  win->y = y;  win->w = w;  win->h = h;
    win->saved_x = x;  win->saved_y = y;
    win->saved_w = w;  win->saved_h = h;
    win->min_w = 200;  win->min_h = 120;
    win->state = WM_STATE_NORMAL;
    win->visible = true;  win->focused = false;
    win->z_order = win_count;
    win->app_id = win_count;
    win->is_chromeless = false;
    win->needs_redraw = true;
    win->on_draw = 0;  win->on_key = 0;
    win->on_mouse = 0; win->on_close = 0;
    win->user_data = 0;
    win->opacity = 255;
    win->anim_phase = 0;
    win_count++;
    wm_focus_window(win);
    bg_dirty = true;
    wm_invalidate_all();
    return win;
}

void wm_destroy_window(Window *win)
{
    if (!win) return;
    win->visible = false;
    win->state = WM_STATE_MINIMIZED;
    bg_dirty = true;
    if (focus_win == win) {
        focus_win = 0;
        int bz = -1; Window *bw = 0;
        for (int i = 0; i < win_count; i++) {
            if (windows[i].visible &&
                windows[i].state != WM_STATE_MINIMIZED &&
                &windows[i] != win &&
                windows[i].z_order > bz) {
                bz = windows[i].z_order; bw = &windows[i];
            }
        }
        if (bw) wm_focus_window(bw);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   WINDOW MANIPULATION
   ═══════════════════════════════════════════════════════════════════════ */
void wm_focus_window(Window *win)
{
    if (!win || !win->visible) return;
    if (focus_win && focus_win != win) {
        focus_win->focused = false;
        focus_win->needs_redraw = true;
    }
    win->focused = true;
    win->needs_redraw = true;
    focus_win = win;
    wm_bring_to_front(win);
    wm_invalidate_all();
}

void wm_clear_focus(void)
{
    if (focus_win) {
        focus_win->focused = false;
        focus_win->needs_redraw = true;
    }
    focus_win = 0;
}

void wm_minimize_window(Window *win)
{
    if (!win) return;
    win->state = WM_STATE_MINIMIZED;
    bg_dirty = true;
    wm_invalidate_windows();
    if (focus_win == win) {
        focus_win = 0;
        int bz = -1; Window *bw = 0;
        for (int i = 0; i < win_count; i++) {
            if (windows[i].visible &&
                windows[i].state != WM_STATE_MINIMIZED &&
                &windows[i] != win &&
                windows[i].z_order > bz) {
                bz = windows[i].z_order; bw = &windows[i];
            }
        }
        if (bw) wm_focus_window(bw);
    }
}

void wm_maximize_window(Window *win)
{
    if (!win) return;
    if (win->state == WM_STATE_MAXIMIZED) {
        wm_restore_window(win); return;
    }
    win->saved_x = win->x;  win->saved_y = win->y;
    win->saved_w = win->w;  win->saved_h = win->h;
    int top = (g_settings.window_style == 0) ? WM_MENUBAR_HEIGHT : 0;
    win->x = 0;  win->y = top;
    win->w = scr_w;
    win->h = scr_h - WM_TASKBAR_HEIGHT - top;
    win->state = WM_STATE_MAXIMIZED;
    win->needs_redraw = true;
    bg_dirty = true;
    wm_invalidate_all();
}

void wm_restore_window(Window *win)
{
    if (!win) return;
    if (win->state == WM_STATE_MAXIMIZED) {
        win->x = win->saved_x;  win->y = win->saved_y;
        win->w = win->saved_w;  win->h = win->saved_h;
    }
    win->state = WM_STATE_NORMAL;
    win->needs_redraw = true;
    bg_dirty = true;
    wm_invalidate_all();
}

void wm_bring_to_front(Window *win)
{
    if (!win) return;
    int oz = win->z_order;
    for (int i = 0; i < win_count; i++)
        if (windows[i].z_order > oz) windows[i].z_order--;
    win->z_order = win_count - 1;
}

/* ═══════════════════════════════════════════════════════════════════════
   CLIENT AREA GEOMETRY
   ═══════════════════════════════════════════════════════════════════════ */
int wm_client_x(Window *w) { return w->x; }
int wm_client_y(Window *w) { return w->is_chromeless ? w->y : w->y + WM_TITLEBAR_HEIGHT; }
int wm_client_w(Window *w) { return w->w; }
int wm_client_h(Window *w) { return w->is_chromeless ? w->h : w->h - WM_TITLEBAR_HEIGHT; }

/* ═══════════════════════════════════════════════════════════════════════
   DRAWING HELPERS (client-relative, clipped)
   ═══════════════════════════════════════════════════════════════════════ */
void wm_draw_pixel(Window *w, int x, int y, uint32_t c)
{
    int sx = wm_client_x(w) + x, sy = wm_client_y(w) + y;
    int cx = wm_client_x(w), cy = wm_client_y(w);
    if (sx >= cx && sx < cx + wm_client_w(w) &&
        sy >= cy && sy < cy + wm_client_h(w))
        gpu_draw_pixel(sx, sy, c);
}

void wm_fill_rect(Window *w, int x, int y, int rw, int rh, uint32_t c)
{
    int cx = wm_client_x(w), cy = wm_client_y(w);
    int cw = wm_client_w(w), ch = wm_client_h(w);
    int sx = cx + x, sy = cy + y, sw = rw, sh = rh;
    if (sx < cx) { sw -= (cx - sx); sx = cx; }
    if (sy < cy) { sh -= (cy - sy); sy = cy; }
    if (sx + sw > cx + cw) sw = cx + cw - sx;
    if (sy + sh > cy + ch) sh = cy + ch - sy;
    if (sw > 0 && sh > 0) gpu_fill_rect(sx, sy, sw, sh, c);
}

void wm_draw_rect(Window *w, int x, int y, int rw, int rh, uint32_t c)
{
    wm_fill_rect(w, x, y, rw, 1, c);
    wm_fill_rect(w, x, y + rh - 1, rw, 1, c);
    wm_fill_rect(w, x, y, 1, rh, c);
    wm_fill_rect(w, x + rw - 1, y, 1, rh, c);
}

void wm_draw_char(Window *w, int x, int y, uint8_t ch,
                  uint32_t fg, uint32_t bg)
{
    int sx = wm_client_x(w) + x, sy = wm_client_y(w) + y;
    int cx = wm_client_x(w), cy = wm_client_y(w);
    if (sx >= cx && sx + 8 <= cx + wm_client_w(w) &&
        sy >= cy && sy + 8 <= cy + wm_client_h(w))
        gpu_draw_char(sx, sy, ch, fg, bg);
}

void wm_draw_string(Window *w, int x, int y, const char *s,
                    uint32_t fg, uint32_t bg)
{
    int px = x;
    while (*s) { wm_draw_char(w, px, y, (uint8_t)*s, fg, bg); px += 8; s++; }
}

void wm_fill_rect_blend(Window *w, int x, int y, int rw, int rh,
                        uint32_t c, uint8_t a)
{
    int cx = wm_client_x(w), cy = wm_client_y(w);
    int cw = wm_client_w(w), ch = wm_client_h(w);
    int sx = cx + x, sy = cy + y, sw = rw, sh = rh;
    if (sx < cx) { sw -= (cx - sx); sx = cx; }
    if (sy < cy) { sh -= (cy - sy); sy = cy; }
    if (sx + sw > cx + cw) sw = cx + cw - sx;
    if (sy + sh > cy + ch) sh = cy + ch - sy;
    if (sw > 0 && sh > 0) gpu_fill_rect_blend(sx, sy, sw, sh, c, a);
}

void wm_draw_rounded_rect(Window *w, int x, int y, int rw, int rh,
                          uint32_t c, int r)
{
    rrect(wm_client_x(w) + x, wm_client_y(w) + y, rw, rh, c, r);
}

/* ═══════════════════════════════════════════════════════════════════════
   Z-ORDER / HIT TEST
   ═══════════════════════════════════════════════════════════════════════ */
static Window *at_z(int z)
{
    for (int i = 0; i < win_count; i++)
        if (windows[i].z_order == z) return &windows[i];
    return 0;
}

static Window *win_at(int px, int py)
{
    Window *r = 0; int bz = -1;
    for (int i = 0; i < win_count; i++) {
        Window *w = &windows[i];
        if (!w->visible || w->state == WM_STATE_MINIMIZED) continue;
        if (px >= w->x && px < w->x + w->w &&
            py >= w->y && py < w->y + w->h && w->z_order > bz) {
            bz = w->z_order; r = w;
        }
    }
    return r;
}

/* ═══════════════════════════════════════════════════════════════════════
   PREMIUM WINDOW DRAWING
   ═══════════════════════════════════════════════════════════════════════ */
static void draw_window(Window *win)
{
    if (!win->visible || win->state == WM_STATE_MINIMIZED)
        return;

    if (win->is_chromeless) {
        if (win->on_draw) win->on_draw(win);
        return;
    }

    bool focused = (win == focus_win);
    bool is_max  = (win->state == WM_STATE_MAXIMIZED);
    int  cr      = (g_settings.window_style == 0 && !is_max)
                       ? WM_CORNER_RADIUS : 0;
    bool glass   = g_settings.dock_transparent;

    /* ─── 1. Premium multi-layer shadow ──────────────────── */
    if (!is_max)
        draw_shadow(win->x, win->y, win->w, win->h, focused, cr);

    /* ─── 2. Window body with vibrancy ───────────────────── */
    if (glass && focused)
    {
        /* Focused: full glass effect with blur */
        gpu_blur_rect(win->x, win->y, win->w, win->h);

        /* Vibrancy tint over blur */
        if (cr > 0)
            rrect_a(win->x, win->y, win->w, win->h,
                    MAT_DARK_BASE, cr, 175);
        else
            gpu_fill_rect_blend(win->x, win->y, win->w, win->h,
                                MAT_DARK_BASE, 175);

        /* Top highlight gradient (frosted shine) */
        for (int i = 0; i < 40 && i < win->h; i++) {
            uint8_t a = 50 - i;
            if (a > 0) {
                int ins = (i < cr) ? corner_inset(cr, i) : 0;
                int sw = win->w - 2 * ins;
                if (sw > 0)
                    gpu_fill_rect_blend(win->x + ins, win->y + i, sw, 1,
                                        0xFFFFFFFF, a);
            }
        }
    }
    else
    {
        /* Unfocused or opaque: solid material with subtle gradient */
        uint32_t body_top = focused ? MAT_DARK_ELEVATED : MAT_DARK_TINT;
        uint32_t body_bot = focused ? MAT_DARK_TINT : MAT_DARK_BASE;

        if (cr > 0) {
            /* Draw gradient body with rounded corners */
            int bh = win->h;
            for (int row = 0; row < bh; row++) {
                uint8_t t = (row * 255) / (bh > 1 ? bh - 1 : 1);
                uint32_t c = lerp_color(body_top, body_bot, t);
                int ins = 0;
                if (row < cr) ins = corner_inset(cr, row);
                else if (row >= bh - cr) ins = corner_inset(cr, bh - 1 - row);
                int sw = win->w - 2 * ins;
                if (sw > 0) gpu_fill_rect(win->x + ins, win->y + row, sw, 1, c);
            }
        } else {
            rrect_gradient(win->x, win->y, win->w, win->h, body_top, body_bot, 0);
        }
    }

    /* ─── 3. Subtle border highlight (1px top rim) ───────── */
    if (cr > 0) {
        int ins0 = corner_inset(cr, 0);
        int sw0 = win->w - 2 * ins0;
        if (sw0 > 0)
            gpu_fill_rect_blend(win->x + ins0, win->y, sw0, 1,
                                0xFFFFFFFF, focused ? 60 : 30);
    } else {
        gpu_fill_rect_blend(win->x, win->y, win->w, 1,
                            0xFFFFFFFF, focused ? 60 : 30);
    }

    /* ─── 4. Titlebar (subtle overlay) ───────────────────── */
    {
        /* Gradient overlay on titlebar for depth */
        uint32_t tb_top = focused ? 0xFF404044 : 0xFF323236;
        uint32_t tb_bot = focused ? 0xFF353539 : 0xFF2A2A2E;
        uint8_t tb_alpha = glass && focused ? 100 : 230;

        if (cr > 0) {
            int tbh = WM_TITLEBAR_HEIGHT;
            for (int row = 0; row < tbh; row++) {
                uint8_t t = (row * 255) / (tbh - 1);
                uint32_t c = lerp_color(tb_top, tb_bot, t);
                int ins = (row < cr) ? corner_inset(cr, row) : 0;
                int sw = win->w - 2 * ins;
                if (sw > 0) {
                    if (glass && focused)
                        gpu_fill_rect_blend(win->x + ins, win->y + row, sw, 1, c, tb_alpha);
                    else
                        gpu_fill_rect(win->x + ins, win->y + row, sw, 1, c);
                }
            }
        } else {
            rrect_gradient(win->x, win->y, win->w, WM_TITLEBAR_HEIGHT, tb_top, tb_bot, 0);
        }
    }

    /* ─── 5. Separator line (subtle) ─────────────────────── */
    gpu_fill_rect_blend(win->x, win->y + WM_TITLEBAR_HEIGHT - 1,
                        win->w, 1, 0xFF000000, 90);
    gpu_fill_rect_blend(win->x, win->y + WM_TITLEBAR_HEIGHT,
                        win->w, 1, 0xFFFFFFFF, focused ? 25 : 12);

    /* ─── 6. Title text with shadow ──────────────────────── */
    {
        int tlen = slen(win->title);
        int tx = win->x + (win->w - tlen * 8) / 2;
        int ty = win->y + (WM_TITLEBAR_HEIGHT - 8) / 2;

        if (g_settings.window_style == 0) {
            int min_tx = win->x + WM_TL_LEFT_PAD + WM_TL_SPACING * 3 + 12;
            if (tx < min_tx) tx = min_tx;
        }

        /* Subtle text shadow for depth */
        if (focused)
            gpu_draw_string(tx, ty + 1, (const uint8_t *)win->title,
                            0xFF000000, 0);

        uint32_t tfg = focused ? TEXT_PRIMARY : TEXT_SECONDARY;
        gpu_draw_string(tx, ty, (const uint8_t *)win->title, tfg, 0);
    }

    /* ─── 7. Window buttons ──────────────────────────────── */
    if (g_settings.window_style == 0)
        draw_traffic_lights(win, mouse_get_x(), mouse_get_y());
    else
        draw_win_buttons(win);

    /* ─── 8. Client area ─────────────────────────────────── */
    {
        int cx = wm_client_x(win);
        int cy = wm_client_y(win);
        int cw = wm_client_w(win);
        int ch = wm_client_h(win);

        if (glass && focused)
        {
            /* Translucent client over blurred bg */
            uint32_t ct = WM_COLOR_WINDOW_BG;
            uint8_t  ca = 210;
            if (cr > 0 && ch > cr) {
                gpu_fill_rect_blend(cx, cy, cw, ch - cr, ct, ca);
                rrect_bot_a(cx, cy + ch - cr, cw, cr, ct, cr, ca);
            } else {
                gpu_fill_rect_blend(cx, cy, cw, ch, ct, ca);
            }
        }
        else
        {
            if (cr > 0 && ch > cr) {
                gpu_fill_rect(cx, cy, cw, ch - cr, WM_COLOR_WINDOW_BG);
                rrect_bot(cx, cy + ch - cr, cw, cr, WM_COLOR_WINDOW_BG, cr);
            } else {
                gpu_fill_rect(cx, cy, cw, ch, WM_COLOR_WINDOW_BG);
            }
        }
    }

    /* ─── 9. App content ─────────────────────────────────── */
    if (win->on_draw) win->on_draw(win);

    win->needs_redraw = false;
}

/* ═══════════════════════════════════════════════════════════════════════
   DESKTOP BACKGROUND
   ═══════════════════════════════════════════════════════════════════════ */
void wm_invalidate_bg(void) {
    bg_dirty = true;
    wm_bg_dirty = true;
    wm_full_redraw = true;
}

void wm_invalidate_dock(void) { wm_dock_dirty = true; }
void wm_invalidate_windows(void) { wm_windows_dirty = true; }
void wm_invalidate_cursor(void) { wm_cursor_dirty = true; }

void wm_invalidate_all(void) {
    wm_full_redraw = true;
    wm_bg_dirty = true;
    wm_dock_dirty = true;
    wm_windows_dirty = true;
    wm_cursor_dirty = true;
}

static void wm_cache_dock_bg(int x, int y, int w, int h) {
    if (w > MAX_DOCK_CACHE_W || h > MAX_DOCK_CACHE_H) return;
    uint32_t* backbuffer = gpu_get_backbuffer();
    if (!backbuffer) return;
    int screen_w = gpu_get_width();
    int screen_h = gpu_get_height();
    for (int cy = 0; cy < h && (y + cy) < screen_h; cy++) {
        for (int cx = 0; cx < w && (x + cx) < screen_w; cx++) {
            wm_dock_bg_cache[cy * w + cx] = backbuffer[(y + cy) * screen_w + (x + cx)];
        }
    }
    wm_dock_bg_x = x; wm_dock_bg_y = y;
    wm_dock_bg_w = w; wm_dock_bg_h = h;
    wm_dock_bg_cached = true;
}

static void wm_restore_dock_bg(void) {
    if (!wm_dock_bg_cached) return;
    uint32_t* backbuffer = gpu_get_backbuffer();
    if (!backbuffer) return;
    int screen_w = gpu_get_width();
    int screen_h = gpu_get_height();
    for (int cy = 0; cy < wm_dock_bg_h && (wm_dock_bg_y + cy) < screen_h; cy++) {
        for (int cx = 0; cx < wm_dock_bg_w && (wm_dock_bg_x + cx) < screen_w; cx++) {
            backbuffer[(wm_dock_bg_y + cy) * screen_w + (wm_dock_bg_x + cx)] = wm_dock_bg_cache[cy * wm_dock_bg_w + cx];
        }
    }
}

void wm_draw_desktop_bg(void)
{
    if (!wm_bg_dirty && !wm_full_redraw && !g_settings.dock_transparent) return;

    bg_dirty = false;
    wm_bg_dirty = false;
    wm_windows_dirty = true;
    if (!wm_full_redraw) wm_dock_dirty = true;

    if (g_wallpaper.valid) {
        wm_render_wallpaper(scr_w, scr_h);
        return;
    }

    /* PREMIUM gradient background with color richness */
    int dh = scr_h;
    int bands = 32;
    int bh = dh / bands;
    if (bh < 1) bh = 1;

    /* Rich dark gradient (deep navy to purple-black) */
    uint32_t top_color = 0xFF1A1A2E;
    uint32_t mid_color = 0xFF16213E;
    uint32_t bot_color = 0xFF0F0F1E;

    for (int i = 0; i < bands; i++) {
        int y = i * bh;
        int h = (i == bands - 1) ? (dh - y) : bh;
        uint8_t t = (i * 255) / bands;
        uint32_t c;
        if (t < 128) c = lerp_color(top_color, mid_color, t * 2);
        else c = lerp_color(mid_color, bot_color, (t - 128) * 2);
        gpu_fill_rect(0, y, scr_w, h, c);
    }

    /* Subtle radial glow center-top */
    int gx = scr_w / 2;
    int gy = scr_h / 3;
    for (int r = 400; r > 0; r -= 40) {
        uint8_t a = 3 + (400 - r) / 20;
        gpu_fill_rect_blend(gx - r, gy - r/2, r * 2, r, ACCENT_BLUE, a);
    }

    /* Centered premium branding */
    const char *label = "Aurion OS";
    int lx = (scr_w - slen(label) * 8) / 2;
    int ly = dh / 2 - 20;
    gpu_draw_string(lx + 1, ly + 1, (const uint8_t *)label, 0xFF000000, 0);
    gpu_draw_string(lx,     ly,     (const uint8_t *)label, TEXT_PRIMARY, 0);
}

/* ═══════════════════════════════════════════════════════════════════════
   MENU BAR
   ═══════════════════════════════════════════════════════════════════════ */
void wm_draw_menubar(void)
{
    extern void menubar_draw(void);
    menubar_draw();
}

/* ═══════════════════════════════════════════════════════════════════════
   PREMIUM DOCK — with glow, reflection, and smooth animations
   ═══════════════════════════════════════════════════════════════════════ */
void wm_dock_add(const char *name, icon_draw_fn fn)
{
    if (dock_cnt >= WM_MAX_DOCK_SLOTS) return;
    int i = 0;
    while (name[i] && i < 63) { dock_names[dock_cnt][i] = name[i]; i++; }
    dock_names[dock_cnt][i] = 0;
    dock_fns[dock_cnt] = fn;
    dock_sz[dock_cnt] = WM_DOCK_ICON_BASE;
    dock_bounce[dock_cnt] = 0;
    dock_cnt++;
}

void wm_dock_clear(void) { dock_cnt = 0; dock_hover = -1; }

int wm_dock_slot_count(void) { return dock_cnt; }

static bool dock_is_open(const char *n)
{
    for (int i = 0; i < win_count; i++) {
        if (!windows[i].visible ||
            windows[i].state == WM_STATE_MINIMIZED) continue;
        if (seq(windows[i].title, n)) return true;
    }
    return false;
}

static void dock_layout(int *out_w, int *out_h, int *out_x, int *out_y)
{
    int tw = 0;
    for (int i = 0; i < dock_cnt; i++) tw += dock_sz[i];
    tw += (dock_cnt - 1) * WM_DOCK_PAD + 32;
    int dh = WM_DOCK_PILL_H;
    int dx = (scr_w - tw) / 2;
    int dy = scr_h - dh - WM_DOCK_BOTTOM_GAP;
    *out_w = tw;  *out_h = dh;  *out_x = dx;  *out_y = dy;
}

static void dock_animate(void)
{
    if (dock_hover < 0 && !g_settings.dock_magnification) {
        for (int i = 0; i < dock_cnt; i++)
            dock_sz[i] = WM_DOCK_ICON_BASE;
        return;
    }

    for (int i = 0; i < dock_cnt; i++) {
        int target = WM_DOCK_ICON_BASE;
        if (g_settings.dock_magnification && dock_hover >= 0) {
            int d = ab(i - dock_hover);
            if (d == 0)
                target = WM_DOCK_ICON_MAG;
            else if (d == 1)
                target = WM_DOCK_ICON_BASE +
                         (WM_DOCK_ICON_MAG - WM_DOCK_ICON_BASE) * 7 / 10;
            else if (d == 2)
                target = WM_DOCK_ICON_BASE +
                         (WM_DOCK_ICON_MAG - WM_DOCK_ICON_BASE) * 3 / 10;
        }
        int diff = target - dock_sz[i];
        if (diff) {
            /* Smoother easing */
            int step = diff / 4;
            if (!step) step = diff > 0 ? 1 : -1;
            dock_sz[i] += step;
            wm_dock_dirty = true;
        }
    }
}

static void dock_slot(int sx, int sy, int sz, icon_draw_fn fn,
                      bool open, bool hov)
{
    /* Hover glow behind icon */
    if (hov) {
        for (int g = 6; g > 0; g -= 2) {
            uint8_t a = 25 - g * 3;
            rrect_a(sx - g, sy - g, sz + g * 2, sz + g * 2,
                    0xFFFFFFFF, 14 + g, a);
        }
    }

    if (fn) {
        int icon_sz = WM_DOCK_ICON_BASE;
        int ix = sx + (sz - icon_sz) / 2;
        int iy = sy + (sz - icon_sz) / 2;
        fn(ix, iy);
    }

    /* Premium indicator dot for running apps */
    if (open) {
        int ix = sx + sz / 2;
        int iy = sy + sz + 6;
        /* Glow */
        circ_a(ix, iy, 3, ACCENT_GLOW, 80);
        /* Core dot */
        circ(ix, iy, 2, 0xFFFFFFFF);
    }
}

static void dock_tooltip(int cx, int top_y, const char *name)
{
    int len = slen(name);
    int tw  = len * 8 + 20;
    int th  = 26;
    int tx  = cl(cx - tw / 2, 2, scr_w - tw - 2);
    int ty  = top_y - th - 14;

    /* Shadow below tooltip */
    rrect_a(tx + 2, ty + 3, tw, th, 0xFF000000, 13, 100);

    /* Tooltip body - very dark with subtle border */
    rrect_a(tx, ty, tw, th, 0xFF0A0A0F, 13, 245);

    /* Top shine */
    gpu_fill_rect_blend(tx + 10, ty, tw - 20, 1, 0xFFFFFFFF, 40);

    /* Border */
    rrect_a(tx, ty, tw, th, 0xFFFFFFFF, 13, 15);

    gpu_draw_string(tx + 10, ty + 9,
                    (const uint8_t *)name, TEXT_PRIMARY, 0);
}

void wm_draw_taskbar(void)
{
    if (focus_win && focus_win->is_chromeless) return;
    if (dock_cnt == 0) return;

    dock_animate();
    wm_anim_tick++;

    if (!wm_dock_dirty && !wm_full_redraw) return;

    int tw, dh, dx, dy;
    dock_layout(&tw, &dh, &dx, &dy);

    int radius = WM_DOCK_CORNER;

    /* ── PREMIUM DOCK PILL ──────────────────────────────── */

    /* Shadow beneath dock */
    gpu_fill_rect_blend(dx - 6, dy + dh - 4, tw + 12, 12, 0xFF000000, 40);
    gpu_fill_rect_blend(dx - 2, dy + dh, tw + 4, 8, 0xFF000000, 60);

    if (g_settings.dock_transparent)
    {
        if (vmware_svga_available()) {
            /* Full glass with blur */
            gpu_blur_rect(dx, dy, tw, dh);

            /* Gradient glass tint */
            uint32_t glass_top = 0xFF2A2A30;
            uint32_t glass_bot = 0xFF18181C;
            for (int row = 0; row < dh; row++) {
                uint8_t t = (row * 255) / (dh - 1);
                uint32_t c = lerp_color(glass_top, glass_bot, t);
                int ins = 0;
                if (row < radius) ins = corner_inset(radius, row);
                else if (row >= dh - radius) ins = corner_inset(radius, dh - 1 - row);
                int sw = tw - 2 * ins;
                if (sw > 0)
                    gpu_fill_rect_blend(dx + ins, dy + row, sw, 1, c, 180);
            }

            /* Top shine */
            gpu_fill_rect_blend(dx + radius, dy,
                                tw - 2 * radius, 1, 0xFFFFFFFF, 60);
            gpu_fill_rect_blend(dx + radius, dy + 1,
                                tw - 2 * radius, 1, 0xFFFFFFFF, 25);

            /* Side highlights */
            gpu_fill_rect_blend(dx, dy + radius, 1,
                                dh - 2 * radius, 0xFFFFFFFF, 20);
            gpu_fill_rect_blend(dx + tw - 1, dy + radius, 1,
                                dh - 2 * radius, 0xFFFFFFFF, 20);

            /* Bottom shadow inside */
            gpu_fill_rect_blend(dx + radius, dy + dh - 1,
                                tw - 2 * radius, 1, 0xFF000000, 100);
        } else {
            /* QEMU-safe path with gradient */
            uint32_t glass_top = 0xFF22222A;
            uint32_t glass_bot = 0xFF0E0E14;
            for (int row = 0; row < dh; row++) {
                uint8_t t = (row * 255) / (dh - 1);
                uint32_t c = lerp_color(glass_top, glass_bot, t);
                int ins = 0;
                if (row < radius) ins = corner_inset(radius, row);
                else if (row >= dh - radius) ins = corner_inset(radius, dh - 1 - row);
                int sw = tw - 2 * ins;
                if (sw > 0)
                    gpu_fill_rect_blend(dx + ins, dy + row, sw, 1, c, 215);
            }
            gpu_fill_rect_blend(dx + radius, dy,
                                tw - 2 * radius, 1, 0xFFFFFFFF, 50);
        }
    }
    else
    {
        /* Opaque gradient dock */
        rrect_gradient(dx, dy, tw, dh, 0xFF1E1E24, 0xFF0A0A0E, radius);
        gpu_fill_rect(dx + radius, dy, tw - 2 * radius, 1, 0xFF333338);
    }

    /* ── Icons with premium hover effects ────────────────── */
    int sx = dx + 16;
    for (int i = 0; i < dock_cnt; i++) {
        int sz = dock_sz[i];
        int sy = dy + (dh - sz) / 2;
        if (sy < 0) sy = 0;
        if (sy + sz > scr_h) sy = scr_h - sz;
        bool hov = (i == dock_hover);
        bool op  = dock_is_open(dock_names[i]);

        dock_slot(sx, sy, sz, dock_fns[i], op, hov);

        if (hov && tip_show)
            dock_tooltip(sx + sz / 2, dy, dock_names[i]);

        sx += sz + WM_DOCK_PAD;
    }

    /* ── Clock (Windows-style only) ─────────────────────── */
    if (g_settings.window_style != 0) {
        uint8_t hh, mm, ss;
        sys_get_time(&hh, &mm, &ss);
        char clk[9];
        clk[0] = '0' + hh / 10; clk[1] = '0' + hh % 10; clk[2] = ':';
        clk[3] = '0' + mm / 10; clk[4] = '0' + mm % 10; clk[5] = ':';
        clk[6] = '0' + ss / 10; clk[7] = '0' + ss % 10; clk[8] = 0;
        rrect_a(scr_w - 104, 6, 98, 26, 0xFF0A0A12, 13, 200);
        gpu_fill_rect_blend(scr_w - 100, 6, 90, 1, 0xFFFFFFFF, 30);
        gpu_draw_string(scr_w - 94, 13,
                        (const uint8_t *)clk, TEXT_PRIMARY, 0);
    }

    wm_dock_dirty = false;
}

bool wm_dock_handle(int mmx, int mmy, bool click)
{
    if (dock_cnt == 0) return false;

    int tw, dh, dx, dy;
    dock_layout(&tw, &dh, &dx, &dy);

    int ext = g_settings.dock_magnification ? 20 : 4;
    if (mmx < dx - ext || mmx > dx + tw + ext ||
        mmy < dy - ext || mmy > dy + dh + ext) {
        if (dock_hover != -1) {
            dock_hover = -1; tip_show = false; tip_slot = -1;
            wm_dock_dirty = true;
        }
        return false;
    }

    int sx = dx + 16;
    int found = -1;
    for (int i = 0; i < dock_cnt; i++) {
        int sz = dock_sz[i];
        int sy = dy + (dh - sz) / 2;
        if (mmx >= sx && mmx < sx + sz && mmy >= sy && mmy < sy + sz) {
            found = i;
            break;
        }
        sx += sz + WM_DOCK_PAD;
    }

    if (found >= 0) {
        if (dock_hover != found) {
            wm_dock_dirty = true;
            dock_hover = found;
        }
        if (found != tip_slot) {
            tip_slot = found; tip_tick = get_ticks(); tip_show = false;
        } else if (!tip_show && (get_ticks() - tip_tick) > 30) {
            tip_show = true;
            wm_dock_dirty = true;
        }
        if (click) dock_clicked_slot = found;
        return true;
    }

    if (dock_hover != -1) {
        dock_hover = -1; tip_show = false; tip_slot = -1;
        wm_dock_dirty = true;
    }
    return false;
}

/* ═══════════════════════════════════════════════════════════════════════
   PREMIUM CURSOR
   ═══════════════════════════════════════════════════════════════════════ */
void wm_draw_cursor(int mmx, int mmy)
{
    if (mmx != wm_prev_cursor_x || mmy != wm_prev_cursor_y) {
        wm_cursor_dirty = true;
        wm_prev_cursor_x = mmx;
        wm_prev_cursor_y = mmy;
    }
    static const uint8_t cur[16][11] = {
        {2,2,0,0,0,0,0,0,0,0,0},
        {2,1,2,0,0,0,0,0,0,0,0},
        {2,1,1,2,0,0,0,0,0,0,0},
        {2,1,1,1,2,0,0,0,0,0,0},
        {2,1,1,1,1,2,0,0,0,0,0},
        {2,1,1,1,1,1,2,0,0,0,0},
        {2,1,1,1,1,1,1,2,0,0,0},
        {2,1,1,1,1,1,1,1,2,0,0},
        {2,1,1,1,1,1,1,1,1,2,0},
        {2,1,1,1,1,1,2,2,2,2,2},
        {2,1,1,2,1,1,2,0,0,0,0},
        {2,1,2,0,2,1,1,2,0,0,0},
        {2,2,0,0,2,1,1,2,0,0,0},
        {0,0,0,0,0,2,1,1,2,0,0},
        {0,0,0,0,0,2,1,1,2,0,0},
        {0,0,0,0,0,0,2,2,0,0,0},
    };
    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 11; dx++) {
            uint8_t v = cur[dy][dx];
            if (!v) continue;
            int px = mmx + dx, py = mmy + dy;
            if (px < 0 || px >= scr_w || py < 0 || py >= scr_h) continue;
            gpu_draw_pixel(px, py, v == 1 ? 0xFF000000 : 0xFFFFFFFF);
        }
    }

    wm_cursor_dirty = false;
    wm_full_redraw = false;
}

/* ═══════════════════════════════════════════════════════════════════════
   INPUT HANDLING (unchanged logic, just clean)
   ═══════════════════════════════════════════════════════════════════════ */
static int tl_hit(Window *w, int px, int py)
{
    int cy  = w->y + WM_TITLEBAR_HEIGHT / 2;
    int cx0 = w->x + WM_TL_LEFT_PAD;
    int cx1 = cx0 + WM_TL_SPACING;
    int cx2 = cx1 + WM_TL_SPACING;
    int hr2 = (WM_TL_RADIUS + 4) * (WM_TL_RADIUS + 4);
    int d;
    d = (px-cx0)*(px-cx0) + (py-cy)*(py-cy); if (d <= hr2) return 0;
    d = (px-cx1)*(px-cx1) + (py-cy)*(py-cy); if (d <= hr2) return 1;
    d = (px-cx2)*(px-cx2) + (py-cy)*(py-cy); if (d <= hr2) return 2;
    return -1;
}

static int wb_hit(Window *w, int px, int py)
{
    int bh = WM_WIN_BTN_H, bw = WM_WIN_BTN_W;
    int by = w->y + (WM_TITLEBAR_HEIGHT - bh) / 2;
    if (py < by || py >= by + bh) return -1;
    int cx = w->x + w->w - bw - 4;
    if (px >= cx && px < cx + bw) return 0;
    cx -= bw + 2;
    if (px >= cx && px < cx + bw) return 2;
    cx -= bw + 2;
    if (px >= cx && px < cx + bw) return 1;
    return -1;
}

void wm_handle_input(void)
{
    int  mmx     = mouse_get_x();
    int  mmy     = mouse_get_y();
    bool lmb     = mouse_get_left();
    bool rmb     = mouse_get_right();
    bool click   = lmb && !prev_lmb;
    bool release = !lmb && prev_lmb;
    bool rclick  = rmb && !prev_rmb;

    int tbar_y = scr_h - WM_TASKBAR_HEIGHT;
    int min_wy = (g_settings.window_style == 0) ? WM_MENUBAR_HEIGHT : 0;

    if (dragging && drag_win) {
        if (lmb) {
            drag_tx = mmx - drag_ox;
            drag_ty = cl(mmy - drag_oy, min_wy,
                         tbar_y - WM_TITLEBAR_HEIGHT);
            int dx = drag_tx - drag_win->x;
            int dy = drag_ty - drag_win->y;
            int sx = dx * 3 / 5;
            int sy = dy * 3 / 5;
            if (!sx && dx) sx = dx > 0 ? 1 : -1;
            if (!sy && dy) sy = dy > 0 ? 1 : -1;
            drag_win->x += sx;
            drag_win->y += sy;
            if (ab(drag_win->x - drag_tx) <= 1) drag_win->x = drag_tx;
            if (ab(drag_win->y - drag_ty) <= 1) drag_win->y = drag_ty;
            wm_bg_dirty = true;
            bg_dirty = true;
            wm_invalidate_windows();
        } else {
            drag_win->x = drag_tx;
            drag_win->y = drag_ty;
            dragging = false; drag_win = 0;
        }
        prev_lmb = lmb; prev_rmb = rmb; return;
    }

    if (resizing && rsz_win) {
        if (lmb) {
            if (rsz_edge & 1) {
                int nw = mmx - rsz_win->x;
                if (nw >= rsz_win->min_w) rsz_win->w = nw;
            }
            if (rsz_edge & 2) {
                int nh = mmy - rsz_win->y;
                if (nh >= rsz_win->min_h &&
                    rsz_win->y + nh < tbar_y) rsz_win->h = nh;
            }
            rsz_win->needs_redraw = true;
            bg_dirty = true;
            wm_invalidate_windows();
        } else {
            resizing = false; rsz_win = 0;
        }
        prev_lmb = lmb; prev_rmb = rmb; return;
    }

    if (click) {
        if (mmy >= tbar_y) {
            prev_lmb = lmb; prev_rmb = rmb; return;
        }
        if (g_settings.window_style == 0 && mmy < WM_MENUBAR_HEIGHT) {
            prev_lmb = lmb; prev_rmb = rmb; return;
        }

        Window *cw = win_at(mmx, mmy);
        if (cw) {
            wm_focus_window(cw);

            uint32_t now = get_ticks();
            bool dbl = (cw == dbl_win && (now - dbl_tick) < 40);
            dbl_tick = now; dbl_win = cw;

            if (mmy >= cw->y && mmy < cw->y + WM_TITLEBAR_HEIGHT) {
                if (dbl) {
                    if (cw->state == WM_STATE_MAXIMIZED)
                        wm_restore_window(cw);
                    else
                        wm_maximize_window(cw);
                    dbl_tick = 0;
                    prev_lmb = lmb; prev_rmb = rmb; return;
                }

                int btn = g_settings.window_style == 0
                              ? tl_hit(cw, mmx, mmy)
                              : wb_hit(cw, mmx, mmy);
                if (btn >= 0) {
                    if (btn == 0) {
                        if (cw->on_close) cw->on_close(cw);
                        wm_destroy_window(cw);
                    } else if (btn == 1) {
                        wm_minimize_window(cw);
                    } else if (btn == 2) {
                        wm_maximize_window(cw);
                    }
                    prev_lmb = lmb; prev_rmb = rmb; return;
                }

                if (cw->state != WM_STATE_MAXIMIZED) {
                    dragging = true; drag_win = cw;
                    drag_ox = mmx - cw->x; drag_oy = mmy - cw->y;
                    drag_tx = cw->x; drag_ty = cw->y;
                    prev_lmb = lmb; prev_rmb = rmb; return;
                }
            }

            if (cw->state != WM_STATE_MAXIMIZED) {
                int edge = 0;
                if (mmx >= cw->x + cw->w - 6) edge |= 1;
                if (mmy >= cw->y + cw->h - 6) edge |= 2;
                if (edge) {
                    resizing = true; rsz_edge = edge; rsz_win = cw;
                    prev_lmb = lmb; prev_rmb = rmb; return;
                }
            }

            if (cw->on_mouse)
                cw->on_mouse(cw, mmx - wm_client_x(cw),
                             mmy - wm_client_y(cw), true, false);
            wm_invalidate_windows();
        }
    }

    if (rclick) {
        Window *rw = win_at(mmx, mmy);
        if (rw && rw->on_mouse && mmy >= wm_client_y(rw)) {
            wm_focus_window(rw);
            rw->on_mouse(rw, mmx - wm_client_x(rw),
                         mmy - wm_client_y(rw), false, true);
            wm_invalidate_windows();
        }
    }

    if (lmb && focus_win && focus_win->on_mouse &&
        !dragging && !resizing) {
        int lx = mmx - wm_client_x(focus_win);
        int ly = mmy - wm_client_y(focus_win);
        if (lx >= 0 && ly >= 0 &&
            lx < wm_client_w(focus_win) &&
            ly < wm_client_h(focus_win))
            focus_win->on_mouse(focus_win, lx, ly,
                                lmb, mouse_get_right());
        wm_invalidate_windows();
    }

    if (!lmb && !rmb && focus_win && focus_win->on_mouse &&
        !dragging && !resizing) {
        int lx = mmx - wm_client_x(focus_win);
        int ly = mmy - wm_client_y(focus_win);
        if (lx >= 0 && ly >= 0 &&
            lx < wm_client_w(focus_win) &&
            ly < wm_client_h(focus_win))
            focus_win->on_mouse(focus_win, lx, ly, false, false);
        wm_invalidate_windows();
    }

    if (release && focus_win && focus_win->on_mouse) {
        focus_win->on_mouse(focus_win,
                            mmx - wm_client_x(focus_win),
                            mmy - wm_client_y(focus_win),
                            false, false);
        wm_invalidate_windows();
    }

    while (sys_kb_hit() && focus_win) {
        uint16_t key = sys_getkey();
        if ((key & 0xFF) != 0) {
            char m = keyboard_remap((char)(key & 0xFF));
            key = (key & 0xFF00) | (uint8_t)m;
        }
        if (focus_win->on_key)
            focus_win->on_key(focus_win, key);
        wm_invalidate_windows();
    }

    int zd = mouse_get_z_delta();
    if (zd && focus_win && focus_win->on_key) {
        uint16_t vk = (zd < 0) ? (0x49 << 8) : (0x51 << 8);
        focus_win->on_key(focus_win, vk);
        wm_invalidate_windows();
    }

    prev_lmb = lmb; prev_rmb = rmb;
}

/* ═══════════════════════════════════════════════════════════════════════
   COMPOSITE DRAW
   ═══════════════════════════════════════════════════════════════════════ */
void wm_draw_all(void)
{
    for (int z = 0; z < win_count; z++) {
        Window *w = at_z(z);
        if (w) draw_window(w);
    }
    if (!wm_is_installer_mode())
        wm_draw_menubar();
}

/* ═══════════════════════════════════════════════════════════════════════
   LIGHT UPDATE
   ═══════════════════════════════════════════════════════════════════════ */
void wm_update_light(void)
{
    for (int i = 0; i < 5; i++) mouse_poll();

    while (sys_kb_hit() && focus_win) {
        uint16_t key = sys_getkey();
        if ((key & 0xFF) != 0) {
            char m = keyboard_remap((char)(key & 0xFF));
            key = (key & 0xFF00) | (uint8_t)m;
        }
        if (focus_win->on_key)
            focus_win->on_key(focus_win, key);
    }

    int mmx = mouse_get_x(), mmy = mouse_get_y();
    mouse_get_z_delta();

    wm_draw_desktop_bg();
    wm_draw_all();
    wm_draw_taskbar();
    wm_draw_cursor(mmx, mmy);
    gpu_flush();
}

/* ═══════════════════════════════════════════════════════════════════════
   GETTERS
   ═══════════════════════════════════════════════════════════════════════ */
int     wm_get_screen_w(void)     { return scr_w; }
int     wm_get_screen_h(void)     { return scr_h; }
int     wm_get_window_count(void) { return win_count; }
Window *wm_get_focused(void)      { return focus_win; }
Window *wm_get_window(int i)
{
    return (i >= 0 && i < win_count) ? &windows[i] : 0;
}