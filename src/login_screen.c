/*
 * login_screen.c
 *
 * Improved login screen for AurionOS
 * - Safer framebuffer handling
 * - Better panel visuals
 * - Reusable UI helpers
 * - Hover/focus states
 * - Mouse + keyboard support
 * - Background capture, blur, dim
 *
 * This file is intentionally verbose and structured to be maintainable.
 */

#include "login_screen.h"
#include "window_manager.h"
#include <stdint.h>
#include <stdbool.h>

/* External platform / GPU functions */

extern uint32_t *gpu_get_backbuffer(void);
extern int gpu_flush(void);
extern void gpu_clear(uint32_t color);
extern int gpu_get_width(void);
extern int gpu_get_height(void);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_fill_rect_blend(int x, int y, int w, int h, uint32_t c, uint8_t a);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern uint32_t get_ticks(void);
extern uint16_t sys_getkey(void);
extern int sys_kb_hit(void);
extern char keyboard_remap(char c);

/* Mouse */
extern void mouse_poll(void);
extern int mouse_get_x(void);
extern int mouse_get_y(void);
extern bool mouse_get_left(void);

/* Dock / desktop */
extern void wm_draw_taskbar(void);
extern void wm_draw_desktop_bg(void);
extern void wm_draw_cursor(int mx, int my);

/* Config */

/*
 * Adjust these if your maximum screen resolution is larger.
 * These static buffers replace unsafe raw memory addresses.
 */
#define LOGIN_MAX_W  1920
#define LOGIN_MAX_H  1080

/*
 * Downsampled blur buffer dimensions.
 * We blur at half resolution for speed.
 */
#define BLUR_SCALE   2
#define BLUR_W       (LOGIN_MAX_W / BLUR_SCALE)
#define BLUR_H       (LOGIN_MAX_H / BLUR_SCALE)

/* Text sizes assume fixed 8x16-ish glyph metrics */
#define FONT_CHAR_W  8
#define FONT_CHAR_H  16

/* Input limits */
#define LOGIN_MAX_USERNAME 31
#define LOGIN_MAX_PASSWORD 31

/* Static working buffers */

/*
 * Full-screen captured image.
 * ARGB8888
 */
static uint32_t g_capture_buffer[LOGIN_MAX_W * LOGIN_MAX_H];

/*
 * Half-resolution blur working buffer.
 */
static uint32_t g_blur_small_a[BLUR_W * BLUR_H];
static uint32_t g_blur_small_b[BLUR_W * BLUR_H];

/*
 * Final processed background at full size.
 */
static uint32_t g_blurred_background[LOGIN_MAX_W * LOGIN_MAX_H];

/* Small utility functions */

static int ui_min_int(int a, int b) {
    return (a < b) ? a : b;
}

static int ui_max_int(int a, int b) {
    return (a > b) ? a : b;
}

