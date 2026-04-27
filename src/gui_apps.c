// GUI Applications for Aurion OS Desktop
#include "window_manager.h"
#include <stdint.h>
#include <stdbool.h>

/* External functions */
extern uint32_t get_ticks(void);
extern int sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);
extern int sys_get_date(uint8_t *day, uint8_t *month, uint16_t *year);
extern void mem_get_stats(uint32_t *stats);
extern int fs_list_root(uint32_t out_dir_buffer, uint32_t max_entries);
extern char current_dir[256];

/* Helper functions */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int str_len(const char *s) {
    int l = 0;
    while (s[l]) l++;
    return l;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void int_to_str(uint32_t n, char *buf) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int i = 0;
    uint32_t tmp = n;
    while (tmp > 0) {
        tmp /= 10;
        i++;
    }
    buf[i] = '\0';
    while (n > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
}

/* NOTEPAD APPLICATION */
#define NOTEPAD_MAX_CHARS 3500
#define NOTEPAD_MAX_LINES 150
#define NOTEPAD_TOOLBAR_H 34  /* height of the Save/Discard toolbar */

/* Forward declarations */
extern int save_file_content(const char *filename, const char *data, int len);
extern int load_file_content(const char *filename, char *buffer, int max_len);

typedef struct {
    char text[NOTEPAD_MAX_CHARS];
    int cursor_pos;
    int text_len;
    char filename[64];   /* full path, empty if untitled */
    bool modified;
    bool save_hover;
    bool discard_hover;
    /* Save-As dialog state */
    bool saveas_open;        /* dialog visible */
    char saveas_input[64];   /* user is typing path here */
    int  saveas_len;         /* length of saveas_input */
    /* Right-click context menu */
    bool ctx_open;
    int ctx_x, ctx_y;
    int ctx_hover;           /* -1, 0=Copy, 1=Paste, 2=Select All */
    /* Text selection */
    int sel_start;           /* -1 = no selection */
    int sel_end;
    /* Scrolling */
    int scroll_offset;       /* number of lines scrolled down */
    uint32_t last_click_ticks;
} NotepadState;

/* Shared clipboard from terminal.c */
extern char os_clipboard[];
extern int  os_clipboard_len;

/* Save the notepad content to the filesystem */
static void notepad_save(NotepadState *state) {
    if (state->filename[0] == 0) {
        /* No filename - open the Save As dialog instead */
        state->saveas_open = true;
        state->saveas_input[0] = '\0';
        state->saveas_len = 0;
        return;
    }
    save_file_content(state->filename, state->text, state->text_len);
    state->modified = false;
}

/* Discard changes - reload from disk */
static void notepad_discard(NotepadState *state) {
    if (state->filename[0] == 0) {
        /* Untitled - just clear */
        state->text[0] = 0;
        state->text_len = 0;
        state->cursor_pos = 0;
        state->modified = false;
        return;
    }
    int n = load_file_content(state->filename, state->text, NOTEPAD_MAX_CHARS - 1);
    if (n < 0) n = 0;
    state->text[n] = 0;
    state->text_len = n;
    state->cursor_pos = n;
    state->modified = false;
}

static void notepad_on_draw(Window *win) {
    NotepadState *state = (NotepadState *)win->user_data;
    if (!state) return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* Toolbar background */
    wm_fill_rect(win, 0, 0, cw, NOTEPAD_TOOLBAR_H, 0xFFF0F0F0);
    wm_fill_rect(win, 0, NOTEPAD_TOOLBAR_H - 1, cw, 1, 0xFFCCCCCC);

    /* Save button */
    uint32_t save_bg = state->save_hover ? 0xFF4CAF50 : 0xFF388E3C;
    wm_fill_rect(win, 8, 6, 70, 22, save_bg);
    wm_draw_string(win, 18, 13, "Save", 0xFFFFFFFF, save_bg);

    /* Discard button */
    uint32_t disc_bg = state->discard_hover ? 0xFFEF5350 : 0xFFC62828;
    wm_fill_rect(win, 86, 6, 80, 22, disc_bg);
    wm_draw_string(win, 92, 13, "Discard", 0xFFFFFFFF, disc_bg);

    /* Filename label */
    const char *fname = state->filename[0] ? state->filename : "(untitled)";
    wm_draw_string(win, 178, 13, fname, 0xFF555555, 0xFFF0F0F0);

    /* Text area background */
    int text_top = NOTEPAD_TOOLBAR_H;
    wm_fill_rect(win, 0, text_top, cw, ch - text_top, 0xFFFFFFFF);

    /* Draw text with word wrap and scroll */
    int line_h = 16;
    int max_x = cw - 8;
    int cur_line = 0;  /* current visual line number */
    int x = 8, y = text_top + 6;

    /* Count visual lines to apply scroll */
    int skip_lines = state->scroll_offset;

    for (int i = 0; i < state->text_len; i++) {
        char c = state->text[i];
        if (c == '\n') {
            cur_line++;
            if (cur_line > skip_lines) y += line_h;
            x = 8;
        } else if (c >= 32 && c < 127) {
            if (x + 8 > max_x) {
                cur_line++;
                if (cur_line > skip_lines) y += line_h;
                x = 8;
            }
            if (cur_line >= skip_lines && y + line_h <= ch) {
                wm_draw_char(win, x, y, (uint8_t)c, 0xFF000000, 0xFFFFFFFF);
            }
            x += 8;
        }
        if (y + line_h > ch && cur_line > skip_lines) break;
    }

    /* Blinking cursor */
    int cx = 8, cy = text_top + 6 - (state->scroll_offset * line_h);
    for (int i = 0; i < state->cursor_pos && i < state->text_len; i++) {
        if (state->text[i] == '\n') {
            cx = 8; cy += line_h;
        } else {
            if (cx + 8 > max_x) { cx = 8; cy += line_h; }
            cx += 8;
        }
    }
    if (cy >= text_top && cy < ch && (get_ticks() % 120) < 60) {
        wm_fill_rect(win, cx, cy, 2, 14, 0xFF000000);
    }

    /* Selection highlight */
    if (state->sel_start >= 0 && state->sel_end > state->sel_start) {
        int sx = 8, sy = text_top + 6;
        for (int i = 0; i < state->text_len && sy < ch; i++) {
            char c = state->text[i];
            if (i >= state->sel_start && i < state->sel_end) {
                wm_fill_rect(win, sx, sy, 8, 14, 0xFF4A90D9);
            }
            if (c == '\n') { sx = 8; sy += line_h; }
            else {
                if (sx + 8 > max_x) { sx = 8; sy += line_h; }
                sx += 8;
            }
        }
    }

    /* Modified indicator */
    if (state->modified) {
        wm_draw_string(win, cw - 88, 13, "[Modified]", 0xFFE04050, 0xFFF0F0F0);
    }

    /* Right-click context menu */
    if (state->ctx_open) {
        int mw = 140, row_h = 24, pad = 3;
        int mh = 3 * row_h + pad * 2;
        int mx = state->ctx_x, my = state->ctx_y;
        const char *items[3] = {"Copy", "Paste", "Select All"};
        wm_fill_rect(win, mx, my, mw, mh, 0xFFF0F0F8);
        wm_fill_rect(win, mx, my, mw, 1, 0xFFA0A0B0);
        wm_fill_rect(win, mx, my + mh - 1, mw, 1, 0xFFA0A0B0);
        wm_fill_rect(win, mx, my, 1, mh, 0xFFA0A0B0);
        wm_fill_rect(win, mx + mw - 1, my, 1, mh, 0xFFA0A0B0);
        for (int i = 0; i < 3; i++) {
            int iy = my + pad + i * row_h;
            bool hov = (i == state->ctx_hover);
            uint32_t bg = hov ? WM_COLOR_ACCENT : 0xFFF0F0F8;
            uint32_t fg = hov ? WM_COLOR_WHITE  : 0xFF202030;
            wm_fill_rect(win, mx + 1, iy, mw - 2, row_h, bg);
            wm_draw_string(win, mx + 10, iy + 6, items[i], fg, bg);
            if (i < 2 && !hov)
                wm_fill_rect(win, mx + 4, iy + row_h - 1, mw - 8, 1, 0xFFC0C0D0);
        }
    }

    /* Save-As dialog overlay */
    if (state->saveas_open) {
        /* Dim background */
        wm_fill_rect(win, 0, 0, cw, ch, 0xCC000000);

        /* Dialog box */
        int dw = 380, dh = 130;
        int dx = (cw - dw) / 2;
        int dy = (ch - dh) / 2;
        wm_fill_rect(win, dx, dy, dw, dh, 0xFFF5F5F5);
        wm_fill_rect(win, dx, dy, dw, 26, 0xFF2D6A4F);
        wm_draw_string(win, dx + 10, dy + 8, "Save As - Enter full path", 0xFFFFFFFF, 0xFF2D6A4F);
        /* border */
        wm_draw_rect(win, dx, dy, dw, dh, 0xFF2D6A4F);

        wm_draw_string(win, dx + 10, dy + 36, "Path (e.g. /users/root/file.txt):", 0xFF333333, 0xFFF5F5F5);

        /* Input box */
        wm_fill_rect(win, dx + 10, dy + 54, dw - 20, 22, 0xFFFFFFFF);
        wm_draw_rect(win, dx + 10, dy + 54, dw - 20, 22, 0xFF2D6A4F);
        wm_draw_string(win, dx + 14, dy + 60, state->saveas_input, 0xFF000000, 0xFFFFFFFF);
        /* cursor blink in input box */
        if ((get_ticks() % 120) < 60) {
            int cx2 = dx + 14 + state->saveas_len * 8;
            wm_fill_rect(win, cx2, dy + 57, 2, 14, 0xFF000000);
        }

        /* Buttons */
        wm_fill_rect(win, dx + dw - 170, dy + 90, 70, 24, 0xFF388E3C);
        wm_draw_string(win, dx + dw - 155, dy + 97, "Save", 0xFFFFFFFF, 0xFF388E3C);
        wm_fill_rect(win, dx + dw - 90, dy + 90, 76, 24, 0xFF888888);
        wm_draw_string(win, dx + dw - 78, dy + 97, "Cancel", 0xFFFFFFFF, 0xFF888888);
    }
}

static void notepad_saveas_commit(NotepadState *state) {
    if (state->saveas_len == 0) return;
    str_copy(state->filename, state->saveas_input, 64);
    state->saveas_open = false;
    save_file_content(state->filename, state->text, state->text_len);
    state->modified = false;
}

/* Helper: find char index at pixel position (lx, ly) in text area */
static int notepad_pos_from_pixel(NotepadState *state, int lx, int ly, int text_top, int max_x)
{
    int line_h = 16;
    int px = 8, py = text_top + 6 - (state->scroll_offset * line_h);
    for (int i = 0; i <= state->text_len; i++) {
        /* Check if click is within this character cell */
        if (ly >= py && ly < py + line_h && lx >= px && lx < px + 8)
            return i;
        if (i == state->text_len) break;
        char c = state->text[i];
        if (c == '\n') { px = 8; py += line_h; }
        else {
            if (px + 8 > max_x) { px = 8; py += line_h; }
            px += 8;
        }
    }
    return state->text_len; /* click past end */
}

static void notepad_on_mouse(Window *win, int lx, int ly, bool left, bool right) {
    NotepadState *state = (NotepadState *)win->user_data;
    if (!state) return;

    static bool np_prev_left  = false;
    static bool np_prev_right = false;
    bool is_click  = left  && !np_prev_left;
    bool is_rclick = right && !np_prev_right;
    np_prev_left  = left;
    np_prev_right = right;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* If Save-As dialog is open, handle its buttons */
    if (state->saveas_open) {
        if (!is_click) return;
        int dw = 380, dh = 130;
        int dx = (cw - dw) / 2;
        int dy = (ch - dh) / 2;
        if (lx >= dx + dw - 170 && lx < dx + dw - 100 &&
            ly >= dy + 90 && ly < dy + 114) {
            notepad_saveas_commit(state);
        }
        if (lx >= dx + dw - 90 && lx < dx + dw - 14 &&
            ly >= dy + 90 && ly < dy + 114) {
            state->saveas_open = false;
        }
        return;
    }

    /* Right-click: open context menu */
    if (is_rclick && !state->saveas_open) {
        state->ctx_open  = true;
        state->ctx_x     = lx;
        state->ctx_y     = ly;
        state->ctx_hover = -1;
        return;
    }

    /* If context menu is open, handle it */
    if (state->ctx_open) {
        int mw = 140, row_h = 24, pad = 3;
        int mh = 3 * row_h + pad * 2;
        /* Update hover */
        state->ctx_hover = -1;
        if (lx >= state->ctx_x && lx < state->ctx_x + mw &&
            ly >= state->ctx_y + pad && ly < state->ctx_y + pad + 3 * row_h) {
            state->ctx_hover = (ly - state->ctx_y - pad) / row_h;
        }
        if (is_click) {
            if (state->ctx_hover == 0) {
                /* Copy - copy selection or whole text */
                int s = state->sel_start >= 0 ? state->sel_start : 0;
                int e = (state->sel_start >= 0 && state->sel_end > state->sel_start)
                        ? state->sel_end : state->text_len;
                int n = e - s;
                int cap = 4095;
                if (n > cap) n = cap;
                for (int i = 0; i < n; i++) os_clipboard[i] = state->text[s + i];
                os_clipboard[n] = 0;
                os_clipboard_len = n;
            } else if (state->ctx_hover == 1) {
                /* Paste */
                for (int i = 0; i < os_clipboard_len && state->text_len < NOTEPAD_MAX_CHARS - 1; i++) {
                    /* Insert at cursor */
                    for (int j = state->text_len; j > state->cursor_pos; j--)
                        state->text[j] = state->text[j - 1];
                    state->text[state->cursor_pos] = os_clipboard[i];
                    state->cursor_pos++;
                    state->text_len++;
                }
                state->text[state->text_len] = 0;
                state->modified = true;
            } else if (state->ctx_hover == 2) {
                /* Select All */
                state->sel_start = 0;
                state->sel_end   = state->text_len;
            }
            state->ctx_open = false;
        }
        /* Any click outside the menu closes it */
        if (is_click && (lx < state->ctx_x || lx >= state->ctx_x + mw ||
                         ly < state->ctx_y || ly >= state->ctx_y + mh)) {
            state->ctx_open = false;
        }
        return;
    }

    /* Hover state for toolbar buttons */
    state->save_hover    = (lx >= 8  && lx < 78  && ly >= 6 && ly < 28);
    state->discard_hover = (lx >= 86 && lx < 166 && ly >= 6 && ly < 28);

    if (!is_click) {
        /* Drag selection in text area */
        if (left && state->sel_start >= 0 && ly >= NOTEPAD_TOOLBAR_H) {
            int max_x = cw - 8;
            int text_top = NOTEPAD_TOOLBAR_H;
            state->sel_end = notepad_pos_from_pixel(state, lx, ly, text_top, max_x);
        }
        return;
    }

    if (state->save_hover) { notepad_save(state); return; }
    if (state->discard_hover) { notepad_discard(state); return; }

    /* Click in text area - start selection / move cursor */
    if (ly >= NOTEPAD_TOOLBAR_H) {
        int max_x = cw - 8;
        int text_top = NOTEPAD_TOOLBAR_H;
        int pos = notepad_pos_from_pixel(state, lx, ly, text_top, max_x);
        
        uint32_t now = get_ticks();
        if (now - state->last_click_ticks < 45) {
            /* Double click - select word */
            int start = pos, end = pos;
            while (start > 0 && state->text[start-1] != ' ' && state->text[start-1] != '\n') start--;
            while (end < state->text_len && state->text[end] != ' ' && state->text[end] != '\n') end++;
            state->cursor_pos = end;
            state->sel_start = start;
            state->sel_end = end;
            state->last_click_ticks = 0; /* Reset */
        } else {
            state->cursor_pos = pos;
            state->sel_start  = pos;
            state->sel_end    = pos;
            state->last_click_ticks = now;
        }
    }
}

static void notepad_on_key(Window *win, uint16_t key) {
    NotepadState *state = (NotepadState *)win->user_data;
    if (!state) return;

    uint8_t ascii = key & 0xFF;
    uint8_t scan = (key >> 8) & 0xFF;

    /* If Save-As dialog is open, route keys there */
    if (state->saveas_open) {
        if (ascii == 27) { /* ESC - cancel */
            state->saveas_open = false;
            return;
        }
        if (ascii == 13) { /* Enter - confirm */
            notepad_saveas_commit(state);
            return;
        }
        if ((ascii == 8 || scan == 0x0E) && state->saveas_len > 0) {
            state->saveas_len--;
            state->saveas_input[state->saveas_len] = '\0';
            return;
        }
        if (ascii >= 32 && ascii < 127 && state->saveas_len < 63) {
            state->saveas_input[state->saveas_len++] = (char)ascii;
            state->saveas_input[state->saveas_len] = '\0';
        }
        return; /* eat all keys while dialog open */
    }

    /* Backspace */
    if (ascii == 8 || scan == 0x0E) {
        if (state->cursor_pos > 0) {
            for (int i = state->cursor_pos - 1; i < state->text_len; i++) {
                state->text[i] = state->text[i + 1];
            }
            state->cursor_pos--;
            state->text_len--;
            state->modified = true;
        }
        return;
    }

    /* Enter */
    if (ascii == 13 || ascii == 10) {
        if (state->text_len < NOTEPAD_MAX_CHARS - 1) {
            for (int i = state->text_len; i > state->cursor_pos; i--) {
                state->text[i] = state->text[i - 1];
            }
            state->text[state->cursor_pos] = '\n';
            state->cursor_pos++;
            state->text_len++;
            state->modified = true;
        }
        return;
    }

    /* PgUp / PgDn for notepad scrolling */
    if (scan == 0x49) { /* PgUp */
        if (state->scroll_offset > 0) {
            state->scroll_offset -= 5;
            if (state->scroll_offset < 0) state->scroll_offset = 0;
        }
        return;
    }
    if (scan == 0x51) { /* PgDn */
        state->scroll_offset += 5;
        return;
    }

    /* Printable characters */
    if (ascii >= 32 && ascii < 127 && state->text_len < NOTEPAD_MAX_CHARS - 1) {
        for (int i = state->text_len; i > state->cursor_pos; i--) {
            state->text[i] = state->text[i - 1];
        }
        state->text[state->cursor_pos] = (char)ascii;
        state->cursor_pos++;
        state->text_len++;
        state->modified = true;
    }
}

#define OS_CLIPBOARD_CAP 4095

void notepad_edit_copy(Window *win)
{
    NotepadState *state = win ? (NotepadState *)win->user_data : NULL;
    if (!state) return;
    int s = state->sel_start >= 0 ? state->sel_start : 0;
    int e = (state->sel_start >= 0 && state->sel_end > state->sel_start)
                ? state->sel_end
                : state->text_len;
    int n = e - s;
    if (n > OS_CLIPBOARD_CAP) n = OS_CLIPBOARD_CAP;
    for (int i = 0; i < n; i++)
        os_clipboard[i] = state->text[s + i];
    os_clipboard[n] = 0;
    os_clipboard_len = n;
}

void notepad_edit_cut(Window *win)
{
    notepad_edit_copy(win);
    NotepadState *state = win ? (NotepadState *)win->user_data : NULL;
    if (!state) return;
    if (state->sel_start < 0 || state->sel_end <= state->sel_start) return;
    int s = state->sel_start;
    int e = state->sel_end;
    int len = e - s;
    for (int i = e; i <= state->text_len; i++)
        state->text[i - len] = state->text[i];
    state->text_len -= len;
    state->cursor_pos = s;
    if (state->cursor_pos > state->text_len) state->cursor_pos = state->text_len;
    state->sel_start = -1;
    state->sel_end = -1;
    state->modified = true;
}

void notepad_edit_paste(Window *win)
{
    NotepadState *state = win ? (NotepadState *)win->user_data : NULL;
    if (!state || state->saveas_open) return;
    for (int i = 0; i < os_clipboard_len && state->text_len < NOTEPAD_MAX_CHARS - 1; i++) {
        char c = os_clipboard[i];
        if (c == '\r') continue;
        for (int j = state->text_len; j > state->cursor_pos; j--)
            state->text[j] = state->text[j - 1];
        state->text[state->cursor_pos] = c;
        state->cursor_pos++;
        state->text_len++;
    }
    state->text[state->text_len] = 0;
    state->modified = true;
}

void notepad_edit_undo(Window *win)
{
    notepad_on_key(win, (uint16_t)(0x0E00u | 8u));
}

/* Forward declaration so filebrow_on_mouse can call this */
static void app_notepad_open_file(const char *filepath);

void app_notepad_create(void) {
    app_notepad_open_file(0);
}

static void app_notepad_open_file(const char *filepath) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = 500, h = 400;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Notepad", x, y, w, h);
    if (!win) return;

    NotepadState *state = (NotepadState *)win->app_data;
    win->user_data = state;

    for (int i = 0; i < (int)sizeof(NotepadState); i++) {
        ((char *)state)[i] = 0;
    }

    state->text[0] = '\0';
    state->cursor_pos = 0;
    state->text_len = 0;
    state->filename[0] = '\0';
    state->modified = false;
    state->save_hover = false;
    state->discard_hover = false;
    state->ctx_open = false;
    state->ctx_hover = -1;
    state->sel_start = -1;
    state->sel_end = -1;

    /* If a filepath was given, load its content */
    if (filepath && filepath[0]) {
        str_copy(state->filename, filepath, 64);
        int n = load_file_content(filepath, state->text, NOTEPAD_MAX_CHARS - 1);
        if (n < 0) n = 0;
        state->text[n] = 0;
        state->text_len = n;
        state->cursor_pos = n;
        state->modified = false;
    }

    win->on_draw  = notepad_on_draw;
    win->on_key   = notepad_on_key;
    win->on_mouse = notepad_on_mouse;
}

