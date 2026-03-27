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
    {"Terminal",    terminal_create,        icon_draw_png_terminal},
    {"Blaze",       app_blaze_create,       icon_draw_png_browser},
    {"Notepad",     app_notepad_create,     icon_draw_png_notepad},
    {"Paint",       app_paint_create,       icon_draw_png_paint},
    {"Calculator",  app_calc_create,        icon_draw_png_calculator},
    {"Files",       app_filebrowser_create, icon_draw_png_files},
    {"Clock",       app_clock_create,       icon_draw_png_clock},
    {"System Info", app_sysinfo_create,     icon_draw_png_sysinfo},
    {"Settings",    app_settings_create,    icon_draw_png_settings},
    {"Snake",       app_snake_create,       icon_draw_png_snake},
};
#define NUM_APPS 10

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

static void save_app_hidden(int app_idx) {
    if (app_idx < 0 || app_idx >= NUM_APPS) return;

    char marker[80];
    int pi = 0;
    while (hidden_prefix[pi] && pi < 79) { marker[pi] = hidden_prefix[pi]; pi++; }
    const char *app_name = desktop_apps[app_idx].name;
    int ni = 0;
    while (app_name[ni] && pi < 79) { marker[pi++] = app_name[ni++]; }
    marker[pi] = 0;

    for (int i = 0; i < fs_count; i++) {
        if (desktop_str_cmp(fs_table[i].name, marker) == 0) return;
    }

    if (fs_count < 128) {
        int j = 0;
        while (marker[j] && j < 55) { fs_table[fs_count].name[j] = marker[j]; j++; }
        fs_table[fs_count].name[j] = 0;
        fs_table[fs_count].size = 0;
        fs_table[fs_count].type = 0;
        fs_table[fs_count].attr = 0x02;
        fs_count++;
        extern int fs_save_to_disk(void);
        fs_save_to_disk();
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

/* Desktop icon click detection */
static int check_icon_click(int mx, int my, int screen_h) {
    int icon_w = 72;
    int icon_h = 80;
    int padding = 12;
    int start_x = 16;
    int start_y = 16;
    int col = 0, row = 0;

    for (int i = 0; i < NUM_APPS; i++) {
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
#define CTX_ICON_ITEMS    2
#define CTX_DESKTOP_ITEMS 1

static bool ctx_open       = false;
static bool ctx_is_desktop = false;
static int  ctx_icon       = -1;
static int  ctx_x          = 0, ctx_y = 0;
static int  ctx_hover      = -1;
static int  desktop_needs_rescan = 0;

/* Warning dialog */
static bool warn_open = false;
static int  warn_icon = -1;

static void ctx_draw(int screen_w, int screen_h) {
    (void)screen_w; (void)screen_h;
    if (!ctx_open) return;

    int num_items = ctx_is_desktop ? CTX_DESKTOP_ITEMS : CTX_ICON_ITEMS;
    int mw = 220, row_h = 26, pad = 4;
    int mh = num_items * row_h + pad * 2;
    int mx = ctx_x, my = ctx_y;

    const char *icon_items[CTX_ICON_ITEMS]          = {"Open", "Delete App (forever)"};
    const char *desktop_items_l[CTX_DESKTOP_ITEMS]  = {"Refresh Desktop"};
    const char **items = ctx_is_desktop ? desktop_items_l : icon_items;

    gpu_fill_rect(mx + 3, my + 3, mw, mh, 0xFF06060C);
    gpu_fill_rect(mx, my, mw, mh, 0xFF1E1E30);
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

    gpu_fill_rect(wx + 5, wy + 5, ww, wh, 0xFF06060C);
    gpu_fill_rect(wx, wy, ww, wh, 0xFF1E1E30);
    gpu_fill_rect(wx, wy, ww, 2, 0xFFEF4444);
    gpu_fill_rect(wx, wy + 2, ww, 24, 0xFF221822);
    gpu_draw_string(wx + 10, wy + 9,
                    (const uint8_t *)"WARNING - Delete App", 0xFFEF4444, 0xFF221822);
    gpu_fill_rect(wx, wy, ww, 1, 0xFF3A3A54);
    gpu_fill_rect(wx, wy + wh - 1, ww, 1, 0xFF3A3A54);
    gpu_fill_rect(wx, wy, 1, wh, 0xFF3A3A54);
    gpu_fill_rect(wx + ww - 1, wy, 1, wh, 0xFF3A3A54);

    gpu_draw_string(wx + 16, wy + 34,
                    (const uint8_t *)"This will permanently delete the app",
                    0xFFD0D0E0, 0xFF1E1E30);
    gpu_draw_string(wx + 16, wy + 50,
                    (const uint8_t *)"icon until next rebuild.",
                    0xFF8A8AA0, 0xFF1E1E30);

    gpu_fill_rect(wx + 20, wy + 78, 90, 26, 0xFF3A3A54);
    gpu_draw_string(wx + 36, wy + 85,
                    (const uint8_t *)"Cancel", WM_COLOR_WHITE, 0xFF3A3A54);
    gpu_fill_rect(wx + 230, wy + 78, 90, 26, 0xFFEF4444);
    gpu_draw_string(wx + 246, wy + 85,
                    (const uint8_t *)"Delete", WM_COLOR_WHITE, 0xFFEF4444);
}

static bool ctx_handle(int mx, int my, bool click, bool right_click,
                       int screen_w, int screen_h) {
    /* Warning dialog takes priority */
    if (warn_open) {
        int ww = 340, wh = 120;
        int wx = (screen_w - ww) / 2;
        int wy = (screen_h - wh) / 2;
        if (click) {
            if (mx >= wx + 20 && mx < wx + 110 && my >= wy + 78 && my < wy + 104) {
                warn_open = false;
                warn_icon = -1;
            }
            if (mx >= wx + 230 && mx < wx + 320 && my >= wy + 78 && my < wy + 104) {
                if (warn_icon >= 0 && warn_icon < NUM_APPS) {
                    app_hidden[warn_icon] = true;
                    save_app_hidden(warn_icon);

                    /* Remove shortcut file if it exists */
                    const char *app_name = desktop_apps[warn_icon].name;
                    char shortcut_path[80];
                    const char *prefix = "/Desktop/Applications/";
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

    /* Right-click opens context menu */
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

        int icon = check_icon_click(mx, my, screen_h);
        if (icon >= 0 && !app_hidden[icon]) {
            ctx_open       = true;
            ctx_is_desktop = false;
            ctx_icon       = icon;
        } else {
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

    if (click && (mx < ctx_x || mx >= ctx_x + mw ||
                  my < ctx_y || my >= ctx_y + mh)) {
        ctx_open = false;
        return false;
    }

    ctx_hover = -1;
    if (mx >= ctx_x && mx < ctx_x + mw &&
        my >= ctx_y + pad && my < ctx_y + pad + num_items * row_h) {
        ctx_hover = (my - ctx_y - pad) / row_h;
    }

    if (click) {
        if (ctx_is_desktop) {
            if (ctx_hover == 0) desktop_needs_rescan = 1;
        } else {
            if (ctx_hover == 0) {
                if (ctx_icon >= 0 && ctx_icon < NUM_APPS)
                    desktop_apps[ctx_icon].launcher();
            } else if (ctx_hover == 1) {
                warn_open = true;
                warn_icon = ctx_icon;
            }
        }
        ctx_open = false;
        return true;
    }

    return true;
}

/* Check installation status */
static bool check_installed(void) {
    extern int load_file_content(const char *filename, char *buffer, int max_len);
    extern void c_puts(const char *s);

    char marker[20];
    marker[0] = 0;

    /* 1. Check RAM filesystem (loaded by cmd_init -> fs_load_from_disk) */
    int found = load_file_content("/installed.sys", marker, 16);
    if (found > 0) {
        /* c_puts("[DESKTOP] Found /installed.sys in filesystem\n"); */
        return true;
    }

    /* c_puts("[DESKTOP] /installed.sys not in RAM filesystem, checking raw disk...\n"); */

    /* 2. Fallback: scan raw HDD sectors at LBA 1000+ for the FSEntry
          containing "/installed.sys". The FSEntry struct stores the filename
          in the first 56 bytes of the name field. */
    extern int disk_read_lba(uint32_t lba, uint32_t count, void *buffer);
    char sector[512];

    for (int i = 0; i < 128; i++) {
        if (disk_read_lba(1000 + i, 1, sector) != 0) {
            /* Read failed — stop scanning this direction */
            break;
        }

        /* Empty entry means end of table */
        if (sector[0] == 0) break;

        /* Check if filename matches "/installed.sys" */
        const char *expected = "/installed.sys";
        bool match = true;
        for (int j = 0; expected[j]; j++) {
            if (sector[j] != expected[j]) {
                match = false;
                break;
            }
        }
        /* c_puts("[DESKTOP] Found /installed.sys on HDD at entry "); */
        char num[4];
        num[0] = '0' + (i / 100) % 10;
        num[1] = '0' + (i / 10) % 10;
        num[2] = '0' + i % 10;
        num[3] = 0;
        /* c_puts(num);
        c_puts("\n"); */
        return true;
    }
}

/* c_puts("[DESKTOP] No installation marker found anywhere\n"); */
    return false;
}

/* Main desktop entry point */
int desktop_main(void) {
    extern void c_puts(const char *s);
    extern bool gpu_is_vesa(void);
    extern void cmd_init(void);

    /* c_puts("[DESKTOP] Initializing ATA driver...\n"); */
    extern int ata_init(void);
    int ata_result = ata_init();
    if (ata_result == 0) {
        /* c_puts("[DESKTOP] ATA driver initialized successfully\n"); */
    } else {
        /* c_puts("[DESKTOP] WARNING: ATA driver init failed - no disk persistence!\n"); */
    }

    /* Initialize filesystem (loads from disk if available) */
    /* c_puts("[DESKTOP] Initializing filesystem...\n"); */
    cmd_init();
    wm_load_settings();

    /* Detect VMware SVGA */
    extern bool vmware_svga_detect(void);
    extern bool vmware_svga_available(void);
    extern bool vmware_svga_set_mode(uint32_t width, uint32_t height, uint32_t bpp);

    vmware_svga_detect();

    /* Apply resolution from settings */
    extern void vesa_reinit_mode(void);
    int res_idx = g_settings.resolution;

    uint16_t widths[]  = {800, 1024, 1280, 1280, 1440, 1600, 1920, 2560};
    uint16_t heights[] = {600,  768,  720, 1024,  900,  900, 1080, 1440};

    if (res_idx >= 0 && res_idx < 8) {
        if (vmware_svga_available()) {
            vmware_svga_set_mode(widths[res_idx], heights[res_idx], 32);
        } else {
            *(uint16_t *)(0x9004) = widths[res_idx];
            *(uint16_t *)(0x9006) = heights[res_idx];
            vesa_reinit_mode();
        }
        extern void io_wait(void);
        for (int i = 0; i < 100; i++) io_wait();
    }

    /* c_puts("[DESKTOP] Setting up framebuffer...\n"); */

    uint32_t *fb = gpu_setup_framebuffer();
    if (!fb) {
        /* c_puts("[DESKTOP] ERROR: Failed to initialize graphics!\n"); */
        /* c_puts("[DESKTOP] Falling back to text mode...\n"); */
        return 0;
    }

    gpu_clear(0x00000000);

    int sw = gpu_get_width();
    int sh = gpu_get_height();

    /* Log actual resolution */
    /* c_puts("[DESKTOP] Resolution: "); */
    {
        char buf[16];
        int bi = 0;
        int w = sw, h = sh;
        if (w >= 1000) buf[bi++] = '0' + (w / 1000);
        if (w >= 100) buf[bi++] = '0' + ((w / 100) % 10);
        if (w >= 10) buf[bi++] = '0' + ((w / 10) % 10);
        buf[bi++] = '0' + (w % 10);
        buf[bi++] = 'x';
        if (h >= 1000) buf[bi++] = '0' + (h / 1000);
        if (h >= 100) buf[bi++] = '0' + ((h / 100) % 10);
        if (h >= 10) buf[bi++] = '0' + ((h / 10) % 10);
        buf[bi++] = '0' + (h % 10);
        buf[bi++] = '\n';
        buf[bi] = 0;
        /* c_puts(buf); */
    }

    if (sw <= 0) sw = 1920;
    if (sh <= 0) sh = 1080;

    /* c_puts("[DESKTOP] Initializing window manager...\n"); */
    wm_init(sw, sh);
    wm_load_settings();

    /* c_puts("[DESKTOP] Initializing mouse...\n"); */
    mouse_init();
    mouse_set_bounds(sw, sh);

    if (fs_count < 0 || fs_count > 128) {
        fs_count = 0;
    }

    /* Load PNG icons */
    icons_load_all();

    /* Load hidden app state and scan desktop */
    load_app_hidden_state();
    scan_desktop_items();
    setup_dock();

    /* Installation check */
#ifndef DEBUG_SKIP_INSTALL
    if (!check_installed()) {
        /* c_puts("[DESKTOP] No installation found. Launching OS Setup...\n"); */

        /* Force 1920x1080 for installer */
        int current_w = gpu_get_width();
        int current_h = gpu_get_height();

        if (current_w != 1920 || current_h != 1080) {
            /* c_puts("[DESKTOP] Setting installer resolution to 1920x1080...\n"); */
            if (vmware_svga_available()) {
                vmware_svga_set_mode(1920, 1080, 32);
            } else {
                *(uint16_t *)(0x9004) = 1920;
                *(uint16_t *)(0x9006) = 1080;
                vesa_reinit_mode();
            }
            gpu_setup_framebuffer();
            sw = gpu_get_width();
            sh = gpu_get_height();
            wm_set_screen_size(sw, sh);
            mouse_set_bounds(sw, sh);
        }

        wm_set_installer_mode(true);
        app_installer_create();
    } else {
#endif
        /* c_puts("[DESKTOP] Installation found. Starting desktop.\n"); */

        /* Launch startup app */
        if (g_settings.startup_app_idx >= 0 && g_settings.startup_app_idx < NUM_APPS) {
            desktop_apps[g_settings.startup_app_idx].launcher();
        } else if (g_settings.startup_app_idx == 0) {
            terminal_create();
        }
#ifndef DEBUG_SKIP_INSTALL
    }
#endif

    /* c_puts("[DESKTOP] Entering main loop...\n"); */

    bool prev_left = false;
    bool prev_right = false;
    uint32_t last_scan_tick = 0;
    uint32_t last_frame_tick = get_ticks() - 1;

    /* Map dock slots to app indices (skip hidden) */
    int dock_to_app[NUM_APPS];
    {
        int d = 0;
        for (int i = 0; i < NUM_APPS; i++) {
            if (!app_hidden[i]) dock_to_app[d++] = i;
        }
    }

    while (desktop_exit_reason < 0) {
        /* Resolution change check */
        extern bool wm_resolution_changed(void);
        if (wm_resolution_changed()) {
            sw = gpu_get_width();
            sh = gpu_get_height();
            mouse_set_bounds(sw, sh);
        }

        /* Frame limiter */
        uint32_t now = get_ticks();
        if (now == last_frame_tick) {
            __asm__ volatile("sti\nhlt");
            continue;
        }
        last_frame_tick = now;

        /* Drain mouse packets */
        for (int _mp = 0; _mp < 10; _mp++) mouse_poll();
        int mx = mouse_get_x();
        int my = mouse_get_y();
        bool left = mouse_get_left();
        bool right = mouse_get_right();
        bool click = left && !prev_left;
        bool right_click = right && !prev_right;

        /* Periodic desktop rescan */
        if (now - last_scan_tick > 1000) {
            scan_desktop_items();
            last_scan_tick = now;
        }

        if (desktop_needs_rescan) {
            scan_desktop_items();
            desktop_needs_rescan = 0;
        }

        /* Context menu handling */
        if (ctx_handle(mx, my, click, right_click, sw, sh)) {
            setup_dock();
            int d = 0;
            for (int i = 0; i < NUM_APPS; i++)
                if (!app_hidden[i]) dock_to_app[d++] = i;
            prev_left = left;
            prev_right = right;
            goto draw_frame;
        }

        /* Skip desktop interaction during installer */
        extern bool wm_is_installer_mode(void);
        if (!wm_is_installer_mode()) {
            /* Dock handling */
            if (wm_dock_handle(mx, my, click)) {
                if (dock_clicked_slot >= 0) {
                    int slot = dock_clicked_slot;
                    dock_clicked_slot = -1;
                    if (slot < NUM_APPS) {
                        int app_idx = dock_to_app[slot];
                        bool found = false;
                        for (int i = 0; i < wm_get_window_count(); i++) {
                            Window *w = wm_get_window(i);
                            if (!w || !w->visible) continue;
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

            /* Desktop icon clicks */
            if (click) {
                int icon = check_icon_click(mx, my, sh);
                if (icon >= 0 && icon < NUM_APPS && !app_hidden[icon]) {
                    desktop_apps[icon].launcher();
                    prev_left = left;
                    prev_right = right;
                    goto draw_frame;
                }
            }
        } else {
            /* Installer mode - still need to rebuild dock mapping when
               installer closes (it calls setup_dock externally) */
            int d = 0;
            for (int i = 0; i < NUM_APPS; i++)
                if (!app_hidden[i]) dock_to_app[d++] = i;
        }

        wm_handle_input();

        if (sys_kb_hit() && !wm_get_focused()) {
            uint16_t key = sys_getkey();
            (void)key;
        }

        prev_left = left;
        prev_right = right;

draw_frame:
        wm_draw_desktop_bg();
        draw_desktop_icons(sw, sh);
        wm_draw_all();
        wm_draw_taskbar();
        ctx_draw(sw, sh);
        warn_draw(sw, sh);
        wm_draw_cursor(mx, my);
        gpu_flush();
    }

    int reason = desktop_exit_reason;
    desktop_exit_reason = -1;

    if (reason == 0) {
        extern void set_cursor_pos(int row, int col);
        extern void c_cls(void);
        extern void io_wait(void);

        set_text_mode();
        
        /* Long delay for hardware to settle */
        for (int i = 0; i < 10000; i++) io_wait();
        
        /* Clear multiple times */
        c_cls();
        for (int i = 0; i < 5000; i++) io_wait();
        c_cls();
        for (int i = 0; i < 5000; i++) io_wait();
        c_cls();
        
        set_cursor_pos(0, 0);
    }

    return reason;
}