static int ui_clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint8_t ui_clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static int ui_strlen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void ui_strcpy(char *dst, const char *src, int dst_size) {
    int i;
    if (!dst || dst_size <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    for (i = 0; i < dst_size - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static void ui_memcpy32(uint32_t *dst, const uint32_t *src, int count) {
    if (!dst || !src || count <= 0) return;
    for (int i = 0; i < count; i++) dst[i] = src[i];
}

static bool ui_point_in_rect(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

/* Color helpers */

static uint8_t color_r(uint32_t c) { return (uint8_t)((c >> 16) & 0xFF); }
static uint8_t color_g(uint32_t c) { return (uint8_t)((c >> 8) & 0xFF); }
static uint8_t color_b(uint32_t c) { return (uint8_t)(c & 0xFF); }
static uint8_t color_a(uint32_t c) { return (uint8_t)((c >> 24) & 0xFF); }

static uint32_t make_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) |
           ((uint32_t)r << 16) |
           ((uint32_t)g << 8)  |
           ((uint32_t)b);
}

static uint32_t color_mul_rgb(uint32_t c, uint8_t factor) {
    int r = (color_r(c) * factor) / 255;
    int g = (color_g(c) * factor) / 255;
    int b = (color_b(c) * factor) / 255;
    return make_argb(0xFF, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static uint32_t color_lerp(uint32_t a, uint32_t b, uint8_t t) {
    int ar = color_r(a), ag = color_g(a), ab = color_b(a);
    int br = color_r(b), bg = color_g(b), bb = color_b(b);

    int r = ar + ((br - ar) * t) / 255;
    int g = ag + ((bg - ag) * t) / 255;
    int bl = ab + ((bb - ab) * t) / 255;

    return make_argb(0xFF, ui_clamp_u8(r), ui_clamp_u8(g), ui_clamp_u8(bl));
}

/* Primitive drawing helpers */

static void draw_hline(int x, int y, int w, uint32_t color) {
    if (w <= 0) return;
    gpu_fill_rect(x, y, w, 1, color);
}

static void draw_vline(int x, int y, int h, uint32_t color) {
    if (h <= 0) return;
    gpu_fill_rect(x, y, 1, h, color);
}

static void draw_rect_outline(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    draw_hline(x, y, w, color);
    draw_hline(x, y + h - 1, w, color);
    draw_vline(x, y, h, color);
    draw_vline(x + w - 1, y, h, color);
}

/*
 * Better rounded rectangle than the original.
 * Still simple enough for kernel / baremetal style rendering.
 */
static void draw_rounded_rect_fill(int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (r == 0) {
        gpu_fill_rect(x, y, w, h, color);
        return;
    }

    gpu_fill_rect(x + r, y, w - 2 * r, h, color);
    gpu_fill_rect(x, y + r, r, h - 2 * r, color);
    gpu_fill_rect(x + w - r, y + r, r, h - 2 * r, color);

    for (int yy = 0; yy < r; yy++) {
        int dy = r - yy;
        int dx = r;
        while ((dx * dx + dy * dy) > (r * r) && dx > 0) dx--;

        int left = x + r - dx;
        int right = x + w - r + dx;

        gpu_fill_rect(left, y + yy, right - left, 1, color);
        gpu_fill_rect(left, y + h - 1 - yy, right - left, 1, color);
    }
}

static void draw_rounded_rect_outline(int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 1 || h <= 1) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (r == 0) {
        draw_rect_outline(x, y, w, h, color);
        return;
    }

    draw_hline(x + r, y, w - 2 * r, color);
    draw_hline(x + r, y + h - 1, w - 2 * r, color);
    draw_vline(x, y + r, h - 2 * r, color);
    draw_vline(x + w - 1, y + r, h - 2 * r, color);

    for (int yy = 0; yy < r; yy++) {
        int dy = r - yy;
        int dx = r;
        while ((dx * dx + dy * dy) > (r * r) && dx > 0) dx--;

        gpu_draw_pixel(x + r - dx, y + yy, color);
        gpu_draw_pixel(x + w - r + dx - 1, y + yy, color);
        gpu_draw_pixel(x + r - dx, y + h - 1 - yy, color);
        gpu_draw_pixel(x + w - r + dx - 1, y + h - 1 - yy, color);
    }
}

static int login_corner_inset(int r, int row) {
    int dy = r - row;
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
    return r - dx;
}

static void login_rrect_a(int x, int y, int w, int h, uint32_t c, int r, uint8_t a) {
    if (w <= 0 || h <= 0 || a == 0) return;
    if (r < 1) { gpu_fill_rect_blend(x, y, w, h, c, a); return; }
    r = ui_min_int(r, ui_min_int(w / 2, h / 2));
    for (int i = 0; i < r; i++) {
        int ins = login_corner_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0) {
            gpu_fill_rect_blend(x + ins, y + i, sw, 1, c, a);
            gpu_fill_rect_blend(x + ins, y + h - 1 - i, sw, 1, c, a);
        }
    }
    if (h - 2 * r > 0) gpu_fill_rect_blend(x, y + r, w, h - 2 * r, c, a);
}

static void draw_panel_shadow(int x, int y, int w, int h, int radius) {
    /* smooth rounded multi-layer shadow */
    login_rrect_a(x - 8, y - 4, w + 16, h + 24, 0xFF000000, radius + 8, 10);
    login_rrect_a(x - 4, y - 2, w + 8,  h + 12, 0xFF000000, radius + 4, 15);
    login_rrect_a(x - 2, y,     w + 4,  h + 6,  0xFF000000, radius + 2, 25);

    /* subtle outer glow style depth */
    draw_rounded_rect_outline(x - 1, y - 1, w + 2, h + 2, radius + 1, 0x20202030);
}

static void draw_text_centered(int x, int y, int w, const char *text, uint32_t fg) {
    int len = ui_strlen(text);
    int tx = x + (w - len * FONT_CHAR_W) / 2;
    gpu_draw_string(tx, y, (const uint8_t *)text, fg, 0);
}

static void draw_text_right(int x, int y, int w, const char *text, uint32_t fg) {
    int len = ui_strlen(text);
    int tx = x + w - len * FONT_CHAR_W;
    gpu_draw_string(tx, y, (const uint8_t *)text, fg, 0);
}

/* Framebuffer post-processing */

static bool login_buffers_fit_screen(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (w > LOGIN_MAX_W || h > LOGIN_MAX_H) return false;
    return true;
}

static void capture_current_screen(uint32_t *dst, int w, int h) {
    uint32_t *src = gpu_get_backbuffer();
    if (!dst || !src) return;
    ui_memcpy32(dst, src, w * h);
}

static void downsample_2x(const uint32_t *src, int sw, int sh,
                          uint32_t *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int sy = y * 2;
        int sy1 = ui_min_int(sy + 1, sh - 1);

        for (int x = 0; x < dw; x++) {
            int sx = x * 2;
            int sx1 = ui_min_int(sx + 1, sw - 1);

            uint32_t p0 = src[sy * sw + sx];
            uint32_t p1 = src[sy * sw + sx1];
            uint32_t p2 = src[sy1 * sw + sx];
            uint32_t p3 = src[sy1 * sw + sx1];

            int r = color_r(p0) + color_r(p1) + color_r(p2) + color_r(p3);
            int g = color_g(p0) + color_g(p1) + color_g(p2) + color_g(p3);
            int b = color_b(p0) + color_b(p1) + color_b(p2) + color_b(p3);

            dst[y * dw + x] = make_argb(0xFF, r / 4, g / 4, b / 4);
        }
    }
}

static void blur_box_pass_horizontal(const uint32_t *src, uint32_t *dst, int w, int h, int radius) {
    if (radius < 1) {
        ui_memcpy32(dst, src, w * h);
        return;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int rs = 0, gs = 0, bs = 0, count = 0;
            int x0 = ui_max_int(0, x - radius);
            int x1 = ui_min_int(w - 1, x + radius);

            for (int sx = x0; sx <= x1; sx++) {
                uint32_t p = src[y * w + sx];
                rs += color_r(p);
                gs += color_g(p);
                bs += color_b(p);
                count++;
            }

            dst[y * w + x] = make_argb(0xFF, rs / count, gs / count, bs / count);
        }
    }
}

