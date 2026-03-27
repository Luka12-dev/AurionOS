/*
 * Desktop Environment for Aurion OS
 * Main GUI entry point with app launcher and desktop management
*/

#include <stdint.h>
#include <stdbool.h>
#include "window_manager.h"
#include "drivers/mouse.h"
#include "drivers/icons.h"
#include "icon_loader.h"

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
extern uint16_t c_getkey(void);
extern uint16_t c_getkey_nonblock(void);
extern int c_kb_hit(void);
extern uint32_t get_ticks(void);
extern void set_text_mode(void);
extern int sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);

/* Terminal app */
extern void terminal_create(void);

/* Existing GUI apps */
extern void app_notepad_create(void);
extern void app_paint_create(void);
extern void app_calc_create(void);
extern void app_sysinfo_create(void);
extern void app_filebrowser_create(void);
extern void app_clock_create(void);
extern void app_settings_create(void);
extern void app_snake_create(void);

/* Filesystem access */
extern int fs_count;
typedef struct {
    char name[56];
    uint32_t size;
    uint8_t type; /* 0=file, 1=dir */
    uint8_t attr;
    uint16_t parent_idx;
    uint16_t reserved;
} FSEntry;
extern FSEntry fs_table[];

/* String helpers */
static int desktop_str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int desktop_str_len(const char *s) {
    int l = 0; while (s[l]) l++; return l;
}
static int desktop_str_startswith(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

/* Start menu removed - replaced by dock */

/* Desktop icon layout */
typedef struct {
    const char *name;
    void (*launcher)(void);
    icon_draw_fn icon_draw;
} DesktopApp;

static DesktopApp desktop_apps[] = {
    {"Terminal",     terminal_create,        icon_draw_png_terminal},
    {"Notepad",      app_notepad_create,     icon_draw_png_notepad},
    {"Paint",        app_paint_create,       icon_draw_png_paint},
    {"Calculator",   app_calc_create,        icon_draw_png_calculator},
    {"Files",        app_filebrowser_create, icon_draw_png_files},
    {"Clock",        app_clock_create,       icon_draw_png_clock},
    {"System Info",  app_sysinfo_create,     icon_draw_png_sysinfo},
    {"Settings",     app_settings_create,    icon_draw_png_settings},
    {"Snake",        app_snake_create,       icon_draw_png_snake},
};
#define NUM_APPS 9

/* Desktop file/folder items from C:\Desktop */
#define MAX_DESKTOP_ITEMS 32
typedef struct {
    char name[56];
    uint8_t type; /* 0=file, 1=dir */
} DesktopItem;

static DesktopItem desktop_items[MAX_DESKTOP_ITEMS];
static int desktop_item_count = 0;

/* Scan filesystem for items in C:\Desktop (direct children only, not subfolders) */
static void scan_desktop_items(void) {
    desktop_item_count = 0;
    const char *desktop_prefix = "C:\\Desktop\\";
    int prefix_len = 11;

    for (int i = 0; i < fs_count && desktop_item_count < MAX_DESKTOP_ITEMS; i++) {
        if (!desktop_str_startswith(fs_table[i].name, desktop_prefix)) continue;

        /* Extract the part after C:\Desktop\ */
        const char *item_name = fs_table[i].name + prefix_len;
        if (item_name[0] == 0) continue; /* the Desktop dir itself */

        /* Only show DIRECT children of C:\Desktop\ - skip anything that has
           another backslash (i.e. stuff inside subdirectories like Applications\) */
        bool has_sub = false;
        for (int k = 0; item_name[k]; k++) {
            if (item_name[k] == '\\') { has_sub = true; break; }
        }
        if (has_sub) continue; /* skip Applications\ and its contents */

        /* Skip the Applications folder itself - it's an OS-managed dir */
        if (desktop_str_cmp(item_name, "Applications") == 0 &&
            fs_table[i].type == 1) continue;

        /* Good - direct child file or folder on the desktop */
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

/* Removed/hidden app mask - loaded from disk on startup */
static bool app_hidden[NUM_APPS];


/* Register all visible apps into the dock */
static void setup_dock(void) {
    wm_dock_clear();
    for (int i = 0; i < NUM_APPS; i++) {
        if (app_hidden[i]) continue;
        wm_dock_add(desktop_apps[i].name, desktop_apps[i].icon_draw);
    }
}

/* The deletion marker prefix stored in the filesystem */
static const char *hidden_prefix = "C:\\Desktop\\Applications\\.hidden_";

/* Load hidden state from filesystem markers */
static void load_app_hidden_state(void) {
    for (int a = 0; a < NUM_APPS; a++) app_hidden[a] = false;

    /* Build the prefix length */
    int plen = 0;
    while (hidden_prefix[plen]) plen++;

    for (int i = 0; i < fs_count; i++) {
        if (!desktop_str_startswith(fs_table[i].name, hidden_prefix)) continue;
        /* The part after the prefix is the app name */
        const char *app_name_in_marker = fs_table[i].name + plen;
        for (int a = 0; a < NUM_APPS; a++) {
            if (desktop_str_cmp(desktop_apps[a].name, app_name_in_marker) == 0) {
                app_hidden[a] = true;
                break;
            }
        }
    }
}

/* Write a hidden marker for an app to persist deletion across reboots */
static void save_app_hidden(int app_idx) {
    if (app_idx < 0 || app_idx >= NUM_APPS) return;

    /* Build marker path: hidden_prefix + app name */
    char marker[80];
    int pi = 0;
    while (hidden_prefix[pi] && pi < 79) { marker[pi] = hidden_prefix[pi]; pi++; }
    const char *app_name = desktop_apps[app_idx].name;
    int ni = 0;
    while (app_name[ni] && pi < 79) { marker[pi++] = app_name[ni++]; }
    marker[pi] = 0;

    /* Check if marker already exists */
    for (int i = 0; i < fs_count; i++) {
        if (desktop_str_cmp(fs_table[i].name, marker) == 0) return; /* already there */
    }

    /* Add to filesystem */
    if (fs_count < 128 /* FS_MAX_FILES */) {
        int j = 0;
        while (marker[j] && j < 55) { fs_table[fs_count].name[j] = marker[j]; j++; }
        fs_table[fs_count].name[j] = 0;
        fs_table[fs_count].size = 0;
        fs_table[fs_count].type = 0;
        fs_table[fs_count].attr = 0x02; /* hidden attr */
        fs_count++;
        extern int fs_save_to_disk(void);
        fs_save_to_disk();
    }
}

/* Draw desktop file/folder items only - app icons live in the dock now */
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

        /* Draw file or folder icon */
        int icon_x = ix + (icon_w - 32) / 2;
        int icon_y = iy + 4;
        if (desktop_items[i].type == 1) {
            icon_draw_folder(icon_x, icon_y);
        } else {
            icon_draw_file(icon_x, icon_y);
        }

        /* Label (truncate to fit) */
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

        /* Text shadow + label */
        gpu_draw_string(lx + 1, iy + 41, (const uint8_t *)short_name, 0xFF000000, 0);
        gpu_draw_string(lx, iy + 40, (const uint8_t *)short_name, WM_COLOR_WHITE, 0);

        row++;
    }
    (void)screen_w;
}

/* Check desktop icon clicks - returns app index or -1 */
static int check_icon_click(int mx, int my, int screen_h) {
    int icon_w = 72;
    int icon_h = 80;
    int padding = 12;
    int start_x = 16;
    int start_y = 16;
    int col = 0, row = 0;

    for (int i = 0; i < NUM_APPS; i++) {
        /* Must mirror draw_desktop_icons exactly: hidden apps still advance row */
        if (app_hidden[i]) { row++; continue; }

        int ix = start_x + col * (icon_w + padding);
        int iy = start_y + row * (icon_h + padding);

        if (iy + icon_h > screen_h - WM_TASKBAR_HEIGHT - 16) {
            col++;
            row = 0;
            ix = start_x + col * (icon_w + padding);
            iy = start_y;
        }

        if (mx >= ix && mx < ix + icon_w && my >= iy && my < iy + icon_h) {
            return i;
        }
        row++;
    }
    return -1;
}

/* Right-click context menu */
/* Two separate menus:
 *   Icon menu  (right-click on app icon): Open | Delete App
 *   Desktop menu (right-click on empty space): Refresh Desktop
*/

#define CTX_ICON_ITEMS    2  /* Open, Delete App */
#define CTX_DESKTOP_ITEMS 1  /* Refresh Desktop  */

static bool ctx_open      = false;
static bool ctx_is_desktop = false; /* true = desktop menu, false = icon menu */
static int  ctx_icon      = -1;
static int  ctx_x         = 0, ctx_y = 0;
static int  ctx_hover     = -1;

/* Set to 1 from context menu "Refresh" - main loop picks it up */
static int desktop_needs_rescan = 0;

/* Warning dialog state */
static bool warn_open = false;
static int  warn_icon = -1;

static void ctx_draw(int screen_w, int screen_h) {
    (void)screen_w; (void)screen_h;
    if (!ctx_open) return;

    int num_items = ctx_is_desktop ? CTX_DESKTOP_ITEMS : CTX_ICON_ITEMS;
    int mw = 220, row_h = 26, pad = 4;
    int mh = num_items * row_h + pad * 2;
    int mx = ctx_x, my = ctx_y;

    const char *icon_items[CTX_ICON_ITEMS]       = {"Open", "Delete App (forever)"};
    const char *desktop_items_l[CTX_DESKTOP_ITEMS] = {"Refresh Desktop"};
    const char **items = ctx_is_desktop ? desktop_items_l : icon_items;

    /* Shadow */
    gpu_fill_rect(mx + 3, my + 3, mw, mh, 0xFF06060C);
    /* Background */
    gpu_fill_rect(mx, my, mw, mh, 0xFF1E1E30);
    /* Border */
    gpu_fill_rect(mx, my, mw, 1, 0xFF3A3A54);
    gpu_fill_rect(mx, my + mh - 1, mw, 1, 0xFF3A3A54);
    gpu_fill_rect(mx, my, 1, mh, 0xFF3A3A54);
    gpu_fill_rect(mx + mw - 1, my, 1, mh, 0xFF3A3A54);

    for (int i = 0; i < num_items; i++) {
        int iy = my + pad + i * row_h;
        bool hov = (i == ctx_hover);
        if (hov) gpu_fill_rect(mx + 1, iy, mw - 2, row_h, WM_COLOR_ACCENT);
        uint32_t fg = hov ? WM_COLOR_WHITE : 0xFFD0D0E0;
        uint32_t bg = hov ? WM_COLOR_ACCENT : 0xFF1E1E30;
        gpu_draw_string(mx + 10, iy + 8, (const uint8_t *)items[i], fg, bg);
        if (i < num_items - 1 && !hov)
            gpu_fill_rect(mx + 4, iy + row_h - 1, mw - 8, 1, 0xFF2A2A42);
    }
}

static void warn_draw(int screen_w, int screen_h) {
    if (!warn_open || warn_icon < 0) return;

    int ww = 340, wh = 120;
    int wx = (screen_w - ww) / 2;
    int wy = (screen_h - wh) / 2;

    /* Shadow */
    gpu_fill_rect(wx + 5, wy + 5, ww, wh, 0xFF06060C);
    /* Background */
    gpu_fill_rect(wx, wy, ww, wh, 0xFF1E1E30);
    /* Red accent line at top */
    gpu_fill_rect(wx, wy, ww, 2, 0xFFEF4444);
    /* Title area */
    gpu_fill_rect(wx, wy + 2, ww, 24, 0xFF221822);
    gpu_draw_string(wx + 10, wy + 9, (const uint8_t *)"WARNING - Delete App", 0xFFEF4444, 0xFF221822);
    /* Border */
    gpu_fill_rect(wx, wy, ww, 1, 0xFF3A3A54);
    gpu_fill_rect(wx, wy + wh - 1, ww, 1, 0xFF3A3A54);
    gpu_fill_rect(wx, wy, 1, wh, 0xFF3A3A54);
    gpu_fill_rect(wx + ww - 1, wy, 1, wh, 0xFF3A3A54);

    /* Message */
    gpu_draw_string(wx + 16, wy + 34, (const uint8_t *)"This will permanently delete the app", 0xFFD0D0E0, 0xFF1E1E30);
    gpu_draw_string(wx + 16, wy + 50, (const uint8_t *)"icon until next rebuild.", 0xFF8A8AA0, 0xFF1E1E30);

    /* Buttons */
    gpu_fill_rect(wx + 20, wy + 78, 90, 26, 0xFF3A3A54);
    gpu_draw_string(wx + 36, wy + 85, (const uint8_t *)"Cancel", WM_COLOR_WHITE, 0xFF3A3A54);
    gpu_fill_rect(wx + 230, wy + 78, 90, 26, 0xFFEF4444);
    gpu_draw_string(wx + 246, wy + 85, (const uint8_t *)"Delete", WM_COLOR_WHITE, 0xFFEF4444);
}

/* Returns true if input was consumed by context menu / warning dialog */
static bool ctx_handle(int mx, int my, bool click, bool right_click,
                        int screen_w, int screen_h) {
    /* Warning dialog takes priority over everything */
    if (warn_open) {
        int ww = 340, wh = 120;
        int wx = (screen_w - ww) / 2;
        int wy = (screen_h - wh) / 2;
        if (click) {
            /* Cancel */
            if (mx >= wx + 20 && mx < wx + 110 && my >= wy + 78 && my < wy + 104) {
                warn_open = false;
                warn_icon = -1;
            }
            /* Delete */
            if (mx >= wx + 230 && mx < wx + 320 && my >= wy + 78 && my < wy + 104) {
                if (warn_icon >= 0 && warn_icon < NUM_APPS) {
                    app_hidden[warn_icon] = true;
                    save_app_hidden(warn_icon);
                    const char *app_name = desktop_apps[warn_icon].name;
                    char shortcut_path[80];
                    const char *prefix = "C:\\Desktop\\Applications\\";
                    int pi = 0;
                    while (prefix[pi] && pi < 79) { shortcut_path[pi] = prefix[pi]; pi++; }
                    int ni = 0;
                    while (app_name[ni] && pi < 79) { shortcut_path[pi++] = app_name[ni++]; }
                    shortcut_path[pi] = 0;
                    for (int fi = 0; fi < fs_count; fi++) {
                        if (desktop_str_cmp(fs_table[fi].name, shortcut_path) == 0) {
                            for (int fj = fi; fj < fs_count - 1; fj++)
                                fs_table[fj] = fs_table[fj + 1];
                            fs_count--;
                            break;
                        }
                    }
                }
                warn_open = false;
                warn_icon = -1;
            }
        }
        return true;
    }

    /* Right-click: decide which menu to open.
     * If the cursor is over an open window, let the window manager handle it
     * and do NOT open the desktop context menu. */
    if (right_click && !ctx_open) {
        /* Check if click lands on any visible window - if so, bail out */
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
        if (over_window) return false; /* let wm_handle_input deal with it */

        int icon = check_icon_click(mx, my, screen_h);
        if (icon >= 0 && !app_hidden[icon]) {
            /* Clicked on an app icon - show icon menu */
            ctx_open       = true;
            ctx_is_desktop = false;
            ctx_icon       = icon;
        } else {
            /* Clicked on empty desktop - show desktop menu */
            ctx_open       = true;
            ctx_is_desktop = true;
            ctx_icon       = -1;
        }
        ctx_x     = mx;
        ctx_y     = my;
        ctx_hover = -1;
        return true;
    }

    if (!ctx_open) return false;

    int num_items = ctx_is_desktop ? CTX_DESKTOP_ITEMS : CTX_ICON_ITEMS;
    int mw = 220, row_h = 26, pad = 4;
    int mh = num_items * row_h + pad * 2;

    /* Close menu if user clicks outside it */
    if (click && (mx < ctx_x || mx >= ctx_x + mw ||
                  my < ctx_y || my >= ctx_y + mh)) {
        ctx_open = false;
        return false;
    }

    /* Update hover row */
    ctx_hover = -1;
    if (mx >= ctx_x && mx < ctx_x + mw &&
        my >= ctx_y + pad && my < ctx_y + pad + num_items * row_h) {
        ctx_hover = (my - ctx_y - pad) / row_h;
    }

    if (click) {
        if (ctx_is_desktop) {
            /* Desktop menu */
            if (ctx_hover == 0) {
                desktop_needs_rescan = 1;
            }
        } else {
            /* Icon menu */
            if (ctx_hover == 0) {
                /* Open */
                if (ctx_icon >= 0 && ctx_icon < NUM_APPS)
                    desktop_apps[ctx_icon].launcher();
            } else if (ctx_hover == 1) {
                /* Delete - show warning first */
                warn_open = true;
                warn_icon = ctx_icon;
            }
        }
        ctx_open = false;
        return true;
    }

    /* Consume mouse events while menu is open */
    return true;
}

/* Main desktop entry point */
int desktop_main(void) {
    extern void c_puts(const char *s);
    extern bool gpu_is_vesa(void);
    extern void cmd_init(void);

    /* CRITICAL: Initialize filesystem before loading settings! */
    cmd_init();

    c_puts("[DESKTOP] Setting up framebuffer...\n");
    uint32_t *fb = gpu_setup_framebuffer();

    if (!fb) {
        c_puts("[DESKTOP] ERROR: Failed to initialize graphics!\n");
        c_puts("[DESKTOP] Falling back to text mode...\n");
        return 0;
    }

    c_puts("[DESKTOP] Clearing screen...\n");
    gpu_clear(0x00000000);

    int sw = gpu_get_width();
    int sh = gpu_get_height();
    c_puts("[DESKTOP] Screen dimensions obtained: ");

    if (sw > 0) {
        char buf[16];
        int i = 0;
        int w = sw;
        if (w >= 1000) buf[i++] = '0' + (w / 1000);
        if (w >= 100) buf[i++] = '0' + ((w / 100) % 10);
        if (w >= 10) buf[i++] = '0' + ((w / 10) % 10);
        buf[i++] = '0' + (w % 10);
        buf[i++] = 'x';
        int h = sh;
        if (h >= 1000) buf[i++] = '0' + (h / 1000);
        if (h >= 100) buf[i++] = '0' + ((h / 100) % 10);
        if (h >= 10) buf[i++] = '0' + ((h / 10) % 10);
        buf[i++] = '0' + (h % 10);
        buf[i++] = '\n';
        buf[i] = 0;
        c_puts(buf);
    }

    /* DO NOT override resolution here! Respect what gpu_setup_framebuffer returned.
       Only use defaults if dimensions are completely invalid (<= 0). */
    if (sw <= 0) sw = 1920;
    if (sh <= 0) sh = 1080;

    c_puts("[DESKTOP] Initializing window manager...\n");
    wm_init(sw, sh);
    wm_load_settings();

    c_puts("[DESKTOP] Initializing mouse...\n");
    mouse_init();
    mouse_set_bounds(sw, sh);
    c_puts("[DESKTOP] Mouse initialized and bounds set\n");

    /* Sanity check filesystem state before scanning */
    if (fs_count < 0 || fs_count > 128) {
        fs_count = 0;
    }

    /* Decode embedded PNG icons into pixel buffers - must be done after
     * GPU is initialized (gpu_setup_framebuffer called above) */
    icons_load_all();

    /* Load which apps were deleted (persisted via .hidden_ markers on disk) */
    load_app_hidden_state();

    /* Scan for desktop items */
    scan_desktop_items();

    /* Register apps into the dock */
    setup_dock();

    c_puts("[DESKTOP] Launching startup app...\n");
    if (g_settings.startup_app_idx >= 0 && g_settings.startup_app_idx < NUM_APPS) {
        desktop_apps[g_settings.startup_app_idx].launcher();
    } else if (g_settings.startup_app_idx == 0) {
        /* Default to terminal if not set otherwise, but if explicitly -1 (None), skip */
        terminal_create();
    }
    
    c_puts("[DESKTOP] Desktop ready! Entering main loop...\n");
    
    /* Debug: Check timer */
    c_puts("[DESKTOP] Timer tick check: ");
    uint32_t t = get_ticks();
    char tbuf[16];
    int ti = 0;
    if (t == 0) tbuf[ti++] = '0';
    else {
        int temp = t;
        int d = 1;
        while (temp/d >= 10) d *= 10;
        while (d > 0) {
            tbuf[ti++] = '0' + (temp/d)%10;
            d /= 10;
        }
    }
    tbuf[ti++] = '\n';
    tbuf[ti] = 0;
    c_puts(tbuf);

    bool prev_left = false;
    bool prev_right = false;
    uint32_t last_scan_tick = 0;
    /* Force first frame to render by initializing last_frame_tick to different value */
    uint32_t last_frame_tick = get_ticks() - 1; 

    /* Map dock slot index -> actual app index (skipping hidden apps) */
    int dock_to_app[NUM_APPS];
    {
        int d = 0;
        for (int i = 0; i < NUM_APPS; i++) {
            if (!app_hidden[i]) dock_to_app[d++] = i;
        }
    }

    while (desktop_exit_reason < 0) {
        /* Frame limiter - one frame per tick @ 100Hz = 100 FPS max */
        uint32_t now = get_ticks();
        if (now == last_frame_tick) {
            for (volatile int _i = 0; _i < 2000; _i++);
            continue;
        }
        last_frame_tick = now;

        mouse_poll();
        int mx = mouse_get_x();
        int my = mouse_get_y();
        bool left = mouse_get_left();
        bool right = mouse_get_right();
        bool click = left && !prev_left;
        bool right_click = right && !prev_right;

        /* Periodically rescan desktop items */
        if (now - last_scan_tick > 1000) { /* Every ~10 seconds @ 100Hz */
            scan_desktop_items();
            last_scan_tick = now;
        }

        /* Manual refresh requested from right-click context menu */
        if (desktop_needs_rescan) {
            scan_desktop_items();
            desktop_needs_rescan = 0;
        }

        /* Context menu / warning dialog handle first - consumes input */
        if (ctx_handle(mx, my, click, right_click, sw, sh)) {
            /* After a delete, rebuild dock */
            setup_dock();
            int d = 0;
            for (int i = 0; i < NUM_APPS; i++)
                if (!app_hidden[i]) dock_to_app[d++] = i;
            prev_left = left;
            prev_right = right;
            goto draw_frame;
        }

        /* Dock hover + click handling */
        if (wm_dock_handle(mx, my, click)) {
            /* Check if a slot was clicked */
            if (dock_clicked_slot >= 0) {
                int slot = dock_clicked_slot;
                dock_clicked_slot = -1;
                if (slot < NUM_APPS) {
                    int app_idx = dock_to_app[slot];
                    /* If app window already open, focus it; otherwise launch */
                    bool found = false;
                    for (int i = 0; i < wm_get_window_count(); i++) {
                        Window *w = wm_get_window(i);
                        if (!w || !w->visible) continue;
                        /* Match by app name prefix */
                        const char *wt = w->title;
                        const char *an = desktop_apps[app_idx].name;
                        bool match = true;
                        for (int k = 0; an[k]; k++) {
                            if (wt[k] != an[k]) { match = false; break; }
                        }
                        if (match) {
                            if (w->state == WM_STATE_MINIMIZED)
                                w->state = WM_STATE_NORMAL;
                            wm_focus_window(w);
                            found = true;
                            break;
                        }
                    }
                    if (!found) desktop_apps[app_idx].launcher();
                }
            }
            prev_left = left;
            prev_right = right;
            goto draw_frame;
        }
        dock_clicked_slot = -1;

        /* Left click on desktop icons */
        if (click) {
            int icon = check_icon_click(mx, my, sh);
            if (icon >= 0 && icon < NUM_APPS && !app_hidden[icon]) {
                desktop_apps[icon].launcher();
                prev_left = left;
                prev_right = right;
                goto draw_frame;
            }
        }

        wm_handle_input();

        if (c_kb_hit() && !wm_get_focused()) {
            uint16_t key = c_getkey();
            (void)key;
        }

        prev_left = left;
        prev_right = right;

draw_frame:
        wm_draw_desktop_bg();
        draw_desktop_icons(sw, sh);
        wm_draw_all();
        wm_draw_taskbar(); /* now draws the dock */
        ctx_draw(sw, sh);
        warn_draw(sw, sh);
        wm_draw_cursor(mx, my);
        gpu_flush();
    }

    int reason = desktop_exit_reason;
    desktop_exit_reason = -1;

    if (reason == 0) {
        extern void set_text_mode(void);
        extern void set_cursor_pos(int row, int col);
        extern void c_cls(void);
        set_text_mode();
        /* Clear screen and reset cursor after switching to text mode.
           Without this, old VGA buffer content shows as "random lines". */
        c_cls();
        set_cursor_pos(0, 0);
    }

    return reason;
}