/* PAINT APPLICATION */
/*
 * Canvas is 40x30 pixels, each drawn as 12x12 on screen = 480x360 display.
 * Total state: 40*30*4 + 8 = 4808 bytes -- fits in app_data[4096]? No.
 * Use 40x28 canvas with 11x11 display pixels = 440x308 display.
 * Total state: 40*28*4 = 4480 bytes -- fits in app_data[4096] with room.
 *
 * Actually keep pixel size at 10, canvas 44x30 = 440x300, state=44*30*4=5280 -- too big.
 * Use 36x28 with pixel size 12 = 432x336. State = 36*28*4 = 4032 bytes. Fits!
*/

#define PAINT_CW 36
#define PAINT_CH 28
#define PAINT_PIXEL_SIZE 12
#define PAINT_NUM_COLORS 16

typedef struct {
    uint32_t canvas[PAINT_CW][PAINT_CH];
    uint32_t current_color;
    int brush_size; /* 1, 2, or 3 */
    int last_x, last_y;
    bool is_drawing;
} PaintState;

/* 16-color palette */
static const uint32_t paint_palette[PAINT_NUM_COLORS] = {
    0xFF000000, 0xFFFFFFFF, 0xFFFF0000, 0xFF00CC00,
    0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF,
    0xFF808080, 0xFFC0C0C0, 0xFF800000, 0xFF008000,
    0xFF000080, 0xFF808000, 0xFF800080, 0xFFFF8800,
};