static void blur_box_pass_vertical(const uint32_t *src, uint32_t *dst, int w, int h, int radius) {
    if (radius < 1) {
        ui_memcpy32(dst, src, w * h);
        return;
    }

    for (int y = 0; y < h; y++) {
        int y0 = ui_max_int(0, y - radius);
        int y1 = ui_min_int(h - 1, y + radius);

        for (int x = 0; x < w; x++) {
            int rs = 0, gs = 0, bs = 0, count = 0;

            for (int sy = y0; sy <= y1; sy++) {
                uint32_t p = src[sy * w + x];
                rs += color_r(p);
                gs += color_g(p);
                bs += color_b(p);
                count++;
            }

            dst[y * w + x] = make_argb(0xFF, rs / count, gs / count, bs / count);
        }
    }
}

static void upscale_nearest_2x(const uint32_t *src, int sw, int sh,
                               uint32_t *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int sy = ui_clamp_int(y / 2, 0, sh - 1);
        for (int x = 0; x < dw; x++) {
            int sx = ui_clamp_int(x / 2, 0, sw - 1);
            dst[y * dw + x] = src[sy * sw + sx];
        }
    }
}

static void darken_fullscreen(uint32_t *fb, int w, int h, uint8_t factor) {
    for (int i = 0; i < w * h; i++) {
        fb[i] = color_mul_rgb(fb[i], factor);
    }
}

