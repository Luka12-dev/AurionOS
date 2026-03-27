#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "settings_state.h"

#define WM_MAX_WINDOWS 16
#define WM_TITLEBAR_HEIGHT 32
#define WM_BORDER_WIDTH 1
#define WM_TASKBAR_HEIGHT 76  /* dock height: 52px icon + 16px padding + 8px gap */
#define WM_BTN_SIZE 20

/* Window states */
#define WM_STATE_NORMAL    0
#define WM_STATE_MINIMIZED 1
#define WM_STATE_MAXIMIZED 2

/* Window event types */
#define WM_EVENT_NONE       0
#define WM_EVENT_KEY        1
#define WM_EVENT_MOUSE_MOVE 2
#define WM_EVENT_MOUSE_DOWN 3
#define WM_EVENT_MOUSE_UP   4
#define WM_EVENT_CLOSE      5
#define WM_EVENT_FOCUS      6

/* Colors - Premium dark theme with refined accents */
#define WM_COLOR_BG          0xFF0F0F1A  /* Deep dark background */
#define WM_COLOR_TASKBAR     0xFF1A1A2C  /* Taskbar background */
#define WM_COLOR_TASKBAR_BTN 0xFF2A2A42  /* Taskbar button */
#define WM_COLOR_TASKBAR_ACT 0xFF6366F1  /* Active taskbar button - indigo */
#define WM_COLOR_TITLEBAR    0xFF1C1C30  /* Window title bar (unfocused) */
#define WM_COLOR_TITLE_FOCUS 0xFF252540  /* Focused window title */
#define WM_COLOR_WINDOW_BG   0xFF1A1A28  /* Window client area */
#define WM_COLOR_BORDER      0xFF2A2A40  /* Window border */
#define WM_COLOR_BORDER_FOCUS 0xFF6366F1 /* Focused border accent - indigo */
#define WM_COLOR_TEXT        0xFFE8E8F4  /* Primary text */
#define WM_COLOR_TEXT_DIM    0xFF6B6B8A  /* Dimmed text */
#define WM_COLOR_CLOSE_BTN   0xFFEF4444  /* Close button red */
#define WM_COLOR_CLOSE_HOVER 0xFFF87171  /* Close hover */
#define WM_COLOR_MIN_BTN     0xFF3B3B5A  /* Minimize button */
#define WM_COLOR_MAX_BTN     0xFF3B3B5A  /* Maximize button */
#define WM_COLOR_ACCENT      0xFF6366F1  /* Accent indigo */
#define WM_COLOR_WHITE       0xFFFFFFFF
#define WM_COLOR_BLACK       0xFF000000
#define WM_COLOR_DESKTOP_BG  0xFF0A0A16  /* Desktop wallpaper base */

struct Window;

typedef void (*wm_draw_fn)(struct Window *win);
typedef void (*wm_key_fn)(struct Window *win, uint16_t key);
typedef void (*wm_mouse_fn)(struct Window *win, int lx, int ly, bool left, bool right);
typedef void (*wm_close_fn)(struct Window *win);

typedef struct Window {
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    int min_w, min_h;
    char title[64];
    int state;
    bool visible;
    bool focused;
    int z_order;
    int app_id;

    /* App callbacks */
    wm_draw_fn   on_draw;
    wm_key_fn    on_key;
    wm_mouse_fn  on_mouse;
    wm_close_fn  on_close;

    /* App-specific data */
    void *user_data;
    uint8_t app_data[4096];
} Window;

/* Window Manager API */
void wm_init(int screen_w, int screen_h);
void wm_load_settings(void);
void wm_save_settings(void);
Window *wm_create_window(const char *title, int x, int y, int w, int h);
void wm_destroy_window(Window *win);
void wm_focus_window(Window *win);
void wm_minimize_window(Window *win);
void wm_maximize_window(Window *win);
void wm_restore_window(Window *win);
void wm_bring_to_front(Window *win);

/* Drawing helpers (coordinates relative to window client area) */
void wm_draw_pixel(Window *win, int x, int y, uint32_t color);
void wm_fill_rect(Window *win, int x, int y, int w, int h, uint32_t color);
void wm_draw_rect(Window *win, int x, int y, int w, int h, uint32_t color);
void wm_draw_char(Window *win, int x, int y, uint8_t c, uint32_t fg, uint32_t bg);
void wm_draw_string(Window *win, int x, int y, const char *str, uint32_t fg, uint32_t bg);

/* Main loop helpers */
void wm_handle_input(void);
void wm_draw_all(void);
void wm_draw_desktop_bg(void);
void wm_invalidate_bg(void);
void wm_draw_taskbar(void);
void wm_draw_cursor(int mx, int my);

/* Dock API */
#include "drivers/icons.h"
void wm_dock_add(const char *name, icon_draw_fn fn);
void wm_dock_clear(void);
bool wm_dock_handle(int mx, int my, bool click);

/* Dock click signal - set by wm_dock_handle, read+cleared by desktop */
extern int dock_clicked_slot;

/* Getters */
int wm_get_screen_w(void);
int wm_get_screen_h(void);
int wm_get_window_count(void);
Window *wm_get_focused(void);
Window *wm_get_window(int index);
int wm_client_x(Window *win);
int wm_client_y(Window *win);
int wm_client_w(Window *win);
int wm_client_h(Window *win);

/* Desktop mode flag */
extern volatile int desktop_exit_reason;

#endif