static void paint_on_draw(Window *win) {
    PaintState *state = (PaintState *)win->user_data;
    if (!state) return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* Background */
    wm_fill_rect(win, 0, 0, cw, ch, 0xFF0A0A0A);

    /* Color palette (top row) */
    for (int i = 0; i < PAINT_NUM_COLORS; i++) {
        int px = 10 + i * 26;
        int py = 6;
        wm_fill_rect(win, px, py, 24, 24, paint_palette[i]);
        /* Selection indicator */
        if (paint_palette[i] == state->current_color) {
            wm_draw_rect(win, px - 1, py - 1, 26, 26, 0xFFFFFFFF);
            wm_draw_rect(win, px - 2, py - 2, 28, 28, 0xFF4ADE80);
        }
    }

    /* Brush size buttons */
    int bs_x = cw - 90;
    wm_draw_string(win, bs_x, 8, "Size:", 0xFFE0E0F0, 0xFF0A0A0A);
    for (int s = 1; s <= 3; s++) {
        int bx = bs_x + 44 + (s - 1) * 16;
        bool active = (state->brush_size == s);
        wm_fill_rect(win, bx, 6, 14, 14, active ? 0xFF4ADE80 : 0xFF505070);
        char ch_s = '0' + s;
        wm_draw_char(win, bx + 3, 9, (uint8_t)ch_s, 0xFFFFFFFF,
                     active ? 0xFF4ADE80 : 0xFF505070);
    }

    /* Clear button */
    wm_fill_rect(win, cw - 90, 24, 40, 12, 0xFFE04050);
    wm_draw_string(win, cw - 86, 26, "CLR", 0xFFFFFFFF, 0xFFE04050);

    /* Canvas border */
    int ox = 10, oy = 38;
    int canvas_disp_w = PAINT_CW * PAINT_PIXEL_SIZE;
    int canvas_disp_h = PAINT_CH * PAINT_PIXEL_SIZE;
    wm_draw_rect(win, ox - 1, oy - 1, canvas_disp_w + 2, canvas_disp_h + 2, 0xFF505070);

    /* Draw canvas pixels */
    for (int py = 0; py < PAINT_CH; py++) {
        for (int px = 0; px < PAINT_CW; px++) {
            wm_fill_rect(win, ox + px * PAINT_PIXEL_SIZE, oy + py * PAINT_PIXEL_SIZE,
                         PAINT_PIXEL_SIZE, PAINT_PIXEL_SIZE, state->canvas[px][py]);
        }
    }

    /* Current color indicator */
    wm_fill_rect(win, cw - 40, 24, 30, 12, state->current_color);
    wm_draw_rect(win, cw - 40, 24, 30, 12, 0xFFE0E0F0);
}

static void paint_plot_brush(PaintState *state, int cx, int cy) {
    int radius = state->brush_size - 1;
    if (radius < 0) radius = 0;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= r2 + radius) {
                int bx = cx + dx;
                int by = cy + dy;
                if (bx >= 0 && bx < PAINT_CW && by >= 0 && by < PAINT_CH) {
                    state->canvas[bx][by] = state->current_color;
                }
            }
        }
    }
}

static void paint_on_mouse(Window *win, int lx, int ly, bool left, bool right) {
    PaintState *state = (PaintState *)win->user_data;
    if (!state) return;
    (void)right;

    int cw = wm_client_w(win);

    /* Click palette/buttons */
    static bool prev_left = false;
    bool click = left && !prev_left;
    prev_left = left;

    if (click) {
        if (ly >= 4 && ly < 32) {
            for (int i = 0; i < PAINT_NUM_COLORS; i++) {
                int px = 10 + i * 26;
                if (lx >= px && lx < px + 24) {
                    state->current_color = paint_palette[i];
                    return;
                }
            }
            int bs_x = cw - 90;
            for (int s = 1; s <= 3; s++) {
                int bx = bs_x + 44 + (s - 1) * 16;
                if (lx >= bx && lx < bx + 14 && ly >= 6 && ly < 20) {
                    state->brush_size = s;
                    return;
                }
            }
        }
        if (lx >= cw - 90 && lx < cw - 50 && ly >= 24 && ly < 36) {
            for (int py = 0; py < PAINT_CH; py++)
                for (int px = 0; px < PAINT_CW; px++)
                    state->canvas[px][py] = 0xFFFFFFFF;
            return;
        }
    }

    /* Canvas drawing */
    int ox = 10, oy = 38;
    if (left && lx >= ox && ly >= oy) {
        int cx = (lx - ox) / PAINT_PIXEL_SIZE;
        int cy = (ly - oy) / PAINT_PIXEL_SIZE;

        if (!state->is_drawing) {
            state->is_drawing = true;
            paint_plot_brush(state, cx, cy);
        } else {
            /* Smooth line drawing (Bresenham) */
            int x0 = state->last_x, y0 = state->last_y;
            int x1 = cx, y1 = cy;
            int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1), sx = (x0 < x1) ? 1 : -1;
            int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1), sy = (y0 < y1) ? 1 : -1;
            int err = ((dx > dy) ? dx : -dy) / 2, e2;
            for (;;) {
                paint_plot_brush(state, x0, y0);
                if (x0 == x1 && y0 == y1) break;
                e2 = err;
                if (e2 > -dx) { err -= dy; x0 += sx; }
                if (e2 < dy) { err += dx; y0 += sy; }
            }
        }
        state->last_x = cx;
        state->last_y = cy;
    } else {
        state->is_drawing = false;
    }
}

void app_paint_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    /* Toolbar needs: 10px left + 16*26px colors = 426px + 90px brush/clear = 516px min.
       Canvas needs: 10px + 36*12 = 442px. Window width 540 gives comfortable room. */
    int w = 540, h = 410;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Paint", x, y, w, h);
    if (!win) return;

    PaintState *state = (PaintState *)win->app_data;
    win->user_data = state;

    /* Zero the state (safely within app_data bounds now) */
    for (int i = 0; i < (int)sizeof(PaintState); i++) {
        ((char *)state)[i] = 0;
    }

    /* Initialize canvas to white */
    for (int py = 0; py < PAINT_CH; py++) {
        for (int px = 0; px < PAINT_CW; px++) {
            state->canvas[px][py] = 0xFFFFFFFF;
        }
    }
    state->current_color = 0xFF000000;
    state->brush_size = 1;

    win->on_draw = paint_on_draw;
    win->on_mouse = paint_on_mouse;
}

