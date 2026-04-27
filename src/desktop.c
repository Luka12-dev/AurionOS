/*
 * Desktop Environment for Aurion OS
 * Main GUI entry point with app launcher and desktop management
*/

#include <stdint.h>
#include <stdbool.h>
#include "window_manager.h"
#include "menu_bar.h"
#include "drivers/mouse.h"
#include "drivers/icons.h"
#include "icon_loader.h"
#include "boot_screen.h"
#include "login_screen.h"

/* External functions */
extern uint32_t *gpu_setup_framebuffer(void);
extern int gpu_flush(void);
extern void gpu_clear(uint32_t color);
extern int gpu_get_width(void);
extern int gpu_get_height(void);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern void gpu_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern void hide_cursor(void);
extern uint16_t sys_getkey(void);
extern uint16_t c_getkey_nonblock(void);
extern int sys_kb_hit(void);
extern uint32_t get_ticks(void);
extern void set_text_mode(void);
extern int sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);

/* Terminal app */
extern void terminal_create(void);

/* GUI apps */
extern void app_notepad_create(void);
extern void app_paint_create(void);
extern void app_calc_create(void);
extern void app_sysinfo_create(void);
extern void app_filebrowser_create(void);
extern void app_clock_create(void);
extern void app_settings_create(void);
extern void app_snake_create(void);
extern void app_blaze_create(void);
extern void app_installer_create(void);
extern void app_3d_demo_create(void);

extern void terminal_edit_copy(Window *w);
extern void terminal_edit_cut(Window *w);
extern void terminal_edit_paste(Window *w);
extern void terminal_edit_undo(Window *w);
extern void notepad_edit_copy(Window *w);
extern void notepad_edit_cut(Window *w);
extern void notepad_edit_paste(Window *w);
extern void notepad_edit_undo(Window *w);

extern char os_clipboard[];
extern int  os_clipboard_len;

extern void gpu_fill_rect_blend(int x, int y, int w, int h, uint32_t c, uint8_t a);
extern int fs_mkdir_abs(const char *abs_path);
extern char keyboard_remap(char c);

/* Filesystem */
extern int fs_count;
typedef struct {
    char name[56];
    uint32_t size;
    uint8_t type;   /* 0=file, 1=dir */
    uint8_t attr;
    uint16_t parent_idx;
    uint16_t reserved;
} FSEntry;
extern FSEntry fs_table[];

