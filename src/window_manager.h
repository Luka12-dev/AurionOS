#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "settings_state.h"

/* ── Limits ─────────────────────────────────────────────────────── */
#define WM_MAX_WINDOWS          16
#define WM_MAX_DOCK_SLOTS       32

/* ── Chrome geometry ────────────────────────────────────────────── */
#define WM_TITLEBAR_HEIGHT      30
#define WM_BORDER_WIDTH          0      /* macOS: no visible border  */
#define WM_CORNER_RADIUS        10

/* ── Traffic-light buttons ──────────────────────────────────────── */
#define WM_TL_RADIUS             7
#define WM_TL_LEFT_PAD          20
#define WM_TL_SPACING           20

/* ── System bars ────────────────────────────────────────────────── */
#define WM_MENUBAR_HEIGHT       25
#define WM_TASKBAR_HEIGHT       124

/* ── Dock ───────────────────────────────────────────────────────── */
#define WM_DOCK_ICON_BASE       64
#define WM_DOCK_ICON_MAG        84
#define WM_DOCK_PAD              10
#define WM_DOCK_BOTTOM_GAP       8
#define WM_DOCK_CORNER          28
#define WM_DOCK_PILL_H          88

/* ── Window button sizes (Windows style) ────────────────────────── */
#define WM_BTN_SIZE             20
#define WM_WIN_BTN_W            28
#define WM_WIN_BTN_H            22

/* ── Window states ──────────────────────────────────────────────── */
#define WM_STATE_NORMAL          0
#define WM_STATE_MINIMIZED       1
#define WM_STATE_MAXIMIZED       2

/* ── Event types ────────────────────────────────────────────────── */
#define WM_EVENT_NONE            0
#define WM_EVENT_KEY             1
#define WM_EVENT_MOUSE_MOVE      2
#define WM_EVENT_MOUSE_DOWN      3
#define WM_EVENT_MOUSE_UP        4
#define WM_EVENT_CLOSE           5
#define WM_EVENT_FOCUS           6
#define WM_EVENT_SCROLL          7
#define WM_EVENT_RESIZE          8

/* ══════════════════════════════════════════════════════════════════
 *  COLOR PALETTE — macOS dark theme (gray/black)
 * ══════════════════════════════════════════════════════════════════ */

/* Window surfaces */
#define WM_COLOR_WINDOW_BG      0xFF2C2C2C
#define WM_COLOR_WINDOW_BG_ALT  0xFF282828
#define WM_COLOR_TITLEBAR       0xFF3C3C3C
#define WM_COLOR_TITLE_FOCUS    0xFF404040
#define WM_COLOR_TITLE_BLUR     0xFF323232
#define WM_COLOR_SEPARATOR      0xFF4A4A4A

/* Text */
#define WM_COLOR_TEXT           0xFFE8E8E8
#define WM_COLOR_TEXT_SECONDARY 0xFFB0B0B0
#define WM_COLOR_TEXT_DIM       0xFF808080
#define WM_COLOR_TEXT_BRIGHT    0xFFFFFFFF

/* Borders */
#define WM_COLOR_BORDER         0xFF505050
#define WM_COLOR_BORDER_FOCUS   0xFF707070

/* Accent */
#define WM_COLOR_ACCENT         0xFF707070
#define WM_COLOR_ACCENT_LIGHT   0xFF909090
#define WM_COLOR_ACCENT_DARK    0xFF505050
#define WM_COLOR_HOVER          0xFF484848
#define WM_COLOR_PRESSED        0xFF585858
#define WM_COLOR_SELECTION      0xFF707070

/* Traffic lights */
#define WM_TL_CLOSE_COLOR       0xFFFF5F57
#define WM_TL_MIN_COLOR         0xFFFFBD2E
#define WM_TL_MAX_COLOR         0xFF28C840
#define WM_TL_INACTIVE          0xFF606060

/* Buttons */
#define WM_COLOR_CLOSE_BTN      0xFFEF4444
#define WM_COLOR_CLOSE_HOVER    0xFFF87171
#define WM_COLOR_MIN_BTN        0xFF484848
#define WM_COLOR_MAX_BTN        0xFF484848

/* Desktop / dock */
#define WM_COLOR_DESKTOP_BG     0xFF242424
#define WM_COLOR_MENUBAR_BG     0xFF2C2C2C
#define WM_COLOR_MENUBAR_TEXT   0xFFE0E0E0
#define WM_COLOR_DOCK_BG        0xFF282828
#define WM_COLOR_DOCK_BORDER    0xFF404040
#define WM_COLOR_DOCK_INDICATOR 0xFF707070
#define WM_DOCK_INDICATOR_R     3