static void tint_fullscreen(uint32_t *fb, int w, int h, uint32_t tint, uint8_t amount) {
    for (int i = 0; i < w * h; i++) {
        fb[i] = color_lerp(fb[i], tint, amount);
    }
}

static void build_blurred_background(int sw, int sh) {
    int dw = sw / 2;
    int dh = sh / 2;

    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    capture_current_screen(g_capture_buffer, sw, sh);
    downsample_2x(g_capture_buffer, sw, sh, g_blur_small_a, dw, dh);

    blur_box_pass_horizontal(g_blur_small_a, g_blur_small_b, dw, dh, 4);
    blur_box_pass_vertical(g_blur_small_b, g_blur_small_a, dw, dh, 4);

    /* second pass for a nicer blur */
    blur_box_pass_horizontal(g_blur_small_a, g_blur_small_b, dw, dh, 3);
    blur_box_pass_vertical(g_blur_small_b, g_blur_small_a, dw, dh, 3);

    upscale_nearest_2x(g_blur_small_a, dw, dh, g_blurred_background, sw, sh);

    darken_fullscreen(g_blurred_background, sw, sh, 140);
    /* Tint with a deep neutral onyx color instead of navy blue */
    tint_fullscreen(g_blurred_background, sw, sh, 0xFF121212, 45);
}

/* Drawing processed background */

static void draw_background_buffer(const uint32_t *fb, int w, int h) {
    if (!fb) return;

    /*
     * Still uses gpu_draw_pixel because your API doesn't expose a blit.
     * This is slow, but at least background is precomputed once.
     */
    for (int y = 0; y < h; y++) {
        int row = y * w;
        for (int x = 0; x < w; x++) {
            gpu_draw_pixel(x, y, fb[row + x]);
        }
    }
}

static void draw_background_overlay_gradients(int sw, int sh) {
    /* subtle top and bottom overlays for depth */
    gpu_fill_rect_blend(0, 0, sw, sh / 5, 0xFF000000, 28);
    gpu_fill_rect_blend(0, sh - sh / 4, sw, sh / 4, 0xFF000000, 20);
}

/* UI model */

typedef enum LoginFocusField {
    LOGIN_FOCUS_USERNAME = 0,
    LOGIN_FOCUS_PASSWORD = 1
} LoginFocusField;

typedef struct LoginRect {
    int x;
    int y;
    int w;
    int h;
} LoginRect;

typedef struct LoginState {
    char username[LOGIN_MAX_USERNAME + 1];
    char password[LOGIN_MAX_PASSWORD + 1];
    int username_len;
    int password_len;

    LoginFocusField focus;

    bool running;
    bool result;

    bool prev_left;

    bool hover_user;
    bool hover_pass;
    bool hover_button;

    bool pressed_button;

    char status_text[64];
    bool status_is_error;
} LoginState;

typedef struct LoginLayout {
    LoginRect panel;
    LoginRect user_field;
    LoginRect pass_field;
    LoginRect button;
    LoginRect title_area;
    LoginRect subtitle_area;
    LoginRect status_area;
} LoginLayout;

/* State helpers */

static void login_state_init(LoginState *s) {
    if (!s) return;

    s->username[0] = 0;
    s->password[0] = 0;
    s->username_len = 0;
    s->password_len = 0;
    s->focus = LOGIN_FOCUS_USERNAME;
    s->running = true;
    s->result = false;
    s->prev_left = false;
    s->hover_user = false;
    s->hover_pass = false;
    s->hover_button = false;
    s->pressed_button = false;
    s->status_text[0] = 0;
    s->status_is_error = false;
}