/* String helpers */
static int desktop_str_cmp(const char *a, const char *b) {
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

static int desktop_str_len(const char *s) {
    int l = 0;
    while (s[l]) l++;
    return l;
}

static int desktop_str_startswith(const char *s, const char *prefix) {
    while (*prefix) {
        char cs = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
        char cp = (*prefix >= 'a' && *prefix <= 'z') ? *prefix - 32 : *prefix;
        if (cs != cp) return 0;
        s++; prefix++;
    }
    return 1;
}

/* Desktop app registry */
typedef struct {
    const char *name;
    void (*launcher)(void);
    icon_draw_fn icon_draw;
} DesktopApp;

static DesktopApp desktop_apps[] = {
    {"Terminal",      terminal_create,        icon_draw_png_terminal},
    {"File Explorer", app_filebrowser_create, icon_draw_png_files},
    {"Blaze",         app_blaze_create,       icon_draw_png_browser},
    {"Notepad",       app_notepad_create,     icon_draw_png_notepad},
    {"Paint",         app_paint_create,       icon_draw_png_paint},
    {"Calculator",    app_calc_create,        icon_draw_png_calculator},
    {"Clock",         app_clock_create,       icon_draw_png_clock},
    {"System Info",   app_sysinfo_create,     icon_draw_png_sysinfo},
    {"Settings",      app_settings_create,    icon_draw_png_settings},
    {"Snake",         app_snake_create,       icon_draw_png_snake},
    {"3D Demo",       app_3d_demo_create,     icon_draw_png_3d_demo},
};
#define NUM_APPS 11

/* Desktop file/folder items */
#define MAX_DESKTOP_ITEMS 32
typedef struct {
    char name[56];
    uint8_t type;
} DesktopItem;

static DesktopItem desktop_items[MAX_DESKTOP_ITEMS];
static int desktop_item_count = 0;

/* These need to be accessible from installer for post-install refresh */
void scan_desktop_items(void) {
    desktop_item_count = 0;
    const char *desktop_prefix = "/Desktop/";
    int prefix_len = 9;

    for (int i = 0; i < fs_count && desktop_item_count < MAX_DESKTOP_ITEMS; i++) {
        if (!desktop_str_startswith(fs_table[i].name, desktop_prefix)) continue;

        const char *item_name = fs_table[i].name + prefix_len;
        if (item_name[0] == 0) continue;

        /* Only direct children */
        bool has_sub = false;
        for (int k = 0; item_name[k]; k++) {
            if (item_name[k] == '/') { has_sub = true; break; }
        }
        if (has_sub) continue;

        /* Skip Applications folder */
        if (desktop_str_cmp(item_name, "Applications") == 0 &&
            fs_table[i].type == 1) continue;

        int j = 0;
        while (item_name[j] && j < 55) {
            desktop_items[desktop_item_count].name[j] = item_name[j];
            j++;
        }
        desktop_items[desktop_item_count].name[j] = 0;
        desktop_items[desktop_item_count].type = fs_table[i].type;
        desktop_item_count++;
    }
}

/* Hidden app mask */
static bool app_hidden[NUM_APPS];

static const char *hidden_prefix = "/Desktop/Applications/.hidden_";

void load_app_hidden_state(void) {
    for (int a = 0; a < NUM_APPS; a++) app_hidden[a] = false;

    int plen = 0;
    while (hidden_prefix[plen]) plen++;

    for (int i = 0; i < fs_count; i++) {
        if (!desktop_str_startswith(fs_table[i].name, hidden_prefix)) continue;
        const char *app_name_in_marker = fs_table[i].name + plen;
        for (int a = 0; a < NUM_APPS; a++) {
            if (desktop_str_cmp(desktop_apps[a].name, app_name_in_marker) == 0) {
                app_hidden[a] = true;
                break;
            }
        }
    }
}

/* Dock setup */
void setup_dock(void) {
    wm_dock_clear();
    for (int i = 0; i < NUM_APPS; i++) {
        if (app_hidden[i]) continue;
        wm_dock_add(desktop_apps[i].name, desktop_apps[i].icon_draw);
    }
}

/* Desktop icon drawing */
static void draw_desktop_icons(int screen_w, int screen_h) {
    int icon_w = 72;
    int icon_h = 80;
    int padding = 12;
    int start_x = 16;
    int start_y = 16;
    int col = 0;
    int row = 0;

    for (int i = 0; i < desktop_item_count; i++) {
        int ix = start_x + col * (icon_w + padding);
        int iy = start_y + row * (icon_h + padding);

        if (iy + icon_h > screen_h - WM_TASKBAR_HEIGHT - 16) {
            col++;
            row = 0;
            ix = start_x + col * (icon_w + padding);
            iy = start_y;
            if (ix + icon_w > screen_w - 16) break;
        }

        int icon_x = ix + (icon_w - 32) / 2;
        int icon_y = iy + 4;
        if (desktop_items[i].type == 1) {
            icon_draw_folder(icon_x, icon_y);
        } else {
            icon_draw_file(icon_x, icon_y);
        }

        char short_name[10];
        int j = 0;
        while (j < 9 && desktop_items[i].name[j]) {
            short_name[j] = desktop_items[i].name[j];
            j++;
        }
        short_name[j] = 0;
        int label_w = desktop_str_len(short_name);
        int lx = ix + (icon_w - label_w * 8) / 2;
        if (lx < ix) lx = ix;

        gpu_draw_string(lx + 1, iy + 41, (const uint8_t *)short_name, 0xFF000000, 0);
        gpu_draw_string(lx, iy + 40, (const uint8_t *)short_name, WM_COLOR_WHITE, 0);

        row++;
    }
    (void)screen_w;
}

/* ── Desktop context menu (rounded, macOS-style) ───────────────────── */
#define CTX_ROW_H     28
#define CTX_SEP_H     9
#define CTX_PAD       8
#define CTX_MW        268
#define CTX_RADIUS    12

static bool ctx_open       = false;
static int  ctx_x          = 0, ctx_y = 0;
static int  ctx_hover      = -1;
static int  desktop_needs_rescan = 0;

/* New Folder: name entry before create */
static bool desktop_nf_open   = false;
static char desktop_nf_buf[52];
static int  desktop_nf_len    = 0;

static char desktop_toast[96];
static int  desktop_toast_ttl = 0;

static int ctx_desktop_count(void) { return 5; }

static bool ctx_desktop_is_sep(int i) { return i == 1; }

static const char *ctx_desktop_text(int i) {
    switch (i) {
        case 0: return "Refresh Desktop";
        case 2: return "Desktop & Dock Settings...";
        case 3: return "Change Wallpaper...";
        case 4: return "New Folder";
        default: return NULL;
    }
}

static int ctx_menu_height(void) {
    int n  = ctx_desktop_count();
    int h  = CTX_PAD * 2;
    for (int i = 0; i < n; i++) {
        if (ctx_desktop_is_sep(i)) h += CTX_SEP_H;
        else h += CTX_ROW_H;
    }
    return h;
}

static int ctx_row_from_local_y(int ly) {
    int y = CTX_PAD;
    int n = ctx_desktop_count();
    for (int i = 0; i < n; i++) {
        int rh = ctx_desktop_is_sep(i) ? CTX_SEP_H : CTX_ROW_H;
        if (ly >= y && ly < y + rh) {
            if (ctx_desktop_is_sep(i)) return -1;
            return i;
        }
        y += rh;
    }
    return -2;
}

static int ctx_inset_px(int r, int row) {
    int dy = r - row, dx = 0, acc = r * r + r;
    while ((dx + 1) * (dx + 1) + dy * dy <= acc) dx++;
    return r - dx;
}

static void ctx_paint_rrect(int x, int y, int w, int h, uint32_t c, int rad) {
    if (w <= 0 || h <= 0) return;
    if (rad < 1) {
        gpu_fill_rect(x, y, w, h, c);
        return;
    }
    if (rad > w / 2) rad = w / 2;
    if (rad > h / 2) rad = h / 2;
    for (int i = 0; i < rad; i++) {
        int ins = ctx_inset_px(rad, i);
        int sw  = w - 2 * ins;
        if (sw > 0) {
            gpu_fill_rect(x + ins, y + i,         sw, 1, c);
            gpu_fill_rect(x + ins, y + h - 1 - i, sw, 1, c);
        }
    }
    if (h - 2 * rad > 0)
        gpu_fill_rect(x, y + rad, w, h - 2 * rad, c);
}

static void ctx_draw(int screen_w, int screen_h) {
    (void)screen_w;
    (void)screen_h;
    if (!ctx_open) return;

    int mh = ctx_menu_height();
    int mw = CTX_MW;
    int mx = ctx_x;
    int my = ctx_y;

    gpu_fill_rect_blend(mx + 4, my + 5, mw, mh, 0xFF000005, 25);
    gpu_fill_rect_blend(mx + 2, my + 3, mw, mh, 0xFF00000A, 40);
    gpu_fill_rect_blend(mx + 1, my + 1, mw, mh, 0xFF101015, 30);
    ctx_paint_rrect(mx, my, mw, mh, 0xF215151A, CTX_RADIUS);
    ctx_paint_rrect(mx + 1, my + 1, mw - 2, mh - 2, 0xFF121215, CTX_RADIUS - 1);

    int row_y = my + CTX_PAD;
    int n     = ctx_desktop_count();

    for (int i = 0; i < n; i++) {
        if (ctx_desktop_is_sep(i)) {
            gpu_fill_rect_blend(mx + 18, row_y + 3, mw - 36, 1, 0xFF606080, 90);
            row_y += CTX_SEP_H;
            continue;
        }
        int rh = CTX_ROW_H;
        bool hov = (i == ctx_hover);
        const char *label = ctx_desktop_text(i);
        if (!label) label = "";

        if (hov)
            ctx_paint_rrect(mx + 5, row_y + 1, mw - 10, rh - 2, 0xFF707070, 6);

        uint32_t fg = hov ? 0xFFFFFFFFu : 0xFFE8E8F4u;
        uint32_t bg = 0;
        gpu_draw_string(mx + 16, row_y + 9, (const uint8_t *)label, fg, bg);

        row_y += rh;
    }
}

static void desktop_show_toast(const char *msg) {
    int i = 0;
    while (msg[i] && i < 95) {
        desktop_toast[i] = msg[i];
        i++;
    }
    desktop_toast[i] = 0;
    desktop_toast_ttl = 180;
}

static void desktop_toast_draw(int screen_w, int screen_h) {
    if (desktop_toast_ttl <= 0 || desktop_toast[0] == 0) return;
    int len = 0;
    while (desktop_toast[len]) len++;
    int tw = len * 8 + 24;
    if (tw > screen_w - 20) tw = screen_w - 20;
    int tx = (screen_w - tw) / 2;
    int ty = screen_h - WM_TASKBAR_HEIGHT - 48;
    ctx_paint_rrect(tx, ty, tw, 30, 0xFF2A2C40, 10);
    gpu_draw_string(tx + 12, ty + 11, (const uint8_t *)desktop_toast, 0xFFE8E8F8u, 0);
}

static void desktop_nf_begin(void) {
    desktop_nf_open = true;
    const char *def = "New folder";
    int i = 0;
    while (def[i] && i < 51) {
        desktop_nf_buf[i] = def[i];
        i++;
    }
    desktop_nf_buf[i] = 0;
    desktop_nf_len = i;
    wm_clear_focus();
}

static void desktop_nf_draw(int screen_w, int screen_h) {
    if (!desktop_nf_open) return;
    int dw = 420, dh = 132;
    int dx = (screen_w - dw) / 2;
    int dy = (screen_h - dh) / 2 - 40;

    gpu_fill_rect_blend(dx + 4, dy + 5, dw, dh, 0xFF000018, 40);
    ctx_paint_rrect(dx, dy, dw, dh, 0xFF2A2C38, 14);
    ctx_paint_rrect(dx + 1, dy + 1, dw - 2, dh - 2, 0xFF1A1E2C, 13);

    gpu_draw_string(dx + 20, dy + 18, (const uint8_t *)"New Folder", 0xFFFFFFFFu, 0);
    gpu_draw_string(dx + 20, dy + 44, (const uint8_t *)"Type a name (Enter = create, Esc = cancel):",
                    0xFFC0C0D8u, 0);

    int field_y = dy + 68, field_h = 26;
    gpu_fill_rect(dx + 18, field_y, dw - 36, field_h, 0xFF10141E);
    ctx_paint_rrect(dx + 18, field_y, dw - 36, field_h, 0xFF3A3E50, 6);
    gpu_draw_string(dx + 26, field_y + 9, (const uint8_t *)desktop_nf_buf,
                    0xFFE8E8F0u, 0);

    int blink = (get_ticks() / 30) % 2;
    if (blink) {
        int label_w = 0;
        while (desktop_nf_buf[label_w]) label_w++;
        int cx = dx + 26 + label_w * 8;
        gpu_fill_rect(cx, field_y + 5, 2, 16, 0xFF909090);
    }
}

/* Returns true if key was consumed */
static bool desktop_nf_handle_key(uint16_t key) {
    if (!desktop_nf_open) return false;

    uint8_t lo = (uint8_t)(key & 0xFF);

    if (lo == 27) {
        desktop_nf_open = false;
        return true;
    }

    if (lo == 13) {
        char name[52];
        int nlen = 0;
        while (desktop_nf_buf[nlen] && nlen < 51) {
            char c = desktop_nf_buf[nlen];
            if (c != ' ' && c != '\t') break;
            nlen++;
        }
        int start = nlen;
        nlen = 0;
        while (desktop_nf_buf[start + nlen] && nlen < 51) {
            char c = desktop_nf_buf[start + nlen];
            if (c == '/' || c == '\\') {
                desktop_show_toast("Name cannot contain slashes.");
                return true;
            }
            name[nlen++] = c;
        }
        while (nlen > 0 && (name[nlen - 1] == ' ' || name[nlen - 1] == '\t'))
            nlen--;
        name[nlen] = 0;

        if (nlen == 0) {
            desktop_show_toast("Enter a folder name.");
            return true;
        }

        char full_path[96];
        const char *prefix = "/Desktop/";
        int pi = 0;
        while (prefix[pi] && pi < 95) { full_path[pi] = prefix[pi]; pi++; }
        for (int u = 0; u < nlen && pi < 95; u++) full_path[pi++] = name[u];
        full_path[pi] = 0;

        for (int i = 0; i < fs_count; i++) {
            if (desktop_str_cmp(fs_table[i].name, full_path) == 0) {
                desktop_show_toast("A folder or file with that name already exists.");
                return true;
            }
        }

        int rc = fs_mkdir_abs(full_path);
        if (rc == -3) {
            desktop_show_toast("A folder or file with that name already exists.");
            return true;
        }
        if (rc != 0) {
            desktop_show_toast("Could not create folder.");
            return true;
        }

        desktop_nf_open = false;
        desktop_needs_rescan = 1;
        return true;
    }

    if (lo == 8 || lo == 127) {
        if (desktop_nf_len > 0) {
            desktop_nf_len--;
            desktop_nf_buf[desktop_nf_len] = 0;
        }
        return true;
    }

    if (lo >= 32 && lo < 127 && desktop_nf_len < 50) {
        char c = keyboard_remap((char)lo);
        if (c >= 32 && c < 127) {
            desktop_nf_buf[desktop_nf_len++] = c;
            desktop_nf_buf[desktop_nf_len] = 0;
        }
        return true;
    }

    return true;
}

static bool ctx_handle(int mx, int my, bool click, bool right_click,
                       int screen_w, int screen_h) {
    if (desktop_nf_open) {
        if (click) {
            int dw = 420, dh = 132;
            int dx = (screen_w - dw) / 2;
            int dy = (screen_h - dh) / 2 - 40;
            if (mx < dx || mx >= dx + dw || my < dy || my >= dy + dh)
                desktop_nf_open = false;
        }
        return false;
    }

    if (right_click && !ctx_open) {
        bool over_window = false;
        for (int i = 0; i < wm_get_window_count(); i++) {
            Window *w = wm_get_window(i);
            if (!w || !w->visible || w->state == WM_STATE_MINIMIZED) continue;
            if (mx >= w->x && mx < w->x + w->w &&
                my >= w->y && my < w->y + w->h) {
                over_window = true;
                break;
            }
        }
        if (over_window) return false;

        ctx_open = true;

        int pw = CTX_MW;
        int ph = ctx_menu_height();
        ctx_x = mx;
        ctx_y = my;
        if (ctx_x + pw > screen_w) ctx_x = screen_w - pw - 4;
        if (ctx_y + ph > screen_h) ctx_y = screen_h - ph - 4;
        if (ctx_x < 4) ctx_x = 4;
        if (ctx_y < WM_MENUBAR_HEIGHT + 4) ctx_y = WM_MENUBAR_HEIGHT + 4;

        ctx_hover = -1;
        return true;
    }

    if (!ctx_open) return false;

    int mh = ctx_menu_height();
    int mw = CTX_MW;

    if (click && (mx < ctx_x || mx >= ctx_x + mw || my < ctx_y || my >= ctx_y + mh)) {
        ctx_open = false;
        return false;
    }

    ctx_hover = -1;
    if (mx >= ctx_x && mx < ctx_x + mw && my >= ctx_y && my < ctx_y + mh)
        ctx_hover = ctx_row_from_local_y(my - ctx_y);

    if (click) {
        if (ctx_hover == 0) {
            scan_desktop_items();
            desktop_needs_rescan = 0;
            desktop_show_toast("Desktop refreshed.");
        }
        else if (ctx_hover == 2 || ctx_hover == 3) app_settings_create();
        else if (ctx_hover == 4) desktop_nf_begin();
        ctx_open = false;
        return true;
    }

    return true;
}

static bool desktop_title_matches_app(Window *fw, const char *appname)
{
    if (!fw || !appname) return false;
    const char *t = fw->title;
    int k = 0;
    for (; appname[k]; k++) {
        char a = t[k], b = appname[k];
        if (!a) return false;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;
    }
    return t[k] == 0 || t[k] == ' ' || t[k] == '-' || t[k] == '(';
}

static void menu_new_window(void) {
    extern Window *wm_get_focused(void);
    Window *fw = wm_get_focused();
    if (fw && fw->visible) {
        if (desktop_str_startswith(fw->title, "Aurion OS Setup")) {
            terminal_create();
            return;
        }
        for (int i = 0; i < NUM_APPS; i++) {
            if (app_hidden[i]) continue;
            if (desktop_title_matches_app(fw, desktop_apps[i].name)) {
                desktop_apps[i].launcher();
                return;
            }
        }
    }
    if (!app_hidden[0]) terminal_create();
}

static void menu_close_window(void) {
    extern Window *wm_get_focused(void);
    extern void wm_destroy_window(Window *win);
    Window *fw = wm_get_focused();
    if (fw && fw->visible) {
        if (fw->on_close) fw->on_close(fw);
        wm_destroy_window(fw);
    }
}

static void menu_quit(void) {
    extern volatile int desktop_exit_reason;
    desktop_exit_reason = 0;
}

static void menu_undo(void) {
    extern Window *wm_get_focused(void);
    Window *fw = wm_get_focused();
    if (!fw || !fw->visible) return;
    if (desktop_title_matches_app(fw, "Terminal"))
        terminal_edit_undo(fw);
    else if (desktop_title_matches_app(fw, "Notepad"))
        notepad_edit_undo(fw);
}

static void menu_cut(void) {
    extern Window *wm_get_focused(void);
    Window *fw = wm_get_focused();
    if (!fw || !fw->visible) return;
    if (desktop_title_matches_app(fw, "Terminal"))
        terminal_edit_cut(fw);
    else if (desktop_title_matches_app(fw, "Notepad"))
        notepad_edit_cut(fw);
}

static void menu_copy(void) {
    extern Window *wm_get_focused(void);
    Window *fw = wm_get_focused();
    if (!fw || !fw->visible) return;
    if (desktop_title_matches_app(fw, "Terminal"))
        terminal_edit_copy(fw);
    else if (desktop_title_matches_app(fw, "Notepad"))
        notepad_edit_copy(fw);
}

static void menu_paste(void) {
    extern Window *wm_get_focused(void);
    Window *fw = wm_get_focused();
    if (!fw || !fw->visible) return;
    if (desktop_title_matches_app(fw, "Terminal"))
        terminal_edit_paste(fw);
    else if (desktop_title_matches_app(fw, "Notepad"))
        notepad_edit_paste(fw);
    else if (os_clipboard_len > 0 && fw->on_key) {
        for (int i = 0; i < os_clipboard_len && i < 4096; i++) {
            char c = os_clipboard[i];
            if (c == '\n' || c == '\r') continue;
            fw->on_key(fw, (uint16_t)(unsigned char)c);
        }
    }
}

static void menu_zoom_in(void) {
    extern Window *wm_get_focused(void);
    Window *fw = wm_get_focused();
    if (fw && fw->visible && fw->state == WM_STATE_NORMAL) {
        int new_w = fw->w + fw->w / 10;
        int new_h = fw->h + fw->h / 10;
        if (new_w < 1920 && new_h < 1080) {
            fw->w = new_w;
            fw->h = new_h;
            fw->needs_redraw = true;
        }
    }
}

static void menu_zoom_out(void) {
    extern Window *wm_get_focused(void);
    Window *fw = wm_get_focused();
    if (fw && fw->visible && fw->state == WM_STATE_NORMAL) {
        int new_w = fw->w - fw->w / 10;
        int new_h = fw->h - fw->h / 10;
        if (new_w > 200 && new_h > 150) {
            fw->w = new_w;
            fw->h = new_h;
            fw->needs_redraw = true;
        }
    }
}

static void menu_fullscreen(void) {
    extern Window *wm_get_focused(void);
    extern void wm_maximize_window(Window *win);
    extern void wm_restore_window(Window *win);
    Window *fw = wm_get_focused();
    if (fw && fw->visible) {
        if (fw->state == WM_STATE_MAXIMIZED)
            wm_restore_window(fw);
        else
            wm_maximize_window(fw);
    }
}

/* Check installation status */
static bool check_installed(void) {
    extern int load_file_content(const char *filename, char *buffer, int max_len);
#ifdef DEBUG_SKIP_INSTALL
    extern void c_puts(const char *s);
    c_puts("[DESKTOP] DEBUG_SKIP_INSTALL - skipping installer\n");
    return true;
#else
    extern void c_puts(const char *s);
    extern int fs_count;

    c_puts("[DESKTOP] Checking installation status...\n");

    if (fs_count < 5) {
        c_puts("[DESKTOP] Filesystem nearly empty - NOT INSTALLED\n");
        return false;
    }

    char marker[20];
    for (int i = 0; i < 20; i++) marker[i] = 0;

    int found = load_file_content("/installed.sys", marker, 16);

    if (found <= 0) {
        c_puts("[DESKTOP] /installed.sys not found - NOT INSTALLED\n");
        return false;
    }

    const char *expected = "AURION_INSTALLED";
    for (int i = 0; i < 16; i++) {
        if (marker[i] != expected[i]) {
            c_puts("[DESKTOP] /installed.sys content invalid - NOT INSTALLED\n");
            return false;
        }
    }

    c_puts("[DESKTOP] Valid /installed.sys found - INSTALLED\n");
    return true;
#endif
}

/* Main desktop entry point */
int desktop_main(void) {
    extern void c_puts(const char *s);
    extern bool gpu_is_vesa(void);
    extern void cmd_init(void);

    extern int ata_init(void);
    ata_init();

    cmd_init();
    wm_load_settings();

    extern bool vmware_svga_detect(void);
    extern bool vmware_svga_available(void);
    extern bool vmware_svga_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
    extern void vesa_reinit_mode(void);

    vmware_svga_detect();

    bool installed = check_installed();

    uint16_t target_w = 1920;
    uint16_t target_h = 1080;

    if (installed) {
        int res_idx = g_settings.resolution;
        uint16_t widths[]  = {800, 1024, 1280, 1280, 1440, 1600, 1920, 2560};
        uint16_t heights[] = {600,  768,  720, 1024,  900,  900, 1080, 1440};

        if (res_idx >= 0 && res_idx < 8) {
            target_w = widths[res_idx];
            target_h = heights[res_idx];
        } else {
            target_w = 1024;
            target_h = 768;
        }
    }

    bool vmware_accel = vmware_svga_available();
    if (vmware_accel) {
        vmware_svga_set_mode(target_w, target_h, 32);
    } else {
        *(uint16_t *)(0x9004) = target_w;
        *(uint16_t *)(0x9006) = target_h;
        vesa_reinit_mode();
    }

    extern void io_wait(void);
    for (int i = 0; i < 100; i++) io_wait();

    uint32_t *fb = gpu_setup_framebuffer();
    if (!fb) return 0;

    gpu_clear(0xFF000000);

    int sw = gpu_get_width();
    int sh = gpu_get_height();

    if (sw <= 0) sw = target_w;
    if (sh <= 0) sh = target_h;

    wm_init(sw, sh);
    mouse_init();
    mouse_set_bounds(sw, sh);

    if (fs_count < 0 || fs_count > 128) fs_count = 0;

    icons_load_all();
    boot_screen_show();

    if (installed) {
        /* Only draw background before login - login screen will capture this for blur.
         * DON'T draw taskbar here, otherwise it gets baked into the blur AND drawn over it (Double Dock). */
        wm_draw_desktop_bg();
        gpu_flush();

#ifndef DEBUG_SKIP_INSTALL
        if (!login_screen_show()) return 0;
#endif

        menubar_init();

        static MenuItem file_menu[] = {
            {"New Window",   menu_new_window,   false, true},
            {"",             NULL,              true,  false},
            {"Close Window", menu_close_window, false, true},
            {"",             NULL,              true,  false},
            {"Quit",         menu_quit,         false, true}
        };
        menubar_add_menu("File", file_menu, 5);

        static MenuItem edit_menu[] = {
            {"Undo",  menu_undo,  false, true},
            {"",      NULL,       true,  false},
            {"Cut",   menu_cut,   false, true},
            {"Copy",  menu_copy,  false, true},
            {"Paste", menu_paste, false, true}
        };
        menubar_add_menu("Edit", edit_menu, 5);

        static MenuItem view_menu[] = {
            {"Zoom In",     menu_zoom_in,    false, true},
            {"Zoom Out",    menu_zoom_out,   false, true},
            {"",            NULL,            true,  false},
            {"Full Screen", menu_fullscreen, false, true}
        };
        menubar_add_menu("View", view_menu, 4);

        load_app_hidden_state();
        scan_desktop_items();
        setup_dock();

        if (g_settings.startup_app_idx >= 0 &&
            g_settings.startup_app_idx < NUM_APPS)
            desktop_apps[g_settings.startup_app_idx].launcher();

    } else {
        wm_set_installer_mode(true);
        app_installer_create();
    }

    /* ── Tracking state ─────────────────────────────────── */
    bool     prev_left  = false;
    bool     prev_right = false;
    uint32_t last_scan_tick  = 0;
    uint32_t last_frame_tick = get_ticks() - 1;

    /* Previous cursor position so we know when it actually moved */
    int prev_mx = -1, prev_my = -1;

    int dock_to_app[NUM_APPS];
    {
        int d = 0;
        for (int i = 0; i < NUM_APPS; i++)
            if (!app_hidden[i]) dock_to_app[d++] = i;
    }

    /* Force a complete first frame */
    wm_invalidate_all();

    /* ════════════════════════════════════════════════════════
     * MAIN LOOP
     *
     * Rule: the backbuffer is redrawn from scratch every frame
     * (bg → windows → dock → overlays → cursor), then flushed
     * to the hardware LFB once.  This is the only reliable way
     * to prevent cursor trails and scan-line corruption.
     *
     * The dirty-flag system inside the WM is kept for future
     * partial-update optimisations but must NOT gate the flush.
     * ════════════════════════════════════════════════════════ */
    while (desktop_exit_reason < 0) {

        /* ── Resolution change ──────────────────────────── */
        extern bool wm_resolution_changed(void);
        if (wm_resolution_changed()) {
            sw = gpu_get_width();
            sh = gpu_get_height();
            mouse_set_bounds(sw, sh);
            prev_mx = -1; prev_my = -1;
        }

        /* ── Frame limiter: one render per timer tick ───── */
        uint32_t now = get_ticks();
        if (now == last_frame_tick) {
            __asm__ volatile("sti\nhlt");
            continue;
        }
        last_frame_tick = now;

        /* ── Input: mouse ───────────────────────────────── */
        for (int _mp = 0; _mp < 10; _mp++) mouse_poll();
        int  mx          = mouse_get_x();
        int  my          = mouse_get_y();
        bool left        = mouse_get_left();
        bool right       = mouse_get_right();
        bool click       = left  && !prev_left;
        bool right_click = right && !prev_right;

        /* Detect cursor movement */
        bool cursor_moved = (mx != prev_mx || my != prev_my);
        prev_mx = mx;
        prev_my = my;

        /* ── Menu bar (installer skips this) ────────────── */
        extern bool wm_is_installer_mode(void);
        if (!wm_is_installer_mode()) {
            if (menubar_handle_mouse(mx, my, left, prev_left)) {
                prev_left  = left;
                prev_right = right;
                goto draw_frame;
            }
        }

        /* ── Periodic desktop rescan ─────────────────────── */
        if (now - last_scan_tick > 1000) {
            scan_desktop_items();
            last_scan_tick = now;
        }
        if (desktop_needs_rescan) {
            scan_desktop_items();
            desktop_needs_rescan = 0;
        }

        /* ── Context menu ───────────────────────────────── */
        if (ctx_handle(mx, my, click, right_click, sw, sh)) {
            setup_dock();
            int d = 0;
            for (int i = 0; i < NUM_APPS; i++)
                if (!app_hidden[i]) dock_to_app[d++] = i;
            prev_left  = left;
            prev_right = right;
            goto draw_frame;
        }

        /* ── Dock ───────────────────────────────────────── */
        if (!wm_is_installer_mode()) {
            if (wm_dock_handle(mx, my, left && !prev_left)) {
                if (dock_clicked_slot >= 0) {
                    int slot    = dock_clicked_slot;
                    dock_clicked_slot = -1;
                    int app_idx = dock_to_app[slot];
                    if (app_idx >= 0 && app_idx < NUM_APPS) {
                        bool found = false;
                        int  wc    = wm_get_window_count();
                        for (int i = 0; i < wc; i++) {
                            Window *w = wm_get_window(i);
                            if (w && w->visible &&
                                wm_string_compare(w->title,
                                    desktop_apps[app_idx].name)) {
                                wm_focus_window(w);
                                found = true;
                                break;
                            }
                        }
                        if (!found) desktop_apps[app_idx].launcher();
                    }
                }
                prev_left  = left;
                prev_right = right;
                goto draw_frame;
            }
            dock_clicked_slot = -1;
        } else {
            int d = 0;
            for (int i = 0; i < NUM_APPS; i++)
                if (!app_hidden[i]) dock_to_app[d++] = i;
        }

        /* ── Toast timer ─────────────────────────────────── */
        if (desktop_toast_ttl > 0) desktop_toast_ttl--;

        /* ── New-folder keyboard input ───────────────────── */
        if (desktop_nf_open) {
            while (sys_kb_hit()) {
                uint16_t key = sys_getkey();
                if ((key & 0xFF) != 0) {
                    char m = keyboard_remap((char)(key & 0xFF));
                    key = (key & 0xFF00) | (uint8_t)m;
                }
                desktop_nf_handle_key(key);
            }
        }

        /* ── Window-manager input ────────────────────────── */
        wm_handle_input();

        /* ── Unhandled keys (no focused window) ──────────── */
        if (sys_kb_hit() && !wm_get_focused() && !desktop_nf_open) {
            uint16_t key = sys_getkey();
            (void)key;
        }

        prev_left  = left;
        prev_right = right;

        /* ── Mark dirty when cursor moves so bg is redrawn ─ */
        if (cursor_moved) {
            wm_invalidate_all();
        }

draw_frame:
        /* ════════════════════════════════════════════════
         * RENDER — always draw everything to the backbuffer,
         * always flush.  Never skip the flush; skipping it
         * is what causes the cursor trail and scan-line
         * artefacts seen in the screenshot.
         * ════════════════════════════════════════════════ */

        /* 1. Desktop background (wallpaper or gradient) */
        wm_draw_desktop_bg();

        /* 2. Desktop file/folder icons */
        if (!wm_is_installer_mode())
            draw_desktop_icons(sw, sh);

        /* 3. All windows (z-ordered) */
        wm_draw_all();

        /* 4. Dock, context menu, overlays */
        if (!wm_is_installer_mode()) {
            wm_draw_taskbar();
            ctx_draw(sw, sh);
            desktop_nf_draw(sw, sh);
            desktop_toast_draw(sw, sh);
        }

        /* 5. Hardware mouse cursor (always last, always on top) */
        wm_draw_cursor(mx, my);

        /* 6. Flush backbuffer → hardware LFB — UNCONDITIONAL */
        gpu_flush();
    }

    /* ── Exit ───────────────────────────────────────────── */
    int reason = desktop_exit_reason;
    desktop_exit_reason = -1;

    if (reason == 0) {
        extern void set_cursor_pos(int row, int col);
        extern void c_cls(void);
        extern void io_wait(void);

        set_text_mode();
        for (int i = 0; i < 10000; i++) io_wait();
        c_cls();
        for (int i = 0; i < 5000; i++) io_wait();
        c_cls();
        for (int i = 0; i < 5000; i++) io_wait();
        c_cls();
        set_cursor_pos(0, 0);
    }

    return reason;
}