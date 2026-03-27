/*
 * Window Manager for Aurion OS
 * Handles window creation, drawing, input dispatch, taskbar, and desktop
*/

#include "window_manager.h"
#include "drivers/mouse.h"

/* External GPU/graphics functions */
extern void gpu_clear(uint32_t color);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_fill_rect_blend(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
extern void gpu_blur_rect(int x, int y, int w, int h);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern void gpu_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern int gpu_flush(void);
extern int gpu_get_width(void);
extern int gpu_get_height(void);

/* External kernel functions */
extern uint16_t sys_getkey(void);
extern uint16_t c_getkey_nonblock(void);
extern char keyboard_remap(char c);
extern int sys_kb_hit(void);
extern uint32_t get_ticks(void);
extern int sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);

/* Global settings initialization */
OSSettings g_settings = {
    .bg_color = 0,             /* Blue */
    .window_style = 0,         /* MacOS */
    .startup_app_idx = 0,      /* Terminal */
    .dock_magnification = 1,   /* Enabled */
    .dock_transparent = 1,     /* Transparent (default) */
    .resolution = 6            /* 1920x1080 (Modern high-res display) */
};

/* Forward declarations for filesystem helpers from commands.c */
extern int load_file_content(const char *filename, char *buffer, int max_len);
extern int save_file_content(const char *filename, const char *data, int len);
extern int fs_save_to_disk(void);

void wm_load_settings(void) {
    OSSettings loaded;
    /* Try to load settings from a file. This requires filesystem to be init. */
    int bytes_read = load_file_content("/Settings.dat", (char*)&loaded, sizeof(OSSettings));
    if (bytes_read == sizeof(OSSettings)) {
        /* Loaded successfully - but ensure new features are enabled if not set */
        g_settings = loaded;
        
        /* Migration: If dock_magnification and dock_transparent are both 0, 
           this might be an old settings file - enable them */
        if (g_settings.dock_magnification == 0 && g_settings.dock_transparent == 0) {
            g_settings.dock_magnification = 1;
            g_settings.dock_transparent = 1;
            wm_save_settings(); /* Save migrated settings */
        }
    } else {
        /* No settings file or invalid - save defaults */
        wm_save_settings();
    }
}

void wm_save_settings(void) {
    /* Save settings to filesystem */
    save_file_content("/Settings.dat", (const char*)&g_settings, sizeof(OSSettings));
    fs_save_to_disk();
    
    /* CRITICAL: Write resolution to a dedicated boot sector (LBA 999) so bootloader can read it */
    uint16_t widths[] = {800, 1024, 1280, 1280, 1440, 1600, 1920, 2560};
    uint16_t heights[] = {600, 768, 720, 1024, 900, 900, 1080, 1440};
    int res_idx = g_settings.resolution;
    
    if (res_idx >= 0 && res_idx < 8) {
        char boot_settings[512];
        for (int i = 0; i < 512; i++) boot_settings[i] = 0;
        
        /* Magic signature "BOOT" */
        boot_settings[0] = 'B';
        boot_settings[1] = 'O';
        boot_settings[2] = 'O';
        boot_settings[3] = 'T';
        
        /* Resolution index at offset 4 */
        boot_settings[4] = (char)res_idx;
        
        /* Width and height at offset 8 */
        *(uint16_t*)(boot_settings + 8) = widths[res_idx];
        *(uint16_t*)(boot_settings + 10) = heights[res_idx];
        
        /* Write to LBA 999 (just before filesystem metadata) */
        extern int disk_write_lba(uint32_t lba, uint32_t count, const void *buffer);
        disk_write_lba(999, 1, boot_settings);
    }
}

volatile int desktop_exit_reason = -1;

/* Dock click signal - desktop.c reads this each frame to launch apps */
int dock_clicked_slot = -1;

/* Window list */
static Window windows[WM_MAX_WINDOWS];
static int window_count = 0;
static int screen_w = 1024;
static int screen_h = 768;
static Window *focused_window = 0;

/* Resolution change flag */
static bool resolution_changed = false;

/* Installer mode - disables desktop/dock interaction */
static bool installer_mode = false;

void wm_set_installer_mode(bool enabled) {
    installer_mode = enabled;
}

bool wm_is_installer_mode(void) {
    return installer_mode;
}

/* Drag state */
static bool dragging = false;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static Window *drag_window = 0;

/* Animated drag: target position the window smoothly lerps toward */
static int drag_target_x = 0;
static int drag_target_y = 0;

/* Resize state */
static bool resizing = false;
static int resize_edge = 0; /* 1=right, 2=bottom, 3=corner */
static Window *resize_window = 0;

/* Mouse state tracking */
static bool prev_left = false;

/* Background dirty flag - set true to force a desktop bg redraw next frame */
static bool bg_dirty = true;

static int wm_strlen(const char *s) {
    int l = 0;
    while (s[l]) l++;
    return l;
}

static void wm_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void wm_draw_rounded_rect_blend(int x, int y, int w, int h,
                                        uint32_t color, int r, uint8_t alpha);