/* CALCULATOR APPLICATION */
typedef struct {
    char display[32];
    double value;
    double stored;
    char op;
    bool new_input;
} CalcState;

static void calc_on_draw(Window *win) {
    CalcState *state = (CalcState *)win->user_data;
    if (!state) return;

    int cw = wm_client_w(win);
    
    /* Display */
    wm_fill_rect(win, 10, 10, cw - 20, 40, 0xFFE0E0E0);
    wm_draw_string(win, 20, 25, state->display, 0xFF000000, 0xFFE0E0E0);

    /* Buttons */
    const char *buttons[] = {"7", "8", "9", "/", "4", "5", "6", "*", 
                             "1", "2", "3", "-", "0", ".", "=", "+"};
    for (int i = 0; i < 16; i++) {
        int bx = 10 + (i % 4) * 50;
        int by = 60 + (i / 4) * 50;
        wm_fill_rect(win, bx, by, 45, 45, 0xFFD0D0D0);
        wm_draw_string(win, bx + 18, by + 18, buttons[i], 0xFF000000, 0xFFD0D0D0);
    }
    
    /* Clear button */
    wm_fill_rect(win, 10, 260, 95, 45, 0xFFFF6060);
    wm_draw_string(win, 38, 278, "C", 0xFFFFFFFF, 0xFFFF6060);
}

/* Simple double-to-string, handles integers and basic decimals */
static void calc_double_to_str(double val, char *buf, int max) {
    /* Handle negative */
    int pos = 0;
    if (val < 0) {
        buf[pos++] = '-';
        val = -val;
    }

    /* Clamp to avoid huge numbers */
    if (val > 999999999.0) {
        buf[0] = 'E'; buf[1] = 'r'; buf[2] = 'r'; buf[3] = '\0';
        return;
    }

    /* Integer part */
    uint32_t int_part = (uint32_t)val;
    double frac_part = val - (double)int_part;

    /* Write integer digits */
    char tmp[16];
    int ti = 0;
    if (int_part == 0) {
        tmp[ti++] = '0';
    } else {
        uint32_t n = int_part;
        while (n > 0 && ti < 15) {
            tmp[ti++] = '0' + (n % 10);
            n /= 10;
        }
        /* reverse */
        for (int a = 0, b = ti - 1; a < b; a++, b--) {
            char t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t;
        }
    }
    for (int i = 0; i < ti && pos < max - 1; i++)
        buf[pos++] = tmp[i];

    /* Fractional part - show up to 4 decimal places, strip trailing zeros */
    if (frac_part > 0.00001) {
        buf[pos++] = '.';
        for (int d = 0; d < 4 && pos < max - 1; d++) {
            frac_part *= 10.0;
            int digit = (int)frac_part;
            buf[pos++] = '0' + digit;
            frac_part -= digit;
        }
        /* Strip trailing zeros */
        while (pos > 1 && buf[pos - 1] == '0') pos--;
        if (buf[pos - 1] == '.') pos--; /* also remove lone dot */
    }

    buf[pos] = '\0';
}

static void calc_on_mouse(Window *win, int lx, int ly, bool left, bool right) {
    (void)right;
    CalcState *state = (CalcState *)win->user_data;

    /* Track left button state BEFORE any early returns so we always
       capture the release (left=false). If we put this after "if (!left) return"
       the static never resets and the second click never fires. */
    static bool calc_prev_left = false;
    bool is_click = left && !calc_prev_left;
    calc_prev_left = left;

    if (!state || !is_click) return;

    /* Button grid */
    const char *buttons[] = {"7", "8", "9", "/", "4", "5", "6", "*",
                             "1", "2", "3", "-", "0", ".", "=", "+"};

    for (int i = 0; i < 16; i++) {
        int bx = 10 + (i % 4) * 50;
        int by = 60 + (i / 4) * 50;
        if (lx >= bx && lx < bx + 45 && ly >= by && ly < by + 45) {
            char btn = buttons[i][0];

            if (btn >= '0' && btn <= '9') {
                /* Digit */
                if (state->new_input) {
                    /* Start fresh number */
                    state->display[0] = btn;
                    state->display[1] = '\0';
                    state->new_input = false;
                } else {
                    int len = str_len(state->display);
                    if (len < 30) {
                        state->display[len] = btn;
                        state->display[len + 1] = '\0';
                    }
                }
            } else if (btn == '.') {
                /* Decimal point - only add if not already present */
                bool has_dot = false;
                for (int j = 0; state->display[j]; j++) {
                    if (state->display[j] == '.') { has_dot = true; break; }
                }
                if (!has_dot && !state->new_input) {
                    int len = str_len(state->display);
                    if (len < 30) {
                        state->display[len] = '.';
                        state->display[len + 1] = '\0';
                    }
                }
            } else if (btn == '=') {
                /* Calculate result */
                if (state->op != 0) {
                    /* Parse current display as right-hand value */
                    double rhs = 0.0;
                    double frac = 0.0;
                    bool in_frac = false;
                    double frac_div = 10.0;
                    bool neg = false;
                    int start = 0;
                    if (state->display[0] == '-') { neg = true; start = 1; }
                    for (int j = start; state->display[j]; j++) {
                        char dc = state->display[j];
                        if (dc == '.') {
                            in_frac = true;
                        } else if (dc >= '0' && dc <= '9') {
                            if (in_frac) {
                                frac += (dc - '0') / frac_div;
                                frac_div *= 10.0;
                            } else {
                                rhs = rhs * 10.0 + (dc - '0');
                            }
                        }
                    }
                    rhs += frac;
                    if (neg) rhs = -rhs;

                    double result = 0.0;
                    if (state->op == '+') result = state->stored + rhs;
                    else if (state->op == '-') result = state->stored - rhs;
                    else if (state->op == '*') result = state->stored * rhs;
                    else if (state->op == '/') {
                        if (rhs != 0.0) result = state->stored / rhs;
                        else { /* Division by zero */
                            state->display[0]='E'; state->display[1]='r';
                            state->display[2]='r'; state->display[3]='\0';
                            state->op = 0;
                            state->new_input = true;
                            return;
                        }
                    }

                    state->value = result;
                    calc_double_to_str(result, state->display, 32);
                    state->op = 0;
                    state->new_input = true;
                }
            } else {
                /* Operator: +, -, *, / */
                /* Store current display value and remember operator */
                double lhs = 0.0;
                double frac = 0.0;
                bool in_frac = false;
                double frac_div = 10.0;
                bool neg = false;
                int start = 0;
                if (state->display[0] == '-') { neg = true; start = 1; }
                for (int j = start; state->display[j]; j++) {
                    char dc = state->display[j];
                    if (dc == '.') {
                        in_frac = true;
                    } else if (dc >= '0' && dc <= '9') {
                        if (in_frac) {
                            frac += (dc - '0') / frac_div;
                            frac_div *= 10.0;
                        } else {
                            lhs = lhs * 10.0 + (dc - '0');
                        }
                    }
                }
                lhs += frac;
                if (neg) lhs = -lhs;

                state->stored = lhs;
                state->op = btn;
                state->new_input = true;
            }
            return;
        }
    }

    /* Clear button */
    if (lx >= 10 && lx < 105 && ly >= 260 && ly < 305) {
        state->display[0] = '0';
        state->display[1] = '\0';
        state->value = 0;
        state->stored = 0;
        state->op = 0;
        state->new_input = true;
    }
}

void app_calc_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = 220, h = 340;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Calculator", x, y, w, h);
    if (!win) return;

    CalcState *state = (CalcState *)win->app_data;
    win->user_data = state;

    for (int i = 0; i < (int)sizeof(CalcState); i++) {
        ((char *)state)[i] = 0;
    }

    state->display[0] = '0';
    state->display[1] = '\0';
    state->value = 0;
    state->stored = 0;
    state->op = 0;
    state->new_input = true;

    win->on_draw = calc_on_draw;
    win->on_mouse = calc_on_mouse;
}

/* FILE BROWSER APPLICATION */

/* Access the real filesystem from commands.c */
extern int fs_count;
typedef struct {
    char name[56];
    uint32_t size;
    uint8_t type; /* 0=file, 1=dir */
    uint8_t attr;
    uint16_t parent_idx;
    uint16_t reserved;
} FileBrowFSEntry;
extern FileBrowFSEntry fs_table[];
extern char current_dir[256];

/* Size budget: app_data is 4096 bytes.
 * path[128] + entries[40][32] + type[40] + size[40*4] + 3 ints
 * = 128 + 1280 + 40 + 160 + 12 = 1620 bytes. Fits comfortably. */

#define FILEBROW_MAX_ENTRIES 40
#define FILEBROW_NAME_MAX    32
#define FILEBROW_BACK_MAX     4
#define FB_TOOLBAR_H          34
#define FB_ADDR_H             26
#define FB_HDR_H              22
#define FB_LIST_TOP           (FB_TOOLBAR_H + FB_ADDR_H + FB_HDR_H)

typedef struct {
    char path[128];
    char back_stack[FILEBROW_BACK_MAX][128];
    int back_sp;
    char entries[FILEBROW_MAX_ENTRIES][FILEBROW_NAME_MAX];
    uint8_t entry_type[FILEBROW_MAX_ENTRIES];
    uint32_t entry_size[FILEBROW_MAX_ENTRIES];
    int entry_count;
    int scroll;
    int selected;
    int toolbar_hover; /* -1 none, 0 back 1 up 2 refresh */
} FileBrowState;

static void fb_scan(FileBrowState *state);