static void login_set_status(LoginState *s, const char *text, bool is_error) {
    if (!s) return;
    ui_strcpy(s->status_text, text, sizeof(s->status_text));
    s->status_is_error = is_error;
}

/* Layout */

static LoginLayout login_compute_layout(int sw, int sh) {
    LoginLayout l;

    int panel_w = 460;
    int panel_h = 320;
    int panel_x = (sw - panel_w) / 2;
    int panel_y = (sh - panel_h) / 2 - 36;

    if (panel_y < 30) panel_y = 30;

    l.panel.x = panel_x;
    l.panel.y = panel_y;
    l.panel.w = panel_w;
    l.panel.h = panel_h;

    l.title_area.x = panel_x;
    l.title_area.y = panel_y + 20;
    l.title_area.w = panel_w;
    l.title_area.h = 24;

    l.subtitle_area.x = panel_x;
    l.subtitle_area.y = panel_y + 48;
    l.subtitle_area.w = panel_w;
    l.subtitle_area.h = 20;

    l.user_field.x = panel_x + 36;
    l.user_field.y = panel_y + 104;
    l.user_field.w = panel_w - 72;
    l.user_field.h = 38;

    l.pass_field.x = panel_x + 36;
    l.pass_field.y = panel_y + 176;
    l.pass_field.w = panel_w - 72;
    l.pass_field.h = 38;

    l.button.w = 140;
    l.button.h = 40;
    l.button.x = panel_x + (panel_w - l.button.w) / 2;
    l.button.y = panel_y + 244;

    l.status_area.x = panel_x + 20;
    l.status_area.y = panel_y + panel_h - 26;
    l.status_area.w = panel_w - 40;
    l.status_area.h = 16;

    return l;
}

/* Authentication hook */

/* External user authentication */
extern int user_count;
typedef struct {
    char username[32];
    char password_hash[32];
} UserEntry;
extern UserEntry user_table[];

/* Simple hash function (same as commands.c) */
static uint32_t login_hash_string(const char *s) {
    uint32_t h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (uint32_t)(*s);
        s++;
    }
    return h;
}

/* String comparison helper */
static int login_str_cmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if (ca != cb) return (int)(ca - cb);
        a++; b++;
    }
    char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
    char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
    return (int)(ca - cb);
}

static bool login_validate_credentials(const char *username, const char *password) {
    /*
     * Authenticate against the real user table
     * Uses the same hash function as the rest of the system
     */
    if (!username || username[0] == 0) return false;
    if (!password || password[0] == 0) return false;
    
    /* Search for user in user table */
    for (int i = 0; i < user_count; i++) {
        if (login_str_cmp(user_table[i].username, username) == 0) {
            /* Found user - check password hash */
            uint32_t h = login_hash_string(password);
            bool match = true;
            
            for (int j = 0; j < 32; j++) {
                char b = (char)((h >> ((j % 4) * 8)) & 0xFF);
                if (user_table[i].password_hash[j] != b) {
                    match = false;
                    break;
                }
            }
            
            return match;
        }
    }
    
    /* User not found */
    return false;
}

/* Input field helpers */

static void input_backspace(char *buf, int *len) {
    if (!buf || !len) return;
    if (*len <= 0) return;
    (*len)--;
    buf[*len] = 0;
}

static void input_append_char(char *buf, int *len, int max_len, char c) {
    if (!buf || !len) return;
    if (*len >= max_len) return;
    buf[*len] = c;
    (*len)++;
    buf[*len] = 0;
}

static void build_password_display(const char *password, int password_len, char *out, int out_size) {
    int n = password_len;
    if (n > out_size - 1) n = out_size - 1;
    for (int i = 0; i < n; i++) out[i] = '*';
    out[n] = 0;
    (void)password;
}

/* UI widgets */

static void draw_input_label(int x, int y, const char *text) {
    gpu_draw_string(x, y, (const uint8_t *)text, 0xFFD8DCE6, 0);
}