void wm_init(int sw, int sh) {
    screen_w = sw;
    screen_h = sh;
    window_count = 0;
    focused_window = 0;
    dragging = false;
    resizing = false;
    desktop_exit_reason = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].visible = false;
        windows[i].state = WM_STATE_NORMAL;
        windows[i].z_order = 0;
        windows[i].on_draw = 0;
        windows[i].on_key = 0;
        windows[i].on_mouse = 0;
        windows[i].on_close = 0;
        windows[i].user_data = 0;
        windows[i].is_chromeless = false;
    }
}

void wm_set_screen_size(int sw, int sh) {
    screen_w = sw;
    screen_h = sh;
    resolution_changed = true;
    
    /* Reposition windows that are off-screen */
    for (int i = 0; i < window_count; i++) {
        Window *win = &windows[i];
        if (!win->visible) continue;
        
        /* Keep windows on screen */
        if (win->x + win->w > sw) win->x = sw - win->w;
        if (win->y + win->h > sh - WM_TASKBAR_HEIGHT) win->y = sh - WM_TASKBAR_HEIGHT - win->h;
        if (win->x < 0) win->x = 0;
        if (win->y < 0) win->y = 0;
    }
}

bool wm_resolution_changed(void) {
    bool changed = resolution_changed;
    resolution_changed = false;
    return changed;
}

Window *wm_create_window(const char *title, int x, int y, int w, int h) {
    if (window_count >= WM_MAX_WINDOWS) return 0;

    Window *win = &windows[window_count];
    wm_strcpy(win->title, title, 64);
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->min_w = 200;
    win->min_h = 120;
    win->saved_x = x;
    win->saved_y = y;
    win->saved_w = w;
    win->saved_h = h;
    win->state = WM_STATE_NORMAL;
    win->visible = true;
    win->focused = false;
    win->z_order = window_count;
    win->app_id = window_count;
    win->on_draw = 0;
    win->on_key = 0;
    win->on_mouse = 0;
    win->on_close = 0;
    win->user_data = 0;
    win->is_chromeless = false;

    window_count++;
    wm_focus_window(win);
    return win;
}

void wm_destroy_window(Window *win) {
    if (!win) return;
    win->visible = false;
    win->state = WM_STATE_MINIMIZED;
    bg_dirty = true; /* window closed - desktop revealed, needs redraw */

    if (focused_window == win) {
        focused_window = 0;
        for (int i = window_count - 1; i >= 0; i--) {
            if (windows[i].visible && windows[i].state != WM_STATE_MINIMIZED) {
                wm_focus_window(&windows[i]);
                break;
            }
        }
    }
}

void wm_focus_window(Window *win) {
    if (!win || !win->visible) return;
    if (focused_window) focused_window->focused = false;
    win->focused = true;
    focused_window = win;
    wm_bring_to_front(win);
}

void wm_minimize_window(Window *win) {
    if (!win) return;
    win->state = WM_STATE_MINIMIZED;
    bg_dirty = true; /* window minimized - desktop revealed */
    if (focused_window == win) {
        focused_window = 0;
        for (int i = window_count - 1; i >= 0; i--) {
            if (windows[i].visible && windows[i].state != WM_STATE_MINIMIZED && &windows[i] != win) {
                wm_focus_window(&windows[i]);
                break;
            }
        }
    }
}

void wm_maximize_window(Window *win) {
    if (!win) return;
    if (win->state == WM_STATE_MAXIMIZED) {
        wm_restore_window(win);
        return;
    }
    win->saved_x = win->x;
    win->saved_y = win->y;
    win->saved_w = win->w;
    win->saved_h = win->h;
    win->x = 0;
    win->y = 0;
    win->w = screen_w;
    win->h = screen_h - WM_TASKBAR_HEIGHT;
    win->state = WM_STATE_MAXIMIZED;
}

void wm_restore_window(Window *win) {
    if (!win) return;
    if (win->state == WM_STATE_MAXIMIZED) {
        win->x = win->saved_x;
        win->y = win->saved_y;
        win->w = win->saved_w;
        win->h = win->saved_h;
    }
    win->state = WM_STATE_NORMAL;
}

void wm_bring_to_front(Window *win) {
    if (!win) return;
    int old_z = win->z_order;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].z_order > old_z) {
            windows[i].z_order--;
        }
    }
    win->z_order = window_count - 1;
}

/* Client area helpers */
int wm_client_x(Window *win) {
  if (win->is_chromeless) return win->x;
  return win->x + WM_BORDER_WIDTH;
}
int wm_client_y(Window *win) {
  if (win->is_chromeless) return win->y;
  return win->y + WM_TITLEBAR_HEIGHT;
}
int wm_client_w(Window *win) {
  if (win->is_chromeless) return win->w;
  return win->w - 2 * WM_BORDER_WIDTH;
}
int wm_client_h(Window *win) {
  if (win->is_chromeless) return win->h;
  return win->h - WM_TITLEBAR_HEIGHT - WM_BORDER_WIDTH;
}

/* Drawing helpers - coordinates relative to window client area */
void wm_draw_pixel(Window *win, int x, int y, uint32_t color) {
    int sx = wm_client_x(win) + x;
    int sy = wm_client_y(win) + y;
    if (sx >= wm_client_x(win) && sx < wm_client_x(win) + wm_client_w(win) &&
        sy >= wm_client_y(win) && sy < wm_client_y(win) + wm_client_h(win)) {
        gpu_draw_pixel(sx, sy, color);
    }
}