static int fb_str_len(const char *s) { int l=0; while(s[l]) l++; return l; }
static void fb_str_copy(char *d, const char *s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static int fb_starts_with(const char *s, const char *p) {
    while (*p) {
        char cs = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
        char cp = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
        if (cs != cp) return 0;
        s++; p++;
    }
    return 1;
}

static int fb_name_cmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if (ca != cb) return (int)(ca - cb);
        a++; b++;
    }
    return (int)(*a - *b);
}

static void fb_hist_push(FileBrowState *s) {
    if (s->back_sp >= FILEBROW_BACK_MAX) {
        for (int i = 0; i < FILEBROW_BACK_MAX - 1; i++)
            fb_str_copy(s->back_stack[i], s->back_stack[i + 1], 128);
        s->back_sp = FILEBROW_BACK_MAX - 1;
    }
    fb_str_copy(s->back_stack[s->back_sp], s->path, 128);
    s->back_sp++;
}

static void fb_hist_back(FileBrowState *s) {
    if (s->back_sp <= 0) return;
    s->back_sp--;
    fb_str_copy(s->path, s->back_stack[s->back_sp], 128);
    fb_scan(s);
}

static void fb_hist_clear(FileBrowState *s) { s->back_sp = 0; }

/* Populate entries for the given path */
static void fb_scan(FileBrowState *state) {
    state->entry_count = 0;
    state->scroll = 0;
    state->selected = 0;
    int path_len = fb_str_len(state->path);

    for (int i = 0; i < fs_count && state->entry_count < FILEBROW_MAX_ENTRIES; i++) {
        const char *name = fs_table[i].name;
        if (!fb_starts_with(name, state->path)) continue;
        const char *rest = name + path_len;
        if (rest[0] == 0) continue;
        bool sub = false;
        for (int j = 0; rest[j]; j++) {
            if (rest[j] == '/') { sub = true; break; }
        }
        if (sub) continue;
        fb_str_copy(state->entries[state->entry_count], rest, FILEBROW_NAME_MAX);
        state->entry_type[state->entry_count] = fs_table[i].type;
        state->entry_size[state->entry_count] = fs_table[i].size;
        state->entry_count++;
    }

      /* Directories first, then files; sort by name (A–Z). */
    for (int a = 0; a < state->entry_count - 1; a++) {
        for (int b = a + 1; b < state->entry_count; b++) {
            int ta = state->entry_type[a], tb = state->entry_type[b];
            int swap = 0;
            if (ta != tb) {
                if (ta < tb) swap = 1; /* dir (1) before file (0) */
            } else if (fb_name_cmp(state->entries[a], state->entries[b]) > 0) {
                swap = 1;
            }
            if (swap) {
                char tn[FILEBROW_NAME_MAX];
                fb_str_copy(tn, state->entries[a], FILEBROW_NAME_MAX);
                fb_str_copy(state->entries[a], state->entries[b], FILEBROW_NAME_MAX);
                fb_str_copy(state->entries[b], tn, FILEBROW_NAME_MAX);
                uint8_t tt = state->entry_type[a];
                state->entry_type[a] = state->entry_type[b];
                state->entry_type[b] = tt;
                uint32_t ts = state->entry_size[a];
                state->entry_size[a] = state->entry_size[b];
                state->entry_size[b] = ts;
            }
        }
    }
}

static void filebrow_on_draw(Window *win) {
    FileBrowState *state = (FileBrowState *)win->user_data;
    if (!state) return;

    /* Auto-refresh if file system count changed (new file created in terminal) */
    static int last_fs_count = 0;
    if (fs_count != last_fs_count) {
        fb_scan(state);
        last_fs_count = fs_count;
    }

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    wm_fill_rect(win, 0, 0, cw, ch, 0xFFFFFFFF);

    /* Toolbar (Back / Up / Refresh) */
    wm_fill_rect(win, 0, 0, cw, FB_TOOLBAR_H, 0xFFF9F9F9);
    wm_fill_rect(win, 0, FB_TOOLBAR_H - 1, cw, 1, 0xFFD0D0D0);

    uint32_t b0 = 0xFFECECEC, b1 = 0xFFDADADA;
    uint32_t bh0 = 0xFFD0D0D0, bh1 = 0xFFA0A0A0;

    int bx = 8;
    for (int b = 0; b < 3; b++) {
        int bw = (b == 0) ? 52 : ((b == 1) ? 40 : 64);
        uint32_t tb = (state->toolbar_hover == b) ? bh0 : b0;
        uint32_t tbb = (state->toolbar_hover == b) ? bh1 : b1;
        wm_fill_rect(win, bx, 5, bw, 24, tb);
        wm_draw_rect(win, bx, 5, bw, 24, tbb);
        const char *cap = (b == 0) ? "Back" : ((b == 1) ? "Up" : "Refresh");
        int cl = 0;
        while (cap[cl]) cl++;
        wm_draw_string(win, bx + (bw - cl * 8) / 2, 11, cap,
                       0xFF202020, tb);
        bx += bw + 6;
    }

    /* Address bar */
    int ay = FB_TOOLBAR_H + 3;
    wm_fill_rect(win, 6, ay, cw - 12, FB_ADDR_H - 6, 0xFFFFFFFF);
    wm_draw_rect(win, 6, ay, cw - 12, FB_ADDR_H - 6, 0xFFC8C8C8);
    wm_draw_string(win, 12, ay + 6, state->path, 0xFF101010, 0xFFFFFFFF);

    /* Column headers */
    int hx = FB_LIST_TOP - FB_HDR_H;
    wm_fill_rect(win, 0, hx, cw, FB_HDR_H, 0xFFF5F5F5);
    wm_fill_rect(win, 0, hx + FB_HDR_H - 1, cw, 1, 0xFFE0E0E0);
    wm_draw_string(win, 8, hx + 6, "Name", 0xFF303030, 0xFFF5F5F5);
    wm_draw_string(win, cw - 220, hx + 6, "Date modified", 0xFF303030, 0xFFF5F5F5);
    wm_draw_string(win, cw - 120, hx + 6, "Type", 0xFF303030, 0xFFF5F5F5);
    wm_draw_string(win, cw - 52, hx + 6, "Size", 0xFF303030, 0xFFF5F5F5);

    int row_h = 20;
    int start_y = FB_LIST_TOP;
    int list_bottom = ch - 20;
    int visible = (list_bottom - start_y) / row_h;
    if (visible < 1) visible = 1;

    if (fb_str_len(state->path) > 3) {
        wm_fill_rect(win, 0, start_y, cw, row_h, 0xFFF8FBFF);
        wm_draw_string(win, 28, start_y + 5, "..", 0xFF0B5CAD, 0xFFF8FBFF);
        start_y += row_h;
        visible--;
    }

    for (int i = state->scroll; i < state->entry_count && visible > 0; i++, visible--) {
        int ey = start_y + (i - state->scroll) * row_h;
        bool sel = (i == state->selected);
        uint32_t bg = sel ? 0xFFCDE8FF
                          : ((i % 2 == 0) ? 0xFFFFFFFF : 0xFFFAFAFA);
        uint32_t fg = 0xFF101010;

        wm_fill_rect(win, 0, ey, cw, row_h, bg);
        if (sel)
            wm_draw_rect(win, 0, ey, cw, row_h, 0xFF99C8FF);

        const char *sym = state->entry_type[i] == 1 ? ">" : "";
        if (sym[0])
            wm_draw_string(win, 10, ey + 5, sym, 0xFFCA8A00, bg);
        wm_draw_string(win, 28, ey + 5, state->entries[i], fg, bg);

        wm_draw_string(win, cw - 220, ey + 5, "--", 0xFF606060, bg);

        const char *type_label = state->entry_type[i] == 1
            ? "File folder" : "File";
        wm_draw_string(win, cw - 120, ey + 5, type_label, 0xFF606060, bg);

        if (state->entry_type[i] == 0) {
            char sz[14];
            uint32_t s = state->entry_size[i];
            if (s == 0) {
                sz[0] = '0'; sz[1] = 0;
            } else {
                int ti = 0;
                uint32_t tmp = s;
                char tmp2[14];
                while (tmp > 0) {
                    tmp2[ti++] = '0' + (tmp % 10);
                    tmp /= 10;
                }
                for (int k = 0; k < ti; k++)
                    sz[k] = tmp2[ti - 1 - k];
                sz[ti] = 0;
            }
            wm_draw_string(win, cw - 52, ey + 5, sz, fg, bg);
        }
    }

    if (state->entry_count == 0) {
        wm_draw_string(win, cw / 2 - 88, (ch + FB_LIST_TOP) / 2,
                       "This folder is empty.", 0xFF808080, 0xFFFFFFFF);
    }

    /* Status bar */
    wm_fill_rect(win, 0, ch - 16, cw, 16, 0xFFE8E8F0);
    char status[64];
    /* Build: "N items" */
    int ec = state->entry_count;
    char nbuf[8];
    if (ec == 0) {
        nbuf[0] = '0';
        nbuf[1] = 0;
    } else {
        int ti = 0;
        int tmp = ec;
        char tmp2[8];
        while (tmp > 0) {
            tmp2[ti++] = '0' + (tmp % 10);
            tmp /= 10;
        }
        for (int k = 0; k < ti; k++) {
            nbuf[k] = tmp2[ti - 1 - k];
        }
        nbuf[ti] = 0;
    }
    fb_str_copy(status, nbuf, 64);
    int sl = fb_str_len(status);
    const char *suf = " item(s)";
    for (int k=0; suf[k] && sl<63; k++) status[sl++]=suf[k];
    status[sl]=0;
    wm_draw_string(win, 4, ch - 13, status, 0xFF404050, 0xFFE8E8F0);
}