static void draw_input_field(LoginRect r,
                             const char *text,
                             const char *placeholder,
                             bool focused,
                             bool hovered,
                             bool show_caret,
                             int text_len,
                             bool password_mode) {
    uint32_t outer;
    uint32_t inner = 0xFF0A0A0A;
    uint32_t text_color = 0xFFFFFFFF;
    uint32_t placeholder_color = 0xFF7D8696;

    if (focused) {
        outer = 0xFF808080;
        gpu_fill_rect_blend(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 0xFF808080, 25);
    } else if (hovered) {
        outer = 0xFF50586A;
    } else {
        outer = 0xFF383F4C;
    }

    draw_rounded_rect_fill(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 9, outer);
    draw_rounded_rect_fill(r.x, r.y, r.w, r.h, 8, inner);
    draw_rounded_rect_outline(r.x, r.y, r.w, r.h, 8, 0xFF202733);

    if (text && text[0]) {
        gpu_draw_string(r.x + 12, r.y + 12, (const uint8_t *)text, text_color, 0);
    } else if (placeholder) {
        gpu_draw_string(r.x + 12, r.y + 12, (const uint8_t *)placeholder, placeholder_color, 0);
    }

    if (focused && show_caret) {
        int caret_x = r.x + 12 + text_len * FONT_CHAR_W;
        if (caret_x > r.x + r.w - 8) caret_x = r.x + r.w - 8;
        gpu_fill_rect(caret_x, r.y + 9, 2, r.h - 18, 0xFF909090);
    }

    (void)password_mode;
}

static void draw_button(LoginRect r, const char *text, bool hovered, bool pressed, bool enabled) {
    uint32_t bg;
    uint32_t fg;
    uint32_t outline;

    if (!enabled) {
        bg = 0xFF3D4252;
        fg = 0xFFB4BAC7;
        outline = 0xFF4A5161;
    } else if (pressed) {
        bg = 0xFF606060;
        fg = 0xFFFFFFFF;
        outline = 0xFF808080;
    } else if (hovered) {
        bg = 0xFF707070;
        fg = 0xFFFFFFFF;
        outline = 0xFF909090;
        gpu_fill_rect_blend(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 0xFF808080, 24);
    } else {
        bg = 0xFF707070;
        fg = 0xFFFFFFFF;
        outline = 0xFF808080;
    }

    draw_rounded_rect_fill(r.x, r.y, r.w, r.h, 10, bg);
    draw_rounded_rect_outline(r.x, r.y, r.w, r.h, 10, outline);
    draw_text_centered(r.x, r.y + 13, r.w, text, fg);
}

static void draw_panel(LoginRect r) {
    draw_panel_shadow(r.x, r.y, r.w, r.h, 16);

    /* Main panel - modern black with transparency */
    draw_rounded_rect_fill(r.x, r.y, r.w, r.h, 16, 0xE0000000);

    /* Subtle top highlight */
    draw_rounded_rect_outline(r.x, r.y, r.w, r.h, 16, 0xFF2A2A2A);
    draw_hline(r.x + 18, r.y + 1, r.w - 36, 0xFF3A3A3A);

    /* Header separator */
    gpu_fill_rect_blend(r.x + 20, r.y + 78, r.w - 40, 1, 0xFFFFFFFF, 22);
}

static void draw_status_line(LoginRect r, const char *text, bool is_error) {
    if (!text || !text[0]) return;
    uint32_t fg = is_error ? 0xFFFF8A8A : 0xFF9AD0FF;
    draw_text_centered(r.x, r.y, r.w, text, fg);
}

/* Login screen rendering */