void wm_fill_rect(Window *win, int x, int y, int w, int h, uint32_t color) {
    int cx = wm_client_x(win);
    int cy = wm_client_y(win);
    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    int sx = cx + x;
    int sy = cy + y;
    int sw = w;
    int sh = h;

    /* Clip to client area */
    if (sx < cx) { sw -= (cx - sx); sx = cx; }
    if (sy < cy) { sh -= (cy - sy); sy = cy; }
    if (sx + sw > cx + cw) sw = cx + cw - sx;
    if (sy + sh > cy + ch) sh = cy + ch - sy;
    if (sw > 0 && sh > 0) {
        gpu_fill_rect(sx, sy, sw, sh, color);
    }
}

void wm_draw_rect(Window *win, int x, int y, int w, int h, uint32_t color) {
    wm_fill_rect(win, x, y, w, 1, color);
    wm_fill_rect(win, x, y + h - 1, w, 1, color);
    wm_fill_rect(win, x, y, 1, h, color);
    wm_fill_rect(win, x + w - 1, y, 1, h, color);
}

void wm_draw_char(Window *win, int x, int y, uint8_t c, uint32_t fg, uint32_t bg) {
    int sx = wm_client_x(win) + x;
    int sy = wm_client_y(win) + y;
    if (sx >= wm_client_x(win) && sx + 8 <= wm_client_x(win) + wm_client_w(win) &&
        sy >= wm_client_y(win) && sy + 8 <= wm_client_y(win) + wm_client_h(win)) {
        gpu_draw_char(sx, sy, c, fg, bg);
    }
}

void wm_draw_string(Window *win, int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (*str) {
        wm_draw_char(win, cx, y, (uint8_t)*str, fg, bg);
        cx += 8;
        str++;
    }
}

/* Get window at z-order position */
static Window *wm_get_at_z(int z) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].z_order == z) return &windows[i];
    }
    return 0;
}

/* Find window under point */
static Window *wm_window_at(int px, int py) {
    Window *result = 0;
    int highest_z = -1;
    for (int i = 0; i < window_count; i++) {
        if (!windows[i].visible || windows[i].state == WM_STATE_MINIMIZED) continue;
        if (px >= windows[i].x && px < windows[i].x + windows[i].w &&
            py >= windows[i].y && py < windows[i].y + windows[i].h) {
            if (windows[i].z_order > highest_z) {
                highest_z = windows[i].z_order;
                result = &windows[i];
            }
        }
    }
    return result;
}

/* Draw a filled rounded rectangle.
 * For each row in the top/bottom corner bands, compute the exact pixel
 * span using integer circle math and draw two horizontal spans (left + right).
 * The middle band is just a full-width rect - no loops needed. */