/* Compat aliases */
#define WM_COLOR_BG             0xFF1A1A1A
#define WM_COLOR_TASKBAR        0xFF242424
#define WM_COLOR_TASKBAR_BTN    0xFF383838
#define WM_COLOR_TASKBAR_ACT    0xFF707070
#define WM_COLOR_WHITE          0xFFFFFFFF
#define WM_COLOR_BLACK          0xFF000000

/* ── Forward declarations ───────────────────────────────────────── */
struct Window;

typedef void (*wm_draw_fn)(struct Window *win);
typedef void (*wm_key_fn)(struct Window *win, uint16_t key);
typedef void (*wm_mouse_fn)(struct Window *win, int lx, int ly,
                            bool left, bool right);
typedef void (*wm_close_fn)(struct Window *win);

/* ── Window structure ───────────────────────────────────────────── */
typedef struct Window {
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    int min_w, min_h;
    char title[64];
    int  state;
    bool visible;
    bool focused;
    int  z_order;
    int  app_id;
    bool is_chromeless;

    /* callbacks */
    wm_draw_fn   on_draw;
    wm_key_fn    on_key;
    wm_mouse_fn  on_mouse;
    wm_close_fn  on_close;

    /* extra state */
    bool    needs_redraw;
    uint8_t opacity;
    int     anim_phase;
    int     anim_frame;
    int     anim_total;
    int     anim_sx, anim_sy, anim_sw, anim_sh;

    void    *user_data;
    uint8_t  app_data[4096];
} Window;

/* ── Lifecycle ──────────────────────────────────────────────────── */
void     wm_init(int screen_w, int screen_h);
void     wm_set_screen_size(int sw, int sh);
bool     wm_resolution_changed(void);
Window  *wm_create_window(const char *title, int x, int y, int w, int h);
void     wm_destroy_window(Window *win);

/* ── Manipulation ───────────────────────────────────────────────── */
void     wm_focus_window(Window *win);
void     wm_clear_focus(void);
void     wm_minimize_window(Window *win);
void     wm_maximize_window(Window *win);
void     wm_restore_window(Window *win);
void     wm_bring_to_front(Window *win);

/* ── Settings ───────────────────────────────────────────────────── */
void     wm_load_settings(void);
void     wm_save_settings(void);

/* ── Drawing helpers (client-relative, clipped) ─────────────────── */
void     wm_draw_pixel(Window *w, int x, int y, uint32_t c);
void     wm_fill_rect(Window *w, int x, int y, int rw, int rh, uint32_t c);
void     wm_draw_rect(Window *w, int x, int y, int rw, int rh, uint32_t c);
void     wm_draw_char(Window *w, int x, int y, uint8_t ch, uint32_t fg, uint32_t bg);
void     wm_draw_string(Window *w, int x, int y, const char *s,
                         uint32_t fg, uint32_t bg);
void     wm_fill_rect_blend(Window *w, int x, int y, int rw, int rh,
                              uint32_t c, uint8_t a);
void     wm_draw_rounded_rect(Window *w, int x, int y, int rw, int rh,
                                uint32_t c, int r);

/* ── Main loop ──────────────────────────────────────────────────── */
void     wm_handle_input(void);
void     wm_draw_all(void);            /* windows + menubar          */
void     wm_draw_desktop_bg(void);
void     wm_invalidate_bg(void);
void     wm_draw_taskbar(void);        /* dock                       */
void     wm_draw_menubar(void);
void     wm_draw_cursor(int mx, int my);
void     wm_update_light(void);

/* ── Performance: Dirty flag system ─────────────────────────────── */
void     wm_invalidate_dock(void);
void     wm_invalidate_windows(void);
void     wm_invalidate_cursor(void);
void     wm_invalidate_all(void);

/* Dirty flags - exposed for desktop.c selective rendering */
extern bool wm_bg_dirty;
extern bool wm_dock_dirty;
extern bool wm_windows_dirty;
extern bool wm_cursor_dirty;
extern bool wm_full_redraw;

/* ── Dock ───────────────────────────────────────────────────────── */
#include "drivers/icons.h"
void     wm_dock_add(const char *name, icon_draw_fn fn);
void     wm_dock_clear(void);
bool     wm_dock_handle(int mx, int my, bool click);
int      wm_dock_slot_count(void);
extern int dock_clicked_slot;

/* ── Installer mode ─────────────────────────────────────────────── */
void     wm_set_installer_mode(bool enabled);
bool     wm_is_installer_mode(void);
void     wm_invalidate_windows(void);
void     wm_invalidate_all(void);
bool     wm_string_compare(const char *a, const char *b);

/* ── Geometry ───────────────────────────────────────────────────── */
int      wm_get_screen_w(void);
int      wm_get_screen_h(void);
int      wm_get_window_count(void);
Window  *wm_get_focused(void);
Window  *wm_get_window(int index);
int      wm_client_x(Window *w);
int      wm_client_y(Window *w);
int      wm_client_w(Window *w);
int      wm_client_h(Window *w);

extern volatile int desktop_exit_reason;

#endif