static void draw_login_screen_frame(const LoginState *s, const LoginLayout *l, int sw, int sh) {
    char masked[LOGIN_MAX_PASSWORD + 1];
    bool caret_on = ((get_ticks() / 25) % 2) == 0;

    build_password_display(s->password, s->password_len, masked, sizeof(masked));

    draw_background_buffer(g_blurred_background, sw, sh);
    draw_background_overlay_gradients(sw, sh);
    wm_draw_taskbar();

    draw_panel(l->panel);

    draw_text_centered(l->title_area.x, l->title_area.y, l->title_area.w,
                       "Welcome to AurionOS", 0xFFFFFFFF);

    draw_text_centered(l->subtitle_area.x, l->subtitle_area.y, l->subtitle_area.w,
                       "Sign in to continue", 0xFFB6BECC);

    draw_input_label(l->user_field.x, l->user_field.y - 22, "Username");
    draw_input_field(
        l->user_field,
        s->username,
        "Enter your username",
        s->focus == LOGIN_FOCUS_USERNAME,
        s->hover_user,
        s->focus == LOGIN_FOCUS_USERNAME && caret_on,
        s->username_len,
        false
    );

    draw_input_label(l->pass_field.x, l->pass_field.y - 22, "Password");
    draw_input_field(
        l->pass_field,
        masked,
        "Enter your password",
        s->focus == LOGIN_FOCUS_PASSWORD,
        s->hover_pass,
        s->focus == LOGIN_FOCUS_PASSWORD && caret_on,
        s->password_len,
        true
    );

    draw_button(l->button, "Login",
                s->hover_button,
                s->pressed_button,
                s->username_len > 0);

    draw_status_line(l->status_area, s->status_text, s->status_is_error);
}

/* Mouse handling */

static void update_hover_state(LoginState *s, const LoginLayout *l, int mx, int my) {
    s->hover_user = ui_point_in_rect(mx, my, l->user_field.x, l->user_field.y, l->user_field.w, l->user_field.h);
    s->hover_pass = ui_point_in_rect(mx, my, l->pass_field.x, l->pass_field.y, l->pass_field.w, l->pass_field.h);
    s->hover_button = ui_point_in_rect(mx, my, l->button.x, l->button.y, l->button.w, l->button.h);
}

static void handle_mouse_input(LoginState *s, const LoginLayout *l) {
    for (int i = 0; i < 8; i++) mouse_poll();

    int mx = mouse_get_x();
    int my = mouse_get_y();
    bool left = mouse_get_left();
    bool click = left && !s->prev_left;

    update_hover_state(s, l, mx, my);

    if (left && s->hover_button) {
        s->pressed_button = true;
    } else {
        s->pressed_button = false;
    }

    if (click) {
        if (s->hover_user) {
            s->focus = LOGIN_FOCUS_USERNAME;
        } else if (s->hover_pass) {
            s->focus = LOGIN_FOCUS_PASSWORD;
        } else if (s->hover_button) {
            if (s->username_len <= 0) {
                login_set_status(s, "Username is required", true);
            } else if (login_validate_credentials(s->username, s->password)) {
                s->result = true;
                s->running = false;
            } else {
                login_set_status(s, "Invalid username or password", true);
            }
        }
    }

    s->prev_left = left;
}

/* Keyboard handling */

static void handle_key_escape(LoginState *s) {
    s->result = false;
    s->running = false;
}

static void handle_key_tab(LoginState *s) {
    if (s->focus == LOGIN_FOCUS_USERNAME) {
        s->focus = LOGIN_FOCUS_PASSWORD;
    } else {
        s->focus = LOGIN_FOCUS_USERNAME;
    }
}

static void handle_key_enter(LoginState *s) {
    if (s->focus == LOGIN_FOCUS_USERNAME) {
        if (s->username_len > 0) {
            s->focus = LOGIN_FOCUS_PASSWORD;
            login_set_status(s, "", false);
        } else {
            login_set_status(s, "Please enter a username", true);
        }
        return;
    }

    if (s->username_len <= 0) {
        login_set_status(s, "Username is required", true);
        s->focus = LOGIN_FOCUS_USERNAME;
        return;
    }

    if (login_validate_credentials(s->username, s->password)) {
        s->result = true;
        s->running = false;
    } else {
        login_set_status(s, "Invalid username or password", true);
    }
}