static void wm_draw_rounded_window_frame(int x, int y, int w, int h,
                                          uint32_t color, int r) {
    if (r < 1) { gpu_fill_rect(x, y, w, h, color); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    /* Middle band - full width, no rounding needed */
    gpu_fill_rect(x, y + r, w, h - 2 * r, color);

    /* Top and bottom corner bands */
    for (int i = 0; i < r; i++) {
        /* How far from the corner center are we vertically? */
        int vy = r - 1 - i;
        /* Compute horizontal reach: largest vx where vx^2 + vy^2 <= r^2 */
        int vx = 0;
        while ((vx + 1) * (vx + 1) + vy * vy <= r * r) vx++;
        /* The filled span starts at corner center - vx and goes to corner center + vx */
        /* Top corners */
        int top_row = y + i;
        /* Left half: from (x + r - vx) width = vx */
        gpu_fill_rect(x + r - vx, top_row, w - 2 * (r - vx), 1, color);
        /* Bottom corners */
        int bot_row = y + h - 1 - i;
        gpu_fill_rect(x + r - vx, bot_row, w - 2 * (r - vx), 1, color);
    }
}

/* Draw a soft drop shadow under the window.
 * We simulate a macOS-style blur by layering a few rounded rects whose color
 * is only slightly darker than the desktop background - giving a natural
 * diffuse look without any real alpha blending.
 * Shadow spread is kept to 1-2px so it stays tight and subtle. */
static void wm_draw_shadow(int x, int y, int w, int h, bool focused, int r) {
    if (focused) {
        /* Focused: soft 2px spread, offset slightly downward like macOS */
        wm_draw_rounded_window_frame(x - 2, y + 1, w + 4, h + 4, 0xFF111120, r + 2);
        wm_draw_rounded_window_frame(x - 1, y + 1, w + 2, h + 3, 0xFF0E0E1C, r + 1);
        wm_draw_rounded_window_frame(x,     y + 2, w,     h + 2, 0xFF0B0B18, r);
    } else {
        /* Unfocused: just 1px hairline shadow, barely visible */
        wm_draw_rounded_window_frame(x - 1, y + 1, w + 2, h + 2, 0xFF111120, r + 1);
        wm_draw_rounded_window_frame(x,     y + 1, w,     h + 1, 0xFF0E0E1C, r);
    }
}

/* Draw a filled circle using scanline fill - improved for smoothness */
static void wm_draw_circle(int cx, int cy, int r, uint32_t color) {
    /* Use a slightly higher threshold (r*r + r/2) for better small circle shapes.
     * This avoids the 1-pixel 'nubs' at cardinal directions by giving them 
     * a small flat edge (e.g. 3px wide for r=6), which appears rounder to the eye. */
    int threshold = r * r + (r / 2);
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= threshold) {
                gpu_draw_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

/* Draw a single window frame and content */
static void wm_draw_window(Window *win) {
    if (!win->visible || win->state == WM_STATE_MINIMIZED) return;

    if (win->is_chromeless) {
        if (win->on_draw) win->on_draw(win);
        return;
    }
    
    if (win->is_chromeless) {
        if (win->on_draw) win->on_draw(win);
        return;
    }

    bool is_focused = (win == focused_window);
    uint32_t title_bg = is_focused ? WM_COLOR_TITLE_FOCUS : WM_COLOR_TITLEBAR;

    int r = (g_settings.window_style == 0) ? 12 : 2;

    /* Drop shadow - layered rounded rects for a smooth blur */
    if (win->state != WM_STATE_MAXIMIZED) {
        wm_draw_shadow(win->x, win->y, win->w, win->h, is_focused, r);
    }

    /* Window body */
    if (g_settings.dock_transparent) {
        /* Real Blur + Glassmorphic Title Bar */
        gpu_blur_rect(win->x, win->y, win->w, WM_TITLEBAR_HEIGHT + r);
        /* Layer 1: Subtle dark tint to help text readability on any background */
        wm_draw_rounded_rect_blend(win->x, win->y, win->w, WM_TITLEBAR_HEIGHT + r,
                                    0xFF000000, r, 100);
        /* Layer 2: Top shine highlight */
        gpu_fill_rect_blend(win->x + r, win->y, win->w - 2*r, 1, 0xFFFFFFFF, 80);
        /* Layer 3: Opaque client area */
        gpu_fill_rect(win->x, win->y + WM_TITLEBAR_HEIGHT, win->w, 
                      win->h - WM_TITLEBAR_HEIGHT, WM_COLOR_WINDOW_BG);
        /* Bottom corners of client area (if rounded) */
        if (r > 2) {
            wm_draw_rounded_window_frame(win->x, win->y + win->h - r*2, win->w, r*2,
                                          WM_COLOR_WINDOW_BG, r);
        }
    } else {
        /* Standard opaque body */
        wm_draw_rounded_window_frame(win->x, win->y, win->w, win->h,
                                      title_bg, r);
        gpu_fill_rect(wm_client_x(win), wm_client_y(win),
                      wm_client_w(win), wm_client_h(win), WM_COLOR_WINDOW_BG);
    }

    /* Focused accent: subtle 2px accent line at very top of title bar */
    if (is_focused && win->state != WM_STATE_MAXIMIZED) {
        gpu_fill_rect(win->x + r, win->y, win->w - 2 * r, 2, WM_COLOR_ACCENT);
    }

    /* Separator between title bar and client */
    gpu_fill_rect(win->x + WM_BORDER_WIDTH, win->y + WM_TITLEBAR_HEIGHT - 1,
                  win->w - 2 * WM_BORDER_WIDTH, 1, 0xFF1A1A2E);

    /* Title text - centered, lighter font */
    int title_len = wm_strlen(win->title);
    int title_x = win->x + (win->w - title_len * 8) / 2;
    int title_y = win->y + (WM_TITLEBAR_HEIGHT - 8) / 2;
    
    if (g_settings.window_style == 0) {
        if (title_x < win->x + 90) title_x = win->x + 90;
    }
    
    uint32_t title_fg = is_focused ? 0xFFE8E8F4 : 0xFF7A7A98;
    gpu_draw_string(title_x, title_y, (const uint8_t *)win->title,
                    title_fg, title_bg);

    if (g_settings.window_style == 0) {
        /* MacOS Traffic light buttons */
        int btn_cy = win->y + WM_TITLEBAR_HEIGHT / 2;
        int btn_cr = 6;

        int close_cx = win->x + WM_BORDER_WIDTH + 16;
        wm_draw_circle(close_cx, btn_cy, btn_cr, 0xFFFF5F57);

        int min_cx = close_cx + 22;
        wm_draw_circle(min_cx, btn_cy, btn_cr, 0xFFFFBD2E);

        int max_cx = min_cx + 22;
        wm_draw_circle(max_cx, btn_cy, btn_cr, 0xFF28C840);
    } else {
        /* Windows style: -, [], X on the right */
        int btn_w = 28, btn_h = 22;
        int btn_y = win->y + (WM_TITLEBAR_HEIGHT - btn_h) / 2;
        
        int close_x = win->x + win->w - WM_BORDER_WIDTH - btn_w - 4;
        gpu_fill_rect(close_x, btn_y, btn_w, btn_h, 0xFFEF4444);
        gpu_draw_char(close_x + 10, btn_y + 7, 'X', 0xFFFFFFFF, 0xFFEF4444);
        
        int max_x = close_x - btn_w - 4;
        gpu_fill_rect(max_x, btn_y, btn_w, btn_h, 0xFF3B3B5A);
        gpu_fill_rect(max_x + 9, btn_y + 6, 10, 10, 0xFFFFFFFF);
        gpu_fill_rect(max_x + 11, btn_y + 8, 6, 6, 0xFF3B3B5A);
        
        int min_x = max_x - btn_w - 4;
        gpu_fill_rect(min_x, btn_y, btn_w, btn_h, 0xFF3B3B5A);
        gpu_fill_rect(min_x + 9, btn_y + 11, 10, 2, 0xFFFFFFFF);
    }

    /* Client area background */
    gpu_fill_rect(wm_client_x(win), wm_client_y(win),
                  wm_client_w(win), wm_client_h(win), WM_COLOR_WINDOW_BG);

    if (win->on_draw) win->on_draw(win);
}

void wm_invalidate_bg(void) { bg_dirty = true; }

void wm_draw_desktop_bg(void) {
    bg_dirty = false;
    int desktop_h = screen_h;
    int bands = 40;
    int band_h = desktop_h / bands;
    if (band_h < 1) band_h = 1;

    uint32_t base_color = g_bg_colors[g_settings.bg_color % 5];
    int br = (base_color >> 16) & 0xFF;
    int bg = (base_color >> 8) & 0xFF;
    int bb = (base_color >> 0) & 0xFF;

    for (int i = 0; i < bands; i++) {
        int y = i * band_h;
        int h = (i == bands - 1) ? (desktop_h - y) : band_h;
        int t = (i * 255) / bands;

        /* Add a small brightening factor for the gradient */
        int rv = br + (t * 20 / 255);
        int gv = bg + (t * 15 / 255);
        int bv = bb + (t * 40 / 255);

        /* Clamp values to 255 to prevent overflow/wraparound */
        if (rv > 255) rv = 255;
        if (gv > 255) gv = 255;
        if (bv > 255) bv = 255;

        uint32_t color = 0xFF000000 | (rv << 16) | (gv << 8) | bv;
        gpu_fill_rect(0, y, screen_w, h, color);
    }

    const char *label = "Aurion OS";
    int lx = (screen_w - wm_strlen(label) * 8) / 2;
    int ly = (desktop_h - 8) / 2 - 12;

    /* If background is very bright, use darker labels */
    uint32_t shadow_color = 0xFF060610;
    uint32_t text_color = 0xFF3A3A58;
    if (br > 200 && bg > 200 && bb > 200) {
        shadow_color = 0xFFA0A0A0;
        text_color = 0xFF202020;
    }

    gpu_draw_string(lx + 1, ly + 1, (const uint8_t *)label, shadow_color, 0);
    gpu_draw_string(lx,     ly,     (const uint8_t *)label, text_color, 0);
}

/* macOS-style Dock */
#define DOCK_ICON_BASE   64
#define DOCK_ICON_MAG    80
#define DOCK_PAD         8
#define DOCK_BOTTOM_GAP  8
#define DOCK_CORNER      12

static icon_draw_fn dock_icon_fns[WM_MAX_WINDOWS + 16];
static char         dock_icon_names[WM_MAX_WINDOWS + 16][64];
static int          dock_slot_count = 0;
static int          dock_hover_slot = -1;
static int dock_slot_size[WM_MAX_WINDOWS + 16];

void wm_dock_add(const char *name, icon_draw_fn fn) {
    if (dock_slot_count >= WM_MAX_WINDOWS + 15) return;
    int i = 0;
    while (name[i] && i < 63) { dock_icon_names[dock_slot_count][i] = name[i]; i++; }
    dock_icon_names[dock_slot_count][i] = 0;
    dock_icon_fns[dock_slot_count] = fn;
    dock_slot_size[dock_slot_count] = DOCK_ICON_BASE;
    dock_slot_count++;
}

void wm_dock_clear(void) { dock_slot_count = 0; }

static bool dock_app_is_open(const char *name) {
    for (int i = 0; i < window_count; i++) {
        if (!windows[i].visible) continue;
        const char *a = windows[i].title;
        const char *b = name;
        bool match = true;
        while (*b) { if (*a != *b) { match = false; break; } a++; b++; }
        if (match) return true;
    }
    return false;
}

static void dock_draw_slot(int sx, int sy, int sz, icon_draw_fn fn,
                            bool open, bool hovered) {
    if (hovered) {
        int pad = 4;
        wm_draw_rounded_rect_blend(sx - pad, sy - pad,
                                    sz + pad*2, sz + pad*2,
                                    0xFFFFFFFF, 14, 40); /* Subtle light hover glow */
    }
    if (fn) {
        int icon_size = 64; /* Matches the new 64x64 RLE resolution */
        int ix = sx + (sz - icon_size) / 2;
        int iy = sy + (sz - icon_size) / 2;
        fn(ix, iy);
    }
    if (open) {
        wm_draw_circle(sx + sz / 2, sy + sz - 4, 2, WM_COLOR_ACCENT);
    }
}

/* Draw a filled rounded rectangle with alpha blending. */
static void wm_draw_rounded_rect_blend(int x, int y, int w, int h,
                                        uint32_t color, int r, uint8_t alpha) {
    if (r < 1) { gpu_fill_rect_blend(x, y, w, h, color, alpha); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    /* Middle band */
    gpu_fill_rect_blend(x, y + r, w, h - 2 * r, color, alpha);

    /* Top and bottom corner bands */
    for (int i = 0; i < r; i++) {
        int vy = r - 1 - i;
        int vx = 0;
        while ((vx + 1) * (vx + 1) + vy * vy <= r * r) vx++;
        int top_row = y + i;
        gpu_fill_rect_blend(x + r - vx, top_row, w - 2 * (r - vx), 1, color, alpha);
        int bot_row = y + h - 1 - i;
        gpu_fill_rect_blend(x + r - vx, bot_row, w - 2 * (r - vx), 1, color, alpha);
    }
}

void wm_draw_taskbar(void) {
    if (focused_window && focused_window->is_chromeless) return;
    if (dock_slot_count == 0) return;

    /* Update animations first */
    for (int i = 0; i < dock_slot_count; i++) {
        int target = (i == dock_hover_slot && g_settings.dock_magnification) ? DOCK_ICON_MAG : DOCK_ICON_BASE;
        int diff = target - dock_slot_size[i];
        if (diff != 0) {
            int step = (diff > 0) ? (diff + 9) / 10 : (diff - 9) / 10;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            dock_slot_size[i] += step;
        }
    }

    /* Calculate dynamic total width and slot positions */
    int total_icons_w = 0;
    for (int i = 0; i < dock_slot_count; i++) {
        total_icons_w += dock_slot_size[i];
    }
    int total_w = total_icons_w + (dock_slot_count - 1) * DOCK_PAD + 28;
    int dock_h  = DOCK_ICON_BASE + 18;
    int dock_x = (screen_w - total_w) / 2;
    int dock_y = screen_h - dock_h - DOCK_BOTTOM_GAP;
    int br = 20;

    /* Glassmorphic Dock Background */
    uint8_t alpha = g_settings.dock_transparent ? 160 : 255;
    uint32_t dock_bg = 0xFF000000; /* Pure Black as base */

    /* Layer 1: Semi-transparent black base */
    wm_draw_rounded_rect_blend(dock_x, dock_y, total_w, dock_h, dock_bg, br, alpha);
    
    if (g_settings.dock_transparent) {
        /* Layer 2: Subtle white top border for "glass" shine */
        gpu_fill_rect_blend(dock_x + br, dock_y, total_w - 2*br, 1, 0xFFFFFFFF, 60);
        /* Layer 3: Subtle dark bottom border */
        gpu_fill_rect_blend(dock_x + br, dock_y + dock_h - 1, total_w - 2*br, 1, 0xFF000000, 80);
    } else {
        /* Opaque: simple border */
        gpu_fill_rect(dock_x + br, dock_y, total_w - 2*br, 1, 0xFF333333);
    }

    /* Draw Icons */
    int sx = dock_x + 14;
    for (int i = 0; i < dock_slot_count; i++) {
        bool hovered = (i == dock_hover_slot);
        int sz = dock_slot_size[i];
        /* Center vertically relative to DOCK_ICON_BASE, but if magnified it goes up */
        int sy = dock_y + 9 + (DOCK_ICON_BASE - sz);
        bool open = dock_app_is_open(dock_icon_names[i]);
        
        dock_draw_slot(sx, sy, sz, dock_icon_fns[i], open, hovered);
        
        sx += sz + DOCK_PAD; /* Advance by ACTUAL size of current slot */
    }

    /* Clock in top right */
    uint8_t hh, mm, ss;
    sys_get_time(&hh, &mm, &ss);
    hh = (hh + 1) % 24;
    char clk[9];
    clk[0] = '0' + (hh / 10); clk[1] = '0' + (hh % 10); clk[2] = ':';
    clk[3] = '0' + (mm / 10); clk[4] = '0' + (mm % 10); clk[5] = ':';
    clk[6] = '0' + (ss / 10); clk[7] = '0' + (ss % 10); clk[8] = 0;
    
    /* Glassmorphic Clock background */
    wm_draw_rounded_rect_blend(screen_w - 96, 6, 90, 24, 0xFF101018, 8, 180);
    gpu_fill_rect_blend(screen_w - 96 + 8, 6, 90 - 16, 1, 0xFFFFFFFF, 40);
    gpu_draw_string(screen_w - 88, 12, (const uint8_t *)clk, 0xFFE0E0F8, 0);
}

bool wm_dock_handle(int mx, int my, bool click) {
    if (dock_slot_count == 0) return false;
    
    /* We must use the SAME dynamic width logic as draw_taskbar */
    int total_icons_w = 0;
    for (int i = 0; i < dock_slot_count; i++) {
        total_icons_w += dock_slot_size[i];
    }
    int total_w = total_icons_w + (dock_slot_count - 1) * DOCK_PAD + 28;
    int dock_h  = DOCK_ICON_BASE + 16;
    int dock_x  = (screen_w - total_w) / 2;
    int dock_y  = screen_h - dock_h - DOCK_BOTTOM_GAP;

    /* Extended hover area for magnification */
    if (mx < dock_x || mx > dock_x + total_w ||
        my < dock_y - 20 || my > dock_y + dock_h + 20) {
        dock_hover_slot = -1;
        return false;
    }

    int sx = dock_x + 14;
    for (int i = 0; i < dock_slot_count; i++) {
        int sz = dock_slot_size[i];
        if (mx >= sx && mx < sx + sz) {
            dock_hover_slot = i;
            if (click) {
                dock_clicked_slot = i;
            }
            return true;
        }
        sx += sz + DOCK_PAD;
    }
    
    dock_hover_slot = -1;
    return false;
}

static int cursor_prev_x = -1;
static int cursor_prev_y = -1;

void wm_draw_cursor(int mx, int my) {
    cursor_prev_x = mx;
    cursor_prev_y = my;
    static const uint8_t cursor[18][14] = {
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0,0,0,0}, {1,2,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0,0,0}, {1,2,2,2,1,0,0,0,0,0,0,0,0,0}, {1,2,2,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0,0,0}, {1,2,2,2,2,2,2,1,0,0,0,0,0,0}, {1,2,2,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0,0,0}, {1,2,2,2,2,2,2,2,2,2,1,0,0,0}, {1,2,2,2,2,2,2,1,1,1,1,1,0,0},
        {1,2,2,2,1,2,2,1,0,0,0,0,0,0}, {1,2,2,1,0,1,2,2,1,0,0,0,0,0}, {1,2,1,0,0,1,2,2,1,0,0,0,0,0},
        {1,1,0,0,0,0,1,2,2,1,0,0,0,0}, {1,0,0,0,0,0,1,2,2,1,0,0,0,0}, {0,0,0,0,0,0,0,1,1,0,0,0,0,0},
    };
    for (int dy = 0; dy < 18; dy++) {
        for (int dx = 0; dx < 14; dx++) {
            int px = mx + dx; int py = my + dy;
            if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                if (cursor[dy][dx] == 1) gpu_draw_pixel(px, py, 0xFF0A0A14);
                else if (cursor[dy][dx] == 2) gpu_draw_pixel(px, py, WM_COLOR_WHITE);
            }
        }
    }
}

/* Light-weight update for blocking CLI commands (allows mouse move/redraw) */
void wm_update_light(void) {
    /* Poll mouse so cursor keeps moving during blocking commands */
    for (int _mp = 0; _mp < 5; _mp++) mouse_poll();

    /* Dispatch hardware keyboard events to focused window.
     * We do NOT call wm_handle_input() here because it carries
     * shared static state (prev_left, dragging, resizing) that would
     * conflict with the desktop main loop. */
    while (sys_kb_hit() && focused_window) {
        uint16_t key = sys_getkey();
        if ((key & 0xFF) != 0) {
            char mapped = keyboard_remap((char)(key & 0xFF));
            key = (key & 0xFF00) | (uint8_t)mapped;
        }
        if (focused_window->on_key) {
            focused_window->on_key(focused_window, key);
        }
    }

    int mx = mouse_get_x();
    int my = mouse_get_y();
    /* Discard scroll during light update to prevent event buildup */
    mouse_get_z_delta();
    
    /* Just draw everything to keep it alive */
    wm_draw_desktop_bg();
    for (int z = 0; z < window_count; z++) {
        Window *win = wm_get_at_z(z);
        if (win) wm_draw_window(win);
    }
    wm_draw_taskbar(); /* draws dock */
    wm_draw_cursor(mx, my);
    gpu_flush();
}

void wm_handle_input(void) {
    int mx = mouse_get_x(); int my = mouse_get_y();
    bool left = mouse_get_left(); bool right = mouse_get_right();
    bool click = left && !prev_left; bool release = !left && prev_left;
    static bool prev_right = false; bool right_click = right && !prev_right;
    int ty = screen_h - WM_TASKBAR_HEIGHT;

    if (dragging && drag_window) {
        if (left) {
            drag_target_x = mx - drag_offset_x;
            drag_target_y = my - drag_offset_y;
            if (drag_target_y < 0) drag_target_y = 0;
            if (drag_target_y > ty - WM_TITLEBAR_HEIGHT) drag_target_y = ty - WM_TITLEBAR_HEIGHT;
            int dx = drag_target_x - drag_window->x; int dy = drag_target_y - drag_window->y;
            drag_window->x += dx * 2 / 5; drag_window->y += dy * 2 / 5;
            if (dx > -2 && dx < 2) drag_window->x = drag_target_x;
            if (dy > -2 && dy < 2) drag_window->y = drag_target_y;
            bg_dirty = true;
        } else {
            drag_window->x = drag_target_x; drag_window->y = drag_target_y;
            dragging = false; drag_window = 0;
        }
        prev_left = left; return;
    }

    if (resizing && resize_window) {
        if (left) {
            if (resize_edge & 1) { int nw = mx - resize_window->x; if (nw >= resize_window->min_w) resize_window->w = nw; }
            if (resize_edge & 2) { int nh = my - resize_window->y; if (nh >= resize_window->min_h && resize_window->y + nh < ty) resize_window->h = nh; }
        } else { resizing = false; resize_window = 0; }
        prev_left = left; return;
    }

    if (click) {
        if (my >= ty) { prev_left = left; return; }
        Window *clicked = wm_window_at(mx, my);
        if (clicked) {
            wm_focus_window(clicked);

            /* Double-click maximize check on title bar */
            static uint32_t last_click_tick = 0;
            static Window *last_clicked_win = 0;
            uint32_t now = get_ticks();
            bool is_double = (clicked == last_clicked_win && (now - last_click_tick) < 40);
            last_click_tick = now;
            last_clicked_win = clicked;

            if (my >= clicked->y && my < clicked->y + WM_TITLEBAR_HEIGHT) {
                if (is_double) {
                    if (clicked->state == WM_STATE_MAXIMIZED) wm_restore_window(clicked);
                    else wm_maximize_window(clicked);
                    last_click_tick = 0; /* Reset to prevent immediate re-trigger */
                    prev_left = left; return;
                }
            }

            if (g_settings.window_style == 0) {
                int btn_cy = clicked->y + WM_TITLEBAR_HEIGHT / 2; int btn_cr = 6;
                int close_cx = clicked->x + WM_BORDER_WIDTH + 16;
                int yellow_cx = close_cx + 22; int green_cx = yellow_cx + 22;
                #define IN_CIRCLE(px, py, cx, cy, r) (((px)-(cx))*((px)-(cx)) + ((py)-(cy))*((py)-(cy)) <= (r)*(r))
                if (IN_CIRCLE(mx, my, close_cx, btn_cy, btn_cr + 2)) { if (clicked->on_close) clicked->on_close(clicked); wm_destroy_window(clicked); prev_left = left; return; }
                /* Swapping: Yellow=Maximize, Green=Minimize */
                if (IN_CIRCLE(mx, my, yellow_cx, btn_cy, btn_cr + 2)) { wm_maximize_window(clicked); prev_left = left; return; }
                if (IN_CIRCLE(mx, my, green_cx, btn_cy, btn_cr + 2)) { wm_minimize_window(clicked); prev_left = left; return; }
                #undef IN_CIRCLE
            } else {
                int btn_w = 28, btn_h = 22;
                int btn_y = clicked->y + (WM_TITLEBAR_HEIGHT - btn_h) / 2;
                int close_x = clicked->x + clicked->w - WM_BORDER_WIDTH - btn_w - 4;
                int max_x = close_x - btn_w - 4;
                int min_x = max_x - btn_w - 4;
                int ly = my - clicked->y;
                if (ly >= btn_y - clicked->y && ly < btn_y - clicked->y + btn_h) {
                    if (mx >= close_x && mx < close_x + btn_w) { if (clicked->on_close) clicked->on_close(clicked); wm_destroy_window(clicked); prev_left = left; return; }
                    if (mx >= max_x && mx < max_x + btn_w) { wm_maximize_window(clicked); prev_left = left; return; }
                    if (mx >= min_x && mx < min_x + btn_w) { wm_minimize_window(clicked); prev_left = left; return; }
                }
            }

            if (my >= clicked->y && my < clicked->y + WM_TITLEBAR_HEIGHT && clicked->state != WM_STATE_MAXIMIZED) {
                dragging = true; drag_window = clicked; drag_offset_x = mx - clicked->x; drag_offset_y = my - clicked->y;
                prev_left = left; return;
            }

            if (clicked->state != WM_STATE_MAXIMIZED) {
                int edge = 0;
                if (mx >= clicked->x + clicked->w - 6) edge |= 1;
                if (my >= clicked->y + clicked->h - 6) edge |= 2;
                if (edge) { resizing = true; resize_edge = edge; resize_window = clicked; prev_left = left; return; }
            }

            if (clicked->on_mouse) { clicked->on_mouse(clicked, mx - wm_client_x(clicked), my - wm_client_y(clicked), true, false); }
        }
    }

    if (right_click) {
        Window *rwin = wm_window_at(mx, my);
        if (rwin && rwin->on_mouse && my >= wm_client_y(rwin)) {
            wm_focus_window(rwin); rwin->on_mouse(rwin, mx - wm_client_x(rwin), my - wm_client_y(rwin), false, true);
        }
    }

    if (left && focused_window && focused_window->on_mouse && !dragging && !resizing) {
        int lx = mx - wm_client_x(focused_window); int ly = my - wm_client_y(focused_window);
        if (lx >= 0 && ly >= 0 && lx < wm_client_w(focused_window) && ly < wm_client_h(focused_window)) {
            focused_window->on_mouse(focused_window, lx, ly, left, mouse_get_right());
        }
    }

    if (release && focused_window && focused_window->on_mouse) {
        focused_window->on_mouse(focused_window, mx - wm_client_x(focused_window), my - wm_client_y(focused_window), false, false);
    }

    if (sys_kb_hit() && focused_window) {
        uint16_t key = sys_getkey();
        if ((key & 0xFF) != 0) {
            char mapped = keyboard_remap((char)(key & 0xFF));
            key = (key & 0xFF00) | (uint8_t)mapped;
        }
        if (focused_window->on_key) {
            focused_window->on_key(focused_window, key);
        }
    }

    /* Translate mouse scroll wheel to virtual PgUp/PgDn keys */
    int z_delta = mouse_get_z_delta();
    if (z_delta != 0 && focused_window && focused_window->on_key) {
        /* Standard convention: rotating wheel away (up) should move view towards top (PgUp)
           The previous implementation was inverted for the user's mouse/environment. */
        uint16_t virtual_key = (z_delta < 0) ? (0x49 << 8) : (0x51 << 8);
        focused_window->on_key(focused_window, virtual_key);
    }

    prev_left = left; prev_right = right;
}

void wm_draw_all(void) {
    for (int z = 0; z < window_count; z++) {
        Window *win = wm_get_at_z(z);
        if (win) wm_draw_window(win);
    }
}

int wm_get_screen_w(void) { return screen_w; }
int wm_get_screen_h(void) { return screen_h; }
int wm_get_window_count(void) { return window_count; }
Window *wm_get_focused(void) { return focused_window; }
Window *wm_get_window(int index) {
    if (index >= 0 && index < window_count) return &windows[index];
    return 0;
}