/* Navigate into a directory or open a file from the file browser */
static void fb_open_entry(FileBrowState *state, int idx) {
    if (idx < 0 || idx >= state->entry_count) return;

    if (state->entry_type[idx] == 1) {
        fb_hist_push(state);
        int pl = fb_str_len(state->path);
        const char *sub = state->entries[idx];
        int sl2 = fb_str_len(sub);
        if (pl + sl2 + 2 < 128) {
            for (int k = 0; k < sl2; k++) state->path[pl + k] = sub[k];
            state->path[pl + sl2] = '/';
            state->path[pl + sl2 + 1] = 0;
        }
        fb_scan(state);
    } else {
        /* File - open in Notepad with real content */
        char full_path[128];
        int pl = fb_str_len(state->path);
        for (int k = 0; k < pl && k < 127; k++) full_path[k] = state->path[k];
        const char *fn = state->entries[idx];
        int fl = fb_str_len(fn);
        for (int k = 0; k < fl && pl + k < 127; k++) full_path[pl + k] = fn[k];
        full_path[pl + fl] = 0;
        app_notepad_open_file(full_path);
    }
}

static void filebrow_on_mouse(Window *win, int lx, int ly, bool left, bool right) {
    (void)right;
    FileBrowState *state = (FileBrowState *)win->user_data;
    if (!state) return;

    static bool fb_prev_left = false;
    static uint32_t fb_last_click_tick = 0;
    static int fb_last_click_idx = -1;

    state->toolbar_hover = -1;
    if (ly >= 5 && ly < 29) {
        int bx = 8;
        for (int b = 0; b < 3; b++) {
            int bw = (b == 0) ? 52 : ((b == 1) ? 40 : 64);
            if (lx >= bx && lx < bx + bw) state->toolbar_hover = b;
            bx += bw + 6;
        }
    }

    bool is_click = left && !fb_prev_left;
    fb_prev_left = left;

    if (is_click && ly >= 5 && ly < 29 && state->toolbar_hover >= 0) {
        if (state->toolbar_hover == 0)
            fb_hist_back(state);
        else if (state->toolbar_hover == 1) {
            int pl = fb_str_len(state->path);
            if (pl > 3) {
                int i = pl - 2;
                while (i > 0 && state->path[i] != '/') i--;
                state->path[i + 1] = 0;
                fb_hist_clear(state);
                fb_scan(state);
            }
            fb_last_click_idx = -1;
        } else {
            fb_scan(state);
        }
        return;
    }

    if (!is_click) return;

    int row_h = 20;
    int start_y = FB_LIST_TOP;
    bool has_up = (fb_str_len(state->path) > 3);
    if (has_up) {
        if (ly >= start_y && ly < start_y + row_h) {
            int pl = fb_str_len(state->path);
            if (pl > 3) {
                int i = pl - 2;
                while (i > 0 && state->path[i] != '/') i--;
                state->path[i + 1] = 0;
                fb_hist_clear(state);
                fb_scan(state);
                fb_last_click_idx = -1;
            }
            return;
        }
        start_y += row_h;
    }

    if (ly < start_y) return;
    int rel = ly - start_y;
    int clicked_idx = state->scroll + (rel / row_h);
    if (clicked_idx < 0 || clicked_idx >= state->entry_count) return;

    uint32_t now = get_ticks();
    bool is_double = (clicked_idx == fb_last_click_idx &&
                      (now - fb_last_click_tick) < 40);

    state->selected = clicked_idx;

    if (is_double) {
        fb_open_entry(state, clicked_idx);
        fb_last_click_idx = -1;
    } else {
        fb_last_click_tick = now;
        fb_last_click_idx = clicked_idx;
    }
}

static void filebrow_on_key(Window *win, uint16_t key) {
    FileBrowState *state = (FileBrowState *)win->user_data;
    if (!state) return;
    uint8_t scan = (key >> 8) & 0xFF;
    /* Up/Down arrows to navigate */
    if (scan == 0x48 && state->selected > 0) state->selected--;
    if (scan == 0x50 && state->selected < state->entry_count - 1) state->selected++;
    /* PgUp - jump 10 entries up */
    if (scan == 0x49) {
        state->selected -= 10;
        if (state->selected < 0) state->selected = 0;
    }
    /* PgDn - jump 10 entries down */
    if (scan == 0x51) {
        state->selected += 10;
        if (state->selected >= state->entry_count) state->selected = state->entry_count - 1;
        if (state->selected < 0) state->selected = 0;
    }
    int ch_h = wm_client_h(win);
    int row_h = 20;
    int visible = (ch_h - FB_LIST_TOP - 20) / row_h;
    if (visible < 1) visible = 1;
    if (state->selected < state->scroll) state->scroll = state->selected;
    if (state->selected >= state->scroll + visible) state->scroll = state->selected - visible + 1;
    /* Enter to open entry (dir or file) */
    if ((key & 0xFF) == 13) {
        fb_open_entry(state, state->selected);
    }
    if ((key & 0xFF) == 8) {
        int pl = fb_str_len(state->path);
        if (pl > 3) {
            int i = pl - 2;
            while (i > 0 && state->path[i] != '/') i--;
            state->path[i+1] = 0;
            fb_hist_clear(state);
            fb_scan(state);
        }
    }
}

void app_filebrowser_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = sw > 900 ? 840 : (sw - 48);
    if (w < 440) w = (sw > 24) ? (sw - 24) : 440;
    int h = sh > 640 ? 540 : (sh - WM_TASKBAR_HEIGHT - 48);
    if (h < 360) h = (sh > WM_TASKBAR_HEIGHT + 60)
        ? (sh - WM_TASKBAR_HEIGHT - 60) : 360;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("File Explorer", x, y, w, h);
    if (!win) return;

    FileBrowState *state = (FileBrowState *)win->app_data;
    win->user_data = state;

    for (int i = 0; i < (int)sizeof(FileBrowState); i++) ((char*)state)[i] = 0;

    /* Start at current directory */
    fb_str_copy(state->path, current_dir, 128);
    /* Ensure trailing backslash */
    int pl = fb_str_len(state->path);
    if (pl > 0 && state->path[pl-1] != '/') {
        state->path[pl] = '/';
        state->path[pl+1] = 0;
    }

    fb_scan(state);

    win->on_draw  = filebrow_on_draw;
    win->on_key   = filebrow_on_key;
    win->on_mouse = filebrow_on_mouse;
}

/* CLOCK APPLICATION */
static void clock_on_draw(Window *win) {
    uint8_t h, m, s;
    sys_get_time(&h, &m, &s);
    h = (h + 1) % 24;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    wm_fill_rect(win, 0, 0, cw, ch, 0xFF000000);

    /* Digital clock display */
    char time_str[16];
    time_str[0] = '0' + (h / 10);
    time_str[1] = '0' + (h % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (m / 10);
    time_str[4] = '0' + (m % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (s / 10);
    time_str[7] = '0' + (s % 10);
    time_str[8] = '\0';

    /* Center the time */
    int text_w = 8 * 8;
    int tx = (cw - text_w) / 2;
    int ty = ch / 2 - 20;

    wm_draw_string(win, tx, ty, time_str, 0xFF4ADE80, 0xFF000000);

    /* Date */
    uint8_t day, month;
    uint16_t year;
    sys_get_date(&day, &month, &year);

    char date_str[32];
    int pos = 0;
    
    /* Year */
    date_str[pos++] = '0' + (year / 1000);
    date_str[pos++] = '0' + ((year / 100) % 10);
    date_str[pos++] = '0' + ((year / 10) % 10);
    date_str[pos++] = '0' + (year % 10);
    date_str[pos++] = '-';
    
    /* Month */
    date_str[pos++] = '0' + (month / 10);
    date_str[pos++] = '0' + (month % 10);
    date_str[pos++] = '-';
    
    /* Day */
    date_str[pos++] = '0' + (day / 10);
    date_str[pos++] = '0' + (day % 10);
    date_str[pos] = '\0';

    wm_draw_string(win, (cw - pos * 8) / 2, ty + 30, date_str, 0xFF808090, 0xFF000000);
}

void app_clock_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = 280, h = 180;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Clock", x, y, w, h);
    if (!win) return;

    win->on_draw = clock_on_draw;
}

/* SYSTEM INFO APPLICATION */
static void sysinfo_on_draw(Window *win) {
    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    wm_fill_rect(win, 0, 0, cw, ch, 0xFF000000);

    int y = 10;
    wm_draw_string(win, 10, y, "AurionOS v1.1 Beta", 0xFF4ADE80, 0xFF000000);
    y += 20;

    wm_draw_string(win, 10, y, "32-bit x86 Operating System", 0xFFE0E0F0, 0xFF000000);
    y += 20;

    /* Memory info */
    extern uint32_t bios_ram_bytes;
    uint32_t stats[4];
    mem_get_stats(stats);

    char buf[64];
    /* Total RAM */
    str_copy(buf, "Total RAM: ", 64);
    int len = str_len(buf);
    int_to_str(bios_ram_bytes / (1024 * 1024), buf + len);
    len = str_len(buf);
    str_copy(buf + len, " MB", 64 - len);
    wm_draw_string(win, 10, y, buf, 0xFFE0E0F0, 0xFF000000);
    y += 18;

    /* Total Free */
    str_copy(buf, "Heap Free: ", 64);
    len = str_len(buf);
    int_to_str(stats[0], buf + len);
    len = str_len(buf);
    str_copy(buf + len, " bytes (", 64 - len);
    len = str_len(buf);
    int_to_str(stats[0] / (1024 * 1024), buf + len);
    len = str_len(buf);
    str_copy(buf + len, " MB)", 64 - len);
    wm_draw_string(win, 10, y, buf, 0xFF4ADE80, 0xFF000000);
    y += 18;

    /* Total Used */
    str_copy(buf, "Heap Used: ", 64);
    len = str_len(buf);
    int_to_str(stats[1], buf + len);
    len = str_len(buf);
    str_copy(buf + len, " bytes (", 64 - len);
    len = str_len(buf);
    int_to_str(stats[1] / (1024 * 1024), buf + len);
    len = str_len(buf);
    str_copy(buf + len, " MB)", 64 - len);
    wm_draw_string(win, 10, y, buf, 0xFFE0E0F0, 0xFF000000);
    y += 24;

    /* Screen resolution */
    str_copy(buf, "Resolution: ", 64);
    len = str_len(buf);
    int_to_str(wm_get_screen_w(), buf + len);
    len = str_len(buf);
    buf[len++] = 'x';
    int_to_str(wm_get_screen_h(), buf + len);
    wm_draw_string(win, 10, y, buf, 0xFFE0E0F0, 0xFF000000);
}

void app_sysinfo_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = 350, h = 250;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("System Info", x, y, w, h);
    if (!win) return;

    win->on_draw = sysinfo_on_draw;
}

