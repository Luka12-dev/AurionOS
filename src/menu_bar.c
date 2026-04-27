/* ═══════════════════════════════════════════════════════════════════════
 *  Aurion OS — Menu Bar
 *
 *  Real macOS-style menu bar:
 *    • Glassmorphic frosted-glass background
 *    • Accent dot + bold app name + static menu titles
 *    • Click to open dropdown, click outside to close
 *    • Hover highlight tracks mouse in real time
 *    • Rounded dropdown panel with glassmorphism
 *    • Real clock from sys_get_time()
 *    • Separator lines between menu groups
 *    • Grayed-out disabled items
 *    • Action callbacks fired on click
 * ═══════════════════════════════════════════════════════════════════════ */

#include "menu_bar.h"
#include "window_manager.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  External GPU / system functions
 * ═══════════════════════════════════════════════════════════════════════ */
extern void     gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void     gpu_fill_rect_blend(int x, int y, int w, int h,
                                    uint32_t color, uint8_t alpha);
extern void     gpu_blur_rect(int x, int y, int w, int h);
extern void     gpu_draw_pixel(int x, int y, uint32_t color);
extern void     gpu_draw_char(int x, int y, uint8_t c,
                               uint32_t fg, uint32_t bg);
extern void     gpu_draw_string(int x, int y, const uint8_t *str,
                                 uint32_t fg, uint32_t bg);
extern int      wm_get_screen_w(void);
extern int      wm_get_screen_h(void);

/*  Real time — returns 0 on success, fills BCD or binary hours/mins/secs */
extern int      sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);

/* ═══════════════════════════════════════════════════════════════════════
 *  Constants
 * ═══════════════════════════════════════════════════════════════════════ */
#define MAX_MENUS           10
#define ITEM_H              24      /* px per menu item row          */
#define ITEM_INDENT         14      /* left text margin inside panel */
#define ITEM_PAD_V           8      /* vertical padding around panel */
#define DROP_CORNER          8      /* dropdown corner radius        */
#define DROP_MIN_W         160      /* minimum dropdown width        */
#define SEP_H               10      /* height of a separator row     */
#define BAR_TITLE_PAD       10      /* horizontal pad around title   */
#define BAR_LEFT_MARGIN     32      /* x of first menu title         */
#define BAR_TITLE_GAP        4      /* gap between menu titles       */

/* ═══════════════════════════════════════════════════════════════════════
 *  State
 * ═══════════════════════════════════════════════════════════════════════ */
static Menu  g_menus[MAX_MENUS];
static int   g_menu_count    = 0;
static int   g_open_idx      = -1;   /* index of open menu, -1=none  */
static int   g_hover_bar     = -1;   /* hovered title on bar         */
static int   g_hover_item    = -1;   /* hovered item inside dropdown */

/* ═══════════════════════════════════════════════════════════════════════
 *  SMALL HELPERS
 * ═══════════════════════════════════════════════════════════════════════ */
static int mb_slen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int mb_min(int a, int b) { return a < b ? a : b; }