static void handle_key_backspace(LoginState *s) {
    if (s->focus == LOGIN_FOCUS_USERNAME) {
        input_backspace(s->username, &s->username_len);
    } else {
        input_backspace(s->password, &s->password_len);
    }
}

static void handle_key_text(LoginState *s, char c) {
    if (c < 32 || c >= 127) return;

    /* Apply keyboard layout remapping */
    extern int keyboard_layout;
    if (keyboard_layout == 0) {
        /* English - no remapping needed */
    } else {
        c = keyboard_remap(c);
    }

    if (s->focus == LOGIN_FOCUS_USERNAME) {
        input_append_char(s->username, &s->username_len, LOGIN_MAX_USERNAME, c);
    } else {
        input_append_char(s->password, &s->password_len, LOGIN_MAX_PASSWORD, c);
    }

    if (s->status_text[0]) {
        login_set_status(s, "", false);
    }
}

static void handle_keyboard_input(LoginState *s) {
    while (sys_kb_hit()) {
        uint16_t key = sys_getkey();
        uint8_t lo = (uint8_t)(key & 0xFF);

        if (lo == 27) {
            handle_key_escape(s);
        } else if (lo == 9) {
            handle_key_tab(s);
        } else if (lo == 13) {
            handle_key_enter(s);
        } else if (lo == 8 || lo == 127) {
            handle_key_backspace(s);
        } else if (lo >= 32 && lo < 127) {
            handle_key_text(s, (char)lo);
        }
    }
}

static void flush_keyboard_buffer(void) {
    int guard = 0;
    while (sys_kb_hit() && guard < 256) {
        (void)sys_getkey();
        guard++;
    }
}

/* Public entry point */

bool login_screen_show(void) {
    int sw = gpu_get_width();
    int sh = gpu_get_height();

    if (!login_buffers_fit_screen(sw, sh)) {
        /*
         * Fallback if resolution exceeds static buffers.
         * We still allow a plain screen instead of crashing.
         */
        gpu_clear(0xFF10141C);

        int box_w = 420;
        int box_h = 180;
        int box_x = (sw - box_w) / 2;
        int box_y = (sh - box_h) / 2;

        draw_panel_shadow(box_x, box_y, box_w, box_h, 12);
        draw_rounded_rect_fill(box_x, box_y, box_w, box_h, 12, 0xE0202430);
        draw_rounded_rect_outline(box_x, box_y, box_w, box_h, 12, 0xFF394253);

        draw_text_centered(box_x, box_y + 30, box_w, "AurionOS Login", 0xFFFFFFFF);
        draw_text_centered(box_x, box_y + 70, box_w, "Screen too large for login blur buffer", 0xFFFF8A8A);
        draw_text_centered(box_x, box_y + 100, box_w, "Press ESC to cancel or ENTER to continue", 0xFFB6BECC);

        gpu_flush();

        while (true) {
            if (sys_kb_hit()) {
                uint16_t key = sys_getkey();
                uint8_t lo = (uint8_t)(key & 0xFF);
                if (lo == 27) return false;
                if (lo == 13) return true;
            }
        }
    }

    /*
     * Build the blurred background from the current desktop image.
     * If the caller hasn't drawn desktop yet, you can optionally:
     *   wm_draw_desktop_bg();
     *   wm_draw_taskbar();
     * before capture.
     */
    build_blurred_background(sw, sh);
    flush_keyboard_buffer();

    LoginState state;
    login_state_init(&state);
    login_set_status(&state, "Press TAB to switch fields", false);

    while (state.running) {
        LoginLayout layout = login_compute_layout(sw, sh);

        draw_login_screen_frame(&state, &layout, sw, sh);
        
        /* Draw mouse cursor */
        for (int i = 0; i < 10; i++) mouse_poll();
        int mx = mouse_get_x();
        int my = mouse_get_y();
        wm_draw_cursor(mx, my);
        
        gpu_flush();

        handle_mouse_input(&state, &layout);
        handle_keyboard_input(&state);
    }

    return state.result;
}