/* SETTINGS APPLICATION */
typedef struct {
    int tab; /* 0=Appearance, 1=System, 2=Dock, 3=Display */
    int scroll;
    bool show_reboot_dialog;
} SettingsState;

static const char *res_names[] = {
    "800x600", "1024x768", "1280x720", "1280x1024",
    "1440x900", "1600x900", "1920x1080", "2560x1440"
};

static void settings_on_draw(Window *win) {
    SettingsState *state = (SettingsState *)win->user_data;
    if (!state) return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* Sidebar */
    int sb_w = 120;
    wm_fill_rect(win, 0, 0, sb_w, ch, 0xFF000000);
    wm_fill_rect(win, sb_w - 1, 0, 1, ch, 0xFF303050);

    const char *tabs[] = {"Appearance", "System", "Dock", "Display"};
    for (int i = 0; i < 4; i++) {
        bool sel = (state->tab == i);
        if (sel) wm_fill_rect(win, 0, 10 + i * 36, sb_w - 1, 36, WM_COLOR_ACCENT);
        wm_draw_string(win, 10, 20 + i * 36, tabs[i], sel ? 0xFFFFFFFF : 0xFF808090, 
                       sel ? WM_COLOR_ACCENT : 0xFF000000);
    }

    /* Main Content */
    int content_x = sb_w + 20;
    int y = 20;
    
    if (state->tab == 0) {
        /* Appearance */
        wm_draw_string(win, content_x, y, "Background:", 0xFFFFFFFF, WM_COLOR_WINDOW_BG);
        y += 24;
        const char *colors[] = {"Default"};
        for (int i = 0; i < 1; i++) {
            bool sel = (g_settings.bg_color == (uint32_t)i);
            wm_fill_rect(win, content_x, y, 100, 24, sel ? WM_COLOR_ACCENT : 0xFF333333);
            wm_draw_string(win, content_x + 10, y + 6, colors[i], 0xFFFFFFFF, 
                           sel ? WM_COLOR_ACCENT : 0xFF333333);
            y += 28;
        }

        y += 20;
        wm_draw_string(win, content_x, y, "Window Style:", 0xFFFFFFFF, WM_COLOR_WINDOW_BG);
        y += 24;
        const char *styles[] = {"MacOS Style", "Windows Style"};
        for (int i = 0; i < 2; i++) {
            bool sel = (g_settings.window_style == i);
            wm_fill_rect(win, content_x, y, 140, 24, sel ? WM_COLOR_ACCENT : 0xFF333333);
            wm_draw_string(win, content_x + 10, y + 6, styles[i], 0xFFFFFFFF, 
                           sel ? WM_COLOR_ACCENT : 0xFF333333);
            y += 28;
        }
    } else if (state->tab == 1) {
        /* System — startup maps to desktop_apps index (see desktop.c) */
        wm_draw_string(win, content_x, y, "Startup Application:", 0xFFFFFFFF, WM_COLOR_WINDOW_BG);
        y += 24;
        static const char *const su_label[] = {
            "None", "Terminal", "File Explorer", "Blaze", "Notepad",
            "Paint", "Calculator", "Clock", "System Info"
        };
        static const int su_idx[] = {
            -1, 0, 1, 2, 3, 4, 5, 6, 7
        };
        for (int i = 0; i < 9; i++) {
            bool sel = (g_settings.startup_app_idx == su_idx[i]);
            wm_fill_rect(win, content_x, y, 140, 24, sel ? WM_COLOR_ACCENT : 0xFF333333);
            wm_draw_string(win, content_x + 10, y + 6, su_label[i], 0xFFFFFFFF,
                           sel ? WM_COLOR_ACCENT : 0xFF333333);
            y += 28;
            if (y > ch - 40) { y = 44; content_x += 160; }
        }
    } else if (state->tab == 2) {
        /* Dock */
        wm_draw_string(win, content_x, y, "Dock Magnification:", 0xFFFFFFFF, WM_COLOR_WINDOW_BG);
        y += 24;
        bool mag = g_settings.dock_magnification;
        wm_fill_rect(win, content_x, y, 100, 24, mag ? 0xFF4ADE80 : 0xFFEF4444);
        wm_draw_string(win, content_x + 10, y + 6, mag ? "Enabled" : "Disabled", 0xFFFFFFFF, 
                       mag ? 0xFF4ADE80 : 0xFFEF4444);
        
        y += 40;
        wm_draw_string(win, content_x, y, "Dock Transparency:", 0xFFFFFFFF, WM_COLOR_WINDOW_BG);
        y += 24;
        bool trans = g_settings.dock_transparent;
        wm_fill_rect(win, content_x, y, 100, 24, trans ? 0xFF4ADE80 : 0xFFEF4444);
        wm_draw_string(win, content_x + 10, y + 6, trans ? "Enabled" : "Disabled", 0xFFFFFFFF, 
                       trans ? 0xFF4ADE80 : 0xFFEF4444);

        y += 30;
        wm_draw_string(win, content_x, y, "Click to toggle", 0xFF808090, WM_COLOR_WINDOW_BG);
    } else if (state->tab == 3) {
        /* Display */
        wm_draw_string(win, content_x, y, "Screen Resolution:", 0xFFFFFFFF, WM_COLOR_WINDOW_BG);
        y += 30;
        for (int i = 0; i < 8; i++) {
            bool sel = (g_settings.resolution == i);
            wm_fill_rect(win, content_x, y, 140, 28, sel ? WM_COLOR_ACCENT : 0xFF333333);
            wm_draw_string(win, content_x + 10, y + 8, res_names[i], 0xFFFFFFFF, 
                           sel ? WM_COLOR_ACCENT : 0xFF333333);
            y += 34;
        }
        y += 10;
        wm_draw_string(win, content_x, y, "Requires reboot.", 0xFFEF4444, WM_COLOR_WINDOW_BG);
    }

    /* Reboot Dialog Overlay */
    if (state->show_reboot_dialog) {
        wm_fill_rect(win, 0, 0, cw, ch, 0xCC000000);
        int dw = 300, dh = 120;
        int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
        wm_fill_rect(win, dx, dy, dw, dh, 0xFF1E1E30);
        wm_draw_rect(win, dx, dy, dw, dh, WM_COLOR_ACCENT);
        wm_draw_string(win, dx + 20, dy + 20, "Apply changes and reboot now?", 0xFFFFFFFF, 0xFF1E1E30);
        
        /* Yes Button */
        wm_fill_rect(win, dx + 40, dy + 70, 80, 24, 0xFF4ADE80);
        wm_draw_string(win, dx + 65, dy + 76, "Yes", 0xFFFFFFFF, 0xFF4ADE80);
        
        /* No Button */
        wm_fill_rect(win, dx + 180, dy + 70, 80, 24, 0xFFEF4444);
        wm_draw_string(win, dx + 208, dy + 76, "No", 0xFFFFFFFF, 0xFFEF4444);
    }
}

static void settings_on_mouse(Window *win, int lx, int ly, bool left, bool right) {
    (void)right;
    SettingsState *state = (SettingsState *)win->user_data;
    if (!state) return;

    static bool settings_prev_left = false;
    bool is_click = left && !settings_prev_left;
    settings_prev_left = left;

    if (!is_click) return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* Dialog interaction */
    if (state->show_reboot_dialog) {
        int dw = 300, dh = 120;
        int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
        /* Yes button */
        if (lx >= dx + 40 && lx < dx + 120 && ly >= dy + 70 && ly < dy + 94) {
            wm_save_settings();
            
            /* Just reboot - resolution will be applied by bootloader */
            extern void outb(uint16_t port, uint8_t val);
            outb(0x64, 0xFE);
        }
        /* No button */
        if (lx >= dx + 180 && lx < dx + 260 && ly >= dy + 70 && ly < dy + 94) {
            state->show_reboot_dialog = false;
        }
        return;
    }

    int sb_w = 120;
    if (lx < sb_w) {
        /* Sidebar click */
        if (ly >= 10 && ly < 10 + 4 * 36) {
            state->tab = (ly - 10) / 36;
        }
        return;
    }

    /* Content area click */
    int content_x = sb_w + 20;
    int y = 20;

    if (state->tab == 0) {
        /* Appearance - Background Color */
        y += 24;
        for (int i = 0; i < 1; i++) {
            if (lx >= content_x && lx < content_x + 100 && ly >= y && ly < y + 24) {
                g_settings.bg_color = i;
                wm_invalidate_bg();
                wm_save_settings();
                return;
            }
            y += 28;
        }
        y += 20; /* Space */
        y += 24; /* Title */
        for (int i = 0; i < 2; i++) {
            if (lx >= content_x && lx < content_x + 140 && ly >= y && ly < y + 24) {
                g_settings.window_style = i;
                wm_save_settings();
                return;
            }
            y += 28;
        }
    } else if (state->tab == 1) {
        y += 24;
        static const int su_idx[] = {
            -1, 0, 1, 2, 3, 4, 5, 6, 7
        };
        for (int i = 0; i < 9; i++) {
            if (lx >= content_x && lx < content_x + 140 && ly >= y && ly < y + 24) {
                g_settings.startup_app_idx = su_idx[i];
                wm_save_settings();
                return;
            }
            y += 28;
            if (y > wm_client_h(win) - 40) { y = 44; content_x += 160; }
        }
    } else if (state->tab == 2) {
        /* Dock - Magnification */
        y += 24;
        if (lx >= content_x && lx < content_x + 100 && ly >= y && ly < y + 24) {
            g_settings.dock_magnification = !g_settings.dock_magnification;
            wm_save_settings();
            return;
        }
        /* Dock - Transparency */
        y += 40; /* Skip "Dock Transparency:" label */
        y += 24; /* Align with the actual button */
        if (lx >= content_x && lx < content_x + 100 && ly >= y && ly < y + 24) {
            g_settings.dock_transparent = !g_settings.dock_transparent;
            wm_save_settings();
            wm_invalidate_bg();
            return;
        }
    } else if (state->tab == 3) {
        /* Display - Resolution */
        y += 30; /* Skip "Screen Resolution:" title - matches drawing code */
        for (int i = 0; i < 8; i++) {
            if (lx >= content_x && lx < content_x + 140 && ly >= y && ly < y + 28) {
                if (g_settings.resolution != i) {
                    g_settings.resolution = i;
                    state->show_reboot_dialog = true;
                }
                return;
            }
            y += 34;
        }
    }
}