static void mb_get_clean_title(const char *src, char *dst, int max_len) {
    int i = 0;
    if (!src || max_len <= 0) return;

    while (src[i] && i < max_len - 1) {
        char c = src[i];
        if (c == '\n' || c == '\r') break;
        if ((unsigned char)c < 32 || (unsigned char)c > 126) break;
        dst[i] = c;
        i++;
    }
    dst[i] = '\0';

    /* Strip noisy virtualization suffixes like " on vmware K". */
    for (int j = 0; dst[j]; j++) {
        char c0 = dst[j];
        char c1 = dst[j + 1];
        char c2 = dst[j + 2];
        char c3 = dst[j + 3];
        char c4 = dst[j + 4];
        char c5 = dst[j + 5];
        char c6 = dst[j + 6];
        char c7 = dst[j + 7];
        char c8 = dst[j + 8];
        char c9 = dst[j + 9];
        if (c0 == ' ' &&
            (c1 == 'o' || c1 == 'O') &&
            (c2 == 'n' || c2 == 'N') &&
            c3 == ' ' &&
            (c4 == 'v' || c4 == 'V') &&
            (c5 == 'm' || c5 == 'M') &&
            (c6 == 'w' || c6 == 'W') &&
            (c7 == 'a' || c7 == 'A') &&
            (c8 == 'r' || c8 == 'R') &&
            (c9 == 'e' || c9 == 'E')) {
            dst[j] = '\0';
            break;
        }
    }

    /* Also strip generic virtualization suffixes: " on qemu", " on kvm", etc. */
    for (int j = 0; dst[j]; j++) {
        char c0 = dst[j];
        char c1 = dst[j + 1];
        char c2 = dst[j + 2];
        if (c0 == ' ' &&
            (c1 == 'o' || c1 == 'O') &&
            (c2 == 'n' || c2 == 'N')) {
            char *tail = &dst[j + 3];
            for (int k = 0; tail[k]; k++) {
                char t = tail[k];
                if (t >= 'A' && t <= 'Z') t = (char)(t + 32);
                if ((t == 'q' && tail[k + 1] && ((tail[k + 1] | 32) == 'e')) ||
                    (t == 'k' && tail[k + 1] && ((tail[k + 1] | 32) == 'v')) ||
                    (t == 'v' && tail[k + 1] && ((tail[k + 1] | 32) == 'i'))) {
                    dst[j] = '\0';
                    break;
                }
            }
        }
        if (!dst[j]) break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  DRAW PRIMITIVES  (local, so we don't depend on wm internals)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Corner inset for scanline rounded rect */
static int mb_inset(int r, int row) {
    int dy = r - row;
    int dx = 0, r2 = r * r;
    while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
    return r - dx;
}

/* Filled rounded rect, opaque */
static void mb_rrect(int x, int y, int w, int h, uint32_t c, int r) {
    if (w <= 0 || h <= 0) return;
    if (r < 1) { gpu_fill_rect(x, y, w, h, c); return; }
    r = mb_min(r, mb_min(w / 2, h / 2));
    for (int i = 0; i < r; i++) {
        int ins = mb_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0) {
            gpu_fill_rect(x + ins, y + i,         sw, 1, c);
            gpu_fill_rect(x + ins, y + h - 1 - i, sw, 1, c);
        }
    }
    if (h - 2 * r > 0)
        gpu_fill_rect(x, y + r, w, h - 2 * r, c);
}

/* Filled rounded rect, alpha blended */
static void mb_rrect_a(int x, int y, int w, int h,
                        uint32_t c, int r, uint8_t a) {
    if (w <= 0 || h <= 0 || a == 0) return;
    if (r < 1) { gpu_fill_rect_blend(x, y, w, h, c, a); return; }
    r = mb_min(r, mb_min(w / 2, h / 2));
    for (int i = 0; i < r; i++) {
        int ins = mb_inset(r, i);
        int sw  = w - 2 * ins;
        if (sw > 0) {
            gpu_fill_rect_blend(x + ins, y + i,         sw, 1, c, a);
            gpu_fill_rect_blend(x + ins, y + h - 1 - i, sw, 1, c, a);
        }
    }
    if (h - 2 * r > 0)
        gpu_fill_rect_blend(x, y + r, w, h - 2 * r, c, a);
}

/* Rounded rect outline only */
static void mb_rrect_outline(int x, int y, int w, int h,
                              uint32_t c, int r, uint8_t a) {
    if (w <= 0 || h <= 0) return;
    if (r < 1) {
        gpu_fill_rect_blend(x, y, w, 1, c, a);
        gpu_fill_rect_blend(x, y+h-1, w, 1, c, a);
        gpu_fill_rect_blend(x, y, 1, h, c, a);
        gpu_fill_rect_blend(x+w-1, y, 1, h, c, a);
        return;
    }
    r = mb_min(r, mb_min(w/2, h/2));
    /* straight edges */
    gpu_fill_rect_blend(x+r, y,     w-2*r, 1, c, a);
    gpu_fill_rect_blend(x+r, y+h-1, w-2*r, 1, c, a);
    gpu_fill_rect_blend(x,   y+r, 1, h-2*r, c, a);
    gpu_fill_rect_blend(x+w-1, y+r, 1, h-2*r, c, a);
    /* corner pixels */
    for (int i = 0; i < r; i++) {
        int ins = mb_inset(r, i);
        /* top-left & top-right outermost pixel of this row */
        gpu_fill_rect_blend(x + ins, y + i, 1, 1, c, a);
        gpu_fill_rect_blend(x + w - 1 - ins, y + i, 1, 1, c, a);
        /* bottom-left & bottom-right */
        gpu_fill_rect_blend(x + ins, y + h - 1 - i, 1, 1, c, a);
        gpu_fill_rect_blend(x + w - 1 - ins, y + h - 1 - i, 1, 1, c, a);
    }
}

/* Filled circle */
static void mb_circ(int cx, int cy, int r, uint32_t c) {
    int r2 = r * r + r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        if (dx > 0)
            gpu_fill_rect(cx - dx, cy + dy, dx * 2 + 1, 1, c);
        else
            gpu_draw_pixel(cx, cy + dy, c);
    }
}

/* Draw a string (wraps the uint8_t* GPU call) */
static void mb_str(int x, int y, const char *s, uint32_t fg) {
    gpu_draw_string(x, y, (const uint8_t *)s, fg, 0);
}

/* Draw string twice offset by 1 pixel for faux-bold */
static void mb_str_bold(int x, int y, const char *s, uint32_t fg) {
    gpu_draw_string(x,     y, (const uint8_t *)s, fg, 0);
    gpu_draw_string(x + 1, y, (const uint8_t *)s, fg, 0);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  REAL-TIME CLOCK
 *
 *  sys_get_time() reads hardware RTC registers directly.
 *  Returns 0 on success, fills hours/minutes/seconds.
 *  We do NOT simulate or fake the time.
 * ═══════════════════════════════════════════════════════════════════════ */
static void mb_draw_clock(int right_x, int y) {
    uint8_t hh = 0, mm = 0, ss = 0;
    sys_get_time(&hh, &mm, &ss);

    /* Format "HH:MM:SS" */
    char buf[12];
    buf[0] = '0' + hh / 10;
    buf[1] = '0' + hh % 10;
    buf[2] = ':';
    buf[3] = '0' + mm / 10;
    buf[4] = '0' + mm % 10;
    buf[5] = ':';
    buf[6] = '0' + ss / 10;
    buf[7] = '0' + ss % 10;
    buf[8] = '\0';

    int cw = mb_slen(buf) * 8;  /* 8px per char */
    mb_str(right_x - cw - 14, y, buf, 0xFFDDDDEE);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  DROPDOWN GEOMETRY
 *
 *  Compute the total height of a dropdown panel for a given menu.
 *  Separators count as SEP_H, regular items as ITEM_H.
 * ═══════════════════════════════════════════════════════════════════════ */
static int mb_drop_height(Menu *m) {
    int h = ITEM_PAD_V; /* top padding */
    for (int i = 0; i < m->item_count; i++) {
        h += m->items[i].separator ? SEP_H : ITEM_H;
    }
    h += ITEM_PAD_V; /* bottom padding */
    return h;
}

static int mb_drop_width(Menu *m) {
    int w = DROP_MIN_W;
    for (int i = 0; i < m->item_count; i++) {
        if (!m->items[i].separator) {
            int lw = mb_slen(m->items[i].label) * 8
                     + ITEM_INDENT * 2 + 16;
            if (lw > w) w = lw;
        }
    }
    return w;
}

/* Given a mouse y inside the dropdown panel, find which item index
 * the cursor is over.  Returns -1 if over padding or a separator. */
static int mb_item_at_y(Menu *m, int panel_y, int mouse_y) {
    int y = panel_y + ITEM_PAD_V;
    for (int i = 0; i < m->item_count; i++) {
        int row_h = m->items[i].separator ? SEP_H : ITEM_H;
        if (mouse_y >= y && mouse_y < y + row_h) {
            return m->items[i].separator ? -1 : i;
        }
        y += row_h;
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  PUBLIC API: LIFECYCLE
 * ═══════════════════════════════════════════════════════════════════════ */
void menubar_init(void) {
    g_menu_count  = 0;
    g_open_idx    = -1;
    g_hover_bar   = -1;
    g_hover_item  = -1;
}

void menubar_clear(void) {
    g_menu_count  = 0;
    g_open_idx    = -1;
    g_hover_bar   = -1;
    g_hover_item  = -1;
}

void menubar_add_menu(const char *title, MenuItem *items, int item_count) {
    if (g_menu_count >= MAX_MENUS) return;

    Menu *m = &g_menus[g_menu_count];
    m->title      = title;
    m->items      = items;
    m->item_count = item_count;

    /* Title bar_x: first menu starts after space for focused app name,
     * subsequent menus follow the previous one. */
    int title_w = mb_slen(title) * 8 + BAR_TITLE_PAD * 2;
    if (g_menu_count == 0) {
        m->bar_x = BAR_LEFT_MARGIN + 120;  /* 120px for app name */
    } else {
        Menu *prev = &g_menus[g_menu_count - 1];
        m->bar_x = prev->bar_x + prev->bar_w + BAR_TITLE_GAP;
    }
    m->bar_w = title_w;

    /* Dropdown geometry (pre-calculated) */
    m->drop_w = mb_drop_width(m);
    m->drop_h = mb_drop_height(m);

    /* Dropdown x: align with title, clamp to screen */
    int scr_w = wm_get_screen_w();
    m->drop_x = m->bar_x - BAR_TITLE_PAD;
    if (m->drop_x + m->drop_w > scr_w - 4)
        m->drop_x = scr_w - m->drop_w - 4;
    if (m->drop_x < 2) m->drop_x = 2;

    g_menu_count++;
}

void menubar_close(void) {
    g_open_idx   = -1;
    g_hover_item = -1;
}

bool menubar_is_open(void) {
    return g_open_idx >= 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  DRAW — MENU BAR BACKGROUND + TITLES
 * ═══════════════════════════════════════════════════════════════════════ */
static void mb_draw_bar(void) {
    extern OSSettings g_settings;
    int scr_w = wm_get_screen_w();
    int bar_h = WM_MENUBAR_HEIGHT;

    /* Background */
    if (g_settings.dock_transparent) {
        gpu_blur_rect(0, 0, scr_w, bar_h);

        /* Dark translucent base — lower alpha = more glass visible */
        gpu_fill_rect_blend(0, 0, scr_w, bar_h, 0xFF020205, 190);

        /* Top-edge shine (frosted glass highlight) */
        gpu_fill_rect_blend(0, 0, scr_w, 1, 0xFFFFFFFF, 28);

        /* Bottom separator */
        gpu_fill_rect_blend(0, bar_h - 1, scr_w, 1, 0xFF000000, 90);

        /* Subtle inner glow band */
        gpu_fill_rect_blend(0, 1, scr_w, 1, 0xFFFFFFFF, 10);
    } else {
        gpu_fill_rect(0, 0, scr_w, bar_h, 0xFF08080C);
        gpu_fill_rect(0, bar_h - 1, scr_w, 1, 0xFF1A1A22);
    }

    int ty = (bar_h - 8) / 2;

    /* ── Focused window name (LEFT, before menus) ──────────────── */
    Window *fw = wm_get_focused();
    if (fw && fw->visible) {
        int app_x = BAR_LEFT_MARGIN;
        char clean_title[64];
        clean_title[0] = '\0';
        mb_get_clean_title(fw->title, clean_title, (int)sizeof(clean_title));
        if (clean_title[0]) {
            mb_str_bold(app_x, ty, clean_title, 0xFFFFFFFF);
        }
    }

    /* ── Menu titles ──────────────────────────────────────────── */
    for (int i = 0; i < g_menu_count; i++) {
        Menu *m = &g_menus[i];

        /* Highlight open / hovered menu title */
        if (i == g_open_idx) {
            /* Open: filled accent-tinted pill */
            mb_rrect_a(m->bar_x - BAR_TITLE_PAD, 2,
                       m->bar_w, bar_h - 4,
                       WM_COLOR_ACCENT, 4, 55);
        } else if (i == g_hover_bar && g_open_idx < 0) {
            /* Hovered (no menu open): subtle white tint */
            mb_rrect_a(m->bar_x - BAR_TITLE_PAD, 2,
                       m->bar_w, bar_h - 4,
                       0xFFFFFFFF, 4, 18);
        }

        /* Title text */
        uint32_t tfg = (i == g_open_idx) ? 0xFFFFFFFF : 0xFFD8D8EA;
        mb_str(m->bar_x, ty, m->title, tfg);
    }

    /* ── Real-time clock ──────────────────────────────────────── */
    mb_draw_clock(scr_w, ty);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  DRAW — DROPDOWN PANEL
 *
 *  Glassmorphic panel with:
 *    • Blurred background
 *    • Dark translucent fill
 *    • Rounded corners
 *    • Subtle border
 *    • Top-edge shine
 *    • Soft shadow
 * ═══════════════════════════════════════════════════════════════════════ */
static void mb_draw_dropdown(void) {
    if (g_open_idx < 0 || g_open_idx >= g_menu_count) return;

    extern OSSettings g_settings;
    Menu *m = &g_menus[g_open_idx];

    int dx = m->drop_x;
    int dy = WM_MENUBAR_HEIGHT + 4;   /* 4px gap below bar */
    int dw = m->drop_w;
    int dh = m->drop_h;
    int cr = DROP_CORNER;

    /* ── Shadow ───────────────────────────────────────────────── */
    mb_rrect_a(dx + 2, dy + 4, dw, dh, 0xFF000000, cr, 12);
    mb_rrect_a(dx + 1, dy + 3, dw, dh, 0xFF000000, cr, 20);
    mb_rrect_a(dx,     dy + 2, dw, dh, 0xFF000000, cr, 35);

    /* ── Glass background ─────────────────────────────────────── */
    if (g_settings.dock_transparent) {
        gpu_blur_rect(dx, dy, dw, dh);

        /* Base tint */
        mb_rrect_a(dx, dy, dw, dh, 0xFF05050A, cr, 245);

        /* Inner shine band at top */
        gpu_fill_rect_blend(dx + cr, dy + 1, dw - 2 * cr, 1,
                            0xFFFFFFFF, 22);
    } else {
        mb_rrect(dx, dy, dw, dh, 0xFF0A0A12, cr);
    }

    /* ── Border ───────────────────────────────────────────────── */
    mb_rrect_outline(dx, dy, dw, dh, 0xFF606060, cr, 120);

    /* ── Top-edge highlight ───────────────────────────────────── */
    for (int i = 0; i < mb_min(cr, 2); i++) {
        int ins = mb_inset(cr, i);
        int sw  = dw - 2 * ins;
        if (sw > 0)
            gpu_fill_rect_blend(dx + ins, dy + i, sw, 1,
                                0xFFFFFFFF, i == 0 ? 40 : 20);
    }

    /* ── Items ────────────────────────────────────────────────── */
    int iy = dy + ITEM_PAD_V;
    for (int i = 0; i < m->item_count; i++) {
        MenuItem *item = &m->items[i];

        if (item->separator) {
            /* Separator line */
            int sy = iy + SEP_H / 2;
            gpu_fill_rect_blend(dx + 10, sy, dw - 20, 1,
                                0xFF606060, 160);
            iy += SEP_H;
            continue;
        }

        /* Hover / selection highlight */
        if (i == g_hover_item && item->enabled) {
            mb_rrect(dx + 4, iy, dw - 8, ITEM_H,
                     WM_COLOR_ACCENT, 5);
        }

        /* Label */
        int text_y = iy + (ITEM_H - 8) / 2;
        uint32_t fg;
        if (!item->enabled) {
            fg = 0xFF707070;                /* gray  */
        } else if (i == g_hover_item) {
            fg = 0xFFFFFFFF;                /* white on accent */
        } else {
            fg = 0xFFD8D8EA;                /* normal */
        }
        mb_str(dx + ITEM_INDENT, text_y, item->label, fg);

        iy += ITEM_H;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  PUBLIC API: DRAW
 * ═══════════════════════════════════════════════════════════════════════ */
void menubar_draw(void) {
    extern OSSettings g_settings;
    if (g_settings.window_style != 0) return;  /* macOS style only */

    mb_draw_bar();
    mb_draw_dropdown();   /* no-op if nothing open */
}

/* ═══════════════════════════════════════════════════════════════════════
 *  PUBLIC API: INPUT
 *
 *  Call every frame with raw mouse state.
 *  Returns true if this frame's event was consumed by the menu bar
 *  or an open dropdown (caller should skip window input).
 * ═══════════════════════════════════════════════════════════════════════ */
bool menubar_handle_mouse(int mx, int my, bool lmb, bool prev_lmb) {
    extern OSSettings g_settings;
    if (g_settings.window_style != 0) return false;

    bool click   = lmb && !prev_lmb;   /* rising edge = new click   */
    bool consumed = false;

    /* ── Case 1: A dropdown is open ──────────────────────────────
     *
     * Priority: check dropdown first so moving into the panel
     * feels natural (no flicker).
     */
    if (g_open_idx >= 0) {
        Menu *m   = &g_menus[g_open_idx];
        int   dx  = m->drop_x;
        int   dy  = WM_MENUBAR_HEIGHT + 4;
        int   dw  = m->drop_w;
        int   dh  = m->drop_h;

        bool in_panel = (mx >= dx && mx < dx + dw &&
                         my >= dy && my < dy + dh);

        bool in_bar = (my >= 0 && my < WM_MENUBAR_HEIGHT);

        if (in_panel) {
            /* Track hover item */
            g_hover_item = mb_item_at_y(m, dy, my);

            if (click && g_hover_item >= 0) {
                MenuItem *item = &m->items[g_hover_item];
                if (item->enabled && item->action) {
                    item->action();
                }
                menubar_close();
            }
            consumed = true;
        } else if (in_bar) {
            /* Mouse moved over bar while dropdown open:
             * switch to whichever menu title is hovered */
            g_hover_item = -1;
            int new_open = -1;
            for (int i = 0; i < g_menu_count; i++) {
                Menu *bar_m = &g_menus[i];
                if (mx >= bar_m->bar_x - BAR_TITLE_PAD &&
                    mx <  bar_m->bar_x + bar_m->bar_w - BAR_TITLE_PAD) {
                    new_open = i;
                    break;
                }
            }
            if (new_open >= 0 && new_open != g_open_idx) {
                /* Switch open menu on hover (macOS behavior) */
                g_open_idx   = new_open;
                g_hover_item = -1;
                /* Recalculate drop_x for new menu */
                Menu *nm = &g_menus[g_open_idx];
                int scr_w = wm_get_screen_w();
                nm->drop_x = nm->bar_x - BAR_TITLE_PAD;
                if (nm->drop_x + nm->drop_w > scr_w - 4)
                    nm->drop_x = scr_w - nm->drop_w - 4;
                if (nm->drop_x < 2) nm->drop_x = 2;
            }
            if (click && new_open < 0) {
                /* Clicked bar but not on a title → close */
                menubar_close();
            }
            consumed = true;
        } else {
            /* Clicked completely outside bar + panel → close */
            if (click) {
                menubar_close();
                /* Don't consume: let the window below get the click */
                consumed = false;
            } else {
                /* Mouse just moved outside, keep panel open */
                g_hover_item = -1;
            }
        }

        return consumed;
    }

    /* ── Case 2: No dropdown open ─────────────────────────────── */

    /* Is mouse in the menu bar? */
    if (my < 0 || my >= WM_MENUBAR_HEIGHT) {
        g_hover_bar = -1;
        return false;
    }

    /* Find hovered title */
    g_hover_bar = -1;
    for (int i = 0; i < g_menu_count; i++) {
        Menu *m = &g_menus[i];
        if (mx >= m->bar_x - BAR_TITLE_PAD &&
            mx <  m->bar_x + m->bar_w - BAR_TITLE_PAD) {
            g_hover_bar = i;
            break;
        }
    }

    if (click) {
        if (g_hover_bar >= 0) {
            /* Open this menu */
            g_open_idx   = g_hover_bar;
            g_hover_item = -1;

            /* Compute drop_x (clamped to screen) */
            Menu *m   = &g_menus[g_open_idx];
            int scr_w = wm_get_screen_w();
            m->drop_x  = m->bar_x - BAR_TITLE_PAD;
            if (m->drop_x + m->drop_w > scr_w - 4)
                m->drop_x = scr_w - m->drop_w - 4;
            if (m->drop_x < 2) m->drop_x = 2;
        }
        /* Consume any bar click */
        return true;
    }

    /* Hover-only in bar doesn't consume, but we return true to
     * prevent windows below from reacting to bar mouse-move */
    return (my >= 0 && my < WM_MENUBAR_HEIGHT);
}