void app_settings_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = 500, h = 350;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Settings", x, y, w, h);
    if (!win) return;

    SettingsState *state = (SettingsState *)win->app_data;
    win->user_data = state;
    for (int i = 0; i < (int)sizeof(SettingsState); i++) ((char *)state)[i] = 0;
    state->tab = 0;

    win->on_draw = settings_on_draw;
    win->on_mouse = settings_on_mouse;
}

/* SNAKE GAME APPLICATION */
#define SNAKE_GRID_SIZE 20
#define SNAKE_CELL_SIZE 16
#define SNAKE_MAX_LENGTH (SNAKE_GRID_SIZE * SNAKE_GRID_SIZE)

typedef struct {
    int x, y;
} SnakePos;

typedef struct {
    SnakePos body[SNAKE_MAX_LENGTH];
    int length;
    int dx, dy;
    SnakePos food;
    int score;
    bool game_over;
    uint32_t last_move_ticks;
    uint32_t rand_seed;
    int difficulty; /* 0=Easy, 1=Medium, 2=Hard */
    bool first_game;
} SnakeState;

static uint32_t snake_rand(SnakeState *state) {
    state->rand_seed = state->rand_seed * 1103515245 + 12345;
    return (state->rand_seed / 65536) % 32768;
}

static void snake_place_food(SnakeState *state) {
    state->food.x = snake_rand(state) % SNAKE_GRID_SIZE;
    state->food.y = snake_rand(state) % SNAKE_GRID_SIZE;
}

static void snake_reset(SnakeState *state) {
    state->length = 3;
    state->body[0].x = SNAKE_GRID_SIZE / 2;
    state->body[0].y = SNAKE_GRID_SIZE / 2;
    state->body[1].x = state->body[0].x - 1;
    state->body[1].y = state->body[0].y;
    state->body[2].x = state->body[1].x - 1;
    state->body[2].y = state->body[1].y;
    state->dx = 1;
    state->dy = 0;
    state->score = 0;
    state->game_over = false;
    state->first_game = false;
    state->last_move_ticks = get_ticks();
    snake_place_food(state);
}

static void snake_on_key(Window *win, uint16_t key) {
    SnakeState *state = (SnakeState *)win->user_data;
    if (!state) return;

    if (state->game_over) {
        if (key == 'r' || key == 'R' || (key & 0xFF) == 13) { /* R or Enter */
            snake_reset(state);
        }
        if (key == '1') { state->difficulty = 0; snake_reset(state); }
        if (key == '2') { state->difficulty = 1; snake_reset(state); }
        if (key == '3') { state->difficulty = 2; snake_reset(state); }
        return;
    }

    /* Arrow keys or WASD */
    if ((key == 0x48 || key == 'w' || key == 'W') && state->dy != 1) { state->dx = 0; state->dy = -1; }
    else if ((key == 0x50 || key == 's' || key == 'S') && state->dy != -1) { state->dx = 0; state->dy = 1; }
    else if ((key == 0x4B || key == 'a' || key == 'A') && state->dx != 1) { state->dx = -1; state->dy = 0; }
    else if ((key == 0x4D || key == 'd' || key == 'D') && state->dx != -1) { state->dx = 1; state->dy = 0; }
}

static void snake_on_draw(Window *win) {
    SnakeState *state = (SnakeState *)win->user_data;
    if (!state) return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);
    uint32_t now = get_ticks();

    /* Game logic update */
    int move_delay = (state->difficulty == 2) ? 4 : 8;
    if (!state->game_over && (now - state->last_move_ticks > (uint32_t)move_delay)) {
        state->last_move_ticks = now;

        /* Move head */
        int next_x = state->body[0].x + state->dx;
        int next_y = state->body[0].y + state->dy;

        /* Collision with walls */
        if (next_x < 0 || next_x >= SNAKE_GRID_SIZE || next_y < 0 || next_y >= SNAKE_GRID_SIZE) {
            if (state->difficulty == 0) {
                /* Easy: Wrap around */
                if (next_x < 0) next_x = SNAKE_GRID_SIZE - 1;
                else if (next_x >= SNAKE_GRID_SIZE) next_x = 0;
                if (next_y < 0) next_y = SNAKE_GRID_SIZE - 1;
                else if (next_y >= SNAKE_GRID_SIZE) next_y = 0;
            } else {
                state->game_over = true;
            }
        }
        
        if (!state->game_over) {
            /* Collision with self */
            for (int i = 0; i < state->length; i++) {
                if (state->body[i].x == next_x && state->body[i].y == next_y) {
                    state->game_over = true;
                    break;
                }
            }
        }

        if (!state->game_over) {
            /* Check food */
            bool eating = (next_x == state->food.x && next_y == state->food.y);

            /* Shift body */
            for (int i = state->length; i > 0; i--) {
                state->body[i] = state->body[i - 1];
            }
            state->body[0].x = next_x;
            state->body[0].y = next_y;

            if (eating) {
                if (state->length < SNAKE_MAX_LENGTH - 1) state->length++;
                state->score += 10;
                snake_place_food(state);
            }
        }
    }

    /* Background */
    wm_fill_rect(win, 0, 0, cw, ch, 0xFF0A0A16);

    /* Grid area border */
    int grid_px = SNAKE_GRID_SIZE * SNAKE_CELL_SIZE;
    int off_x = (cw - grid_px) / 2;
    int off_y = (ch - grid_px) / 2;
    wm_draw_rect(win, off_x - 1, off_y - 1, grid_px + 2, grid_px + 2, 0xFF303050);

    /* Draw food */
    wm_fill_rect(win, off_x + state->food.x * SNAKE_CELL_SIZE + 2, 
                 off_y + state->food.y * SNAKE_CELL_SIZE + 2, 
                 SNAKE_CELL_SIZE - 4, SNAKE_CELL_SIZE - 4, 0xFFEF4444);

    /* Draw snake */
    for (int i = 0; i < state->length; i++) {
        uint32_t color = (i == 0) ? 0xFF4ADE80 : 0xFF22C55E;
        wm_fill_rect(win, off_x + state->body[i].x * SNAKE_CELL_SIZE + 1, 
                     off_y + state->body[i].y * SNAKE_CELL_SIZE + 1, 
                     SNAKE_CELL_SIZE - 2, SNAKE_CELL_SIZE - 2, color);
    }

    /* UI text */
    char score_buf[32];
    str_copy(score_buf, "Score: ", 32);
    int_to_str(state->score, score_buf + 7);
    wm_draw_string(win, 10, 5, score_buf, 0xFFFFFFFF, 0x00000000);

    if (state->game_over) {
        wm_fill_rect(win, cw / 4, ch / 2 - 60, cw / 2, 120, 0xCC1A1A2E);
        if (state->first_game) {
            wm_draw_string(win, cw / 2 - 64, ch / 2 - 50, "SNAKE REBORN", 0xFF4ADE80, 0);
            wm_draw_string(win, cw / 2 - 76, ch / 2 - 30, "Choose Difficulty", 0xFFFFFFFF, 0);
        } else {
            wm_draw_string(win, cw / 2 - 40, ch / 2 - 50, "GAME OVER", 0xFFEF4444, 0);
            wm_draw_string(win, cw / 2 - 60, ch / 2 - 30, "Press R to Restart", 0xFFFFFFFF, 0);
        }
        
        const char *modes[3] = {"[1] Easy (Wrap)", "[2] Medium (Walls)", "[3] Hard (Fast)"};
        wm_draw_string(win, cw / 2 - 50, ch / 2, "Select Mode:", 0xFF808090, 0);
        for(int m=0; m<3; m++) {
            uint32_t col = (state->difficulty == m) ? 0xFF4ADE80 : 0xFFE0E0F0;
            wm_draw_string(win, cw / 2 - 70, ch / 2 + 15 + m*14, modes[m], col, 0);
        }
    }
}

void app_snake_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = SNAKE_GRID_SIZE * SNAKE_CELL_SIZE + 40;
    int h = SNAKE_GRID_SIZE * SNAKE_CELL_SIZE + 60;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Snake", x, y, w, h);
    if (!win) return;

    SnakeState *state = (SnakeState *)win->app_data;
    win->user_data = state;

    /* Initialize random seed */
    uint8_t h_rtc, m_rtc, s_rtc;
    sys_get_time(&h_rtc, &m_rtc, &s_rtc);
    state->rand_seed = get_ticks() + s_rtc;

    state->first_game = true;
    state->game_over = true;
    state->score = 0;
    state->difficulty = 0;

    win->on_draw = snake_on_draw;
    win->on_key = snake_on_key;
}