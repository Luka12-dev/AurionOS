/*
 * Terminal Application for Aurion OS
 * Provides a command-line interface within the desktop GUI
*/

#include <stdint.h>
#include <stdbool.h>
#include "window_manager.h"

/* External kernel/shell functions */
extern void c_puts(const char *s);
extern void set_terminal_hook(void (*hook)(char));
extern int cmd_dispatch(const char *cmd);
extern void cmd_init_silent(void);
extern char current_dir[256];
extern int sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);
extern uint32_t get_ticks(void);
extern char keyboard_remap(char c);

/* Terminal state */
#define TERM_COLS 100
#define TERM_ROWS 200
#define TERM_HISTORY_SIZE 10
#define TERM_MAX_INPUT 256

typedef struct
{
    char buffer[TERM_ROWS][TERM_COLS + 1];
    uint32_t fg[TERM_ROWS][TERM_COLS];
    int cursor_row;
    int cursor_col;
    int scroll_offset;
    int total_lines;

    /* Input line */
    char input[TERM_MAX_INPUT];
    int input_pos;
    bool input_active;

    /* Command history */
    char history[TERM_HISTORY_SIZE][TERM_MAX_INPUT];
    int history_count;
    int history_pos;

    /* Output capture */
    char output_buf[4096];
    int output_len;

    /* Selection */
    int sel_start_row, sel_start_col;
    int sel_end_row, sel_end_col;
    bool selecting;
} TerminalState;

static TerminalState term_state;
static TerminalState *active_term_state = NULL;

/* Redirect puts output to terminal buffer */
static void term_putchar(TerminalState *ts, char c)
{
    if (c == '\n' || c == '\r')
    {
        ts->cursor_col = 0;
        ts->cursor_row++;
        if (ts->cursor_row >= TERM_ROWS)
        {
            /* Scroll up */
            for (int r = 0; r < TERM_ROWS - 1; r++)
            {
                for (int col = 0; col <= TERM_COLS; col++)
                {
                    ts->buffer[r][col] = ts->buffer[r + 1][col];
                    ts->fg[r][col] = ts->fg[r + 1][col];
                }
            }
            for (int col = 0; col <= TERM_COLS; col++)
            {
                ts->buffer[TERM_ROWS - 1][col] = 0;
                ts->fg[TERM_ROWS - 1][col] = WM_COLOR_TEXT;
            }
            ts->cursor_row = TERM_ROWS - 1;
        }
        if (ts->cursor_row >= ts->total_lines)
        {
            ts->total_lines = ts->cursor_row + 1;
        }
        return;
    }
    if (c == '\b')
    {
        if (ts->cursor_col > 0)
            ts->cursor_col--;
        return;
    }
    if (c == '\t')
    {
        ts->cursor_col = (ts->cursor_col + 4) & ~3;
        if (ts->cursor_col >= TERM_COLS)
        {
            ts->cursor_col = 0;
            term_putchar(ts, '\n');
        }
        return;
    }

    if (ts->cursor_col < TERM_COLS)
    {
        ts->buffer[ts->cursor_row][ts->cursor_col] = c;
        ts->fg[ts->cursor_row][ts->cursor_col] = WM_COLOR_TEXT;
        ts->cursor_col++;
    }
    if (ts->cursor_col >= TERM_COLS)
    {
        ts->cursor_col = 0;
        term_putchar(ts, '\n');
    }
}

static void term_puts(TerminalState *ts, const char *s)
{
    while (*s)
    {
        term_putchar(ts, *s);
        s++;
    }
}

static void term_puts_color(TerminalState *ts, const char *s, uint32_t color)
{
    while (*s)
    {
        if (*s == '\n' || *s == '\r')
        {
            term_putchar(ts, *s);
        }
        else if (ts->cursor_col < TERM_COLS)
        {
            ts->buffer[ts->cursor_row][ts->cursor_col] = *s;
            ts->fg[ts->cursor_row][ts->cursor_col] = color;
            ts->cursor_col++;
            if (ts->cursor_col >= TERM_COLS)
            {
                ts->cursor_col = 0;
                term_putchar(ts, '\n');
            }
        }
        s++;
    }
}

static void term_show_prompt(TerminalState *ts)
{
    term_puts_color(ts, current_dir, 0xFF6366F1); /* Indigo prompt */
    term_puts_color(ts, "> ", 0xFF818CF8);
}

static int term_strlen(const char *s)
{
    int l = 0;
    while (s[l])
        l++;
    return l;
}

static void term_strupr(char *s)
{
    while (*s)
    {
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
        s++;
    }
}

static int term_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a - *b;
}

static void term_init(TerminalState *ts)
{
    for (int r = 0; r < TERM_ROWS; r++)
    {
        for (int c = 0; c <= TERM_COLS; c++)
        {
            ts->buffer[r][c] = 0;
            ts->fg[r][c] = WM_COLOR_TEXT;
        }
    }
    ts->cursor_row = 0;
    ts->cursor_col = 0;
    ts->scroll_offset = 0;
    ts->total_lines = 1;
    ts->input_pos = 0;
    ts->input[0] = 0;
    ts->input_active = true;
    ts->history_count = 0;
    ts->history_pos = 0;
    ts->output_len = 0;

    /* Welcome message */
    term_puts_color(ts, "Aurion OS Terminal v1.0 Beta\n", WM_COLOR_ACCENT);
    term_puts_color(ts, "Type HELP for available commands\n", WM_COLOR_TEXT_DIM);
    term_puts_color(ts, "Type DOSMODE to switch to classic CLI\n\n", WM_COLOR_TEXT_DIM);
    term_show_prompt(ts);
}

/* Global hook callback for c_puts redirection */
static void term_console_hook(char c)
{
    if (active_term_state)
    {
        term_putchar(active_term_state, c);
    }
}

/* Execute a command and capture output */
static void term_execute(TerminalState *ts, const char *cmd)
{
    /* Add to history */
    if (ts->history_count < TERM_HISTORY_SIZE)
    {
        int i = 0;
        while (cmd[i] && i < TERM_MAX_INPUT - 1)
        {
            ts->history[ts->history_count][i] = cmd[i];
            i++;
        }
        ts->history[ts->history_count][i] = 0;
        ts->history_count++;
    }
    ts->history_pos = ts->history_count;

    /* Make uppercase copy for dispatch */
    char upper_cmd[TERM_MAX_INPUT];
    int ci = 0;
    while (cmd[ci] && ci < TERM_MAX_INPUT - 1)
    {
        upper_cmd[ci] = cmd[ci];
        ci++;
    }
    upper_cmd[ci] = 0;
    term_strupr(upper_cmd);

    /* Check for DOSMODE */
    if (term_strcmp(upper_cmd, "DOSMODE") == 0)
    {
        term_puts(ts, "Switching to DOS mode...\n");
        desktop_exit_reason = 0; /* Tell desktop to exit to DOS */
        return;
    }

    /* Check for EXIT */
    if (term_strcmp(upper_cmd, "EXIT") == 0)
    {
        return; /* Window close handled by WM */
    }

    /* Check for CLS/CLEAR - full screen clear (closes all windows behavior kept,
       but in terminal context just clears the terminal buffer) */
    if (term_strcmp(upper_cmd, "CLS") == 0 || term_strcmp(upper_cmd, "CLEAR") == 0)
    {
        /* Clear the buffer */
        for (int r = 0; r < TERM_ROWS; r++)
        {
            for (int c = 0; c <= TERM_COLS; c++)
            {
                ts->buffer[r][c] = 0;
                ts->fg[r][c] = WM_COLOR_TEXT;
            }
        }
        ts->cursor_row = 0;
        ts->cursor_col = 0;
        ts->scroll_offset = 0;
        ts->total_lines = 1;
        /* NOTE: do NOT show prompt here - the caller (terminal_on_key Enter handler)
           will call term_show_prompt after term_execute returns. That way there is
           exactly ONE prompt, not two. */
        return;
    }

    /* Check for CLS-TERMINAL / CLEAR-TERMINAL - clear only this terminal window */
    if (term_strcmp(upper_cmd, "CLS-TERMINAL") == 0 ||
        term_strcmp(upper_cmd, "CLEAR-TERMINAL") == 0)
    {
        for (int r = 0; r < TERM_ROWS; r++)
        {
            for (int c = 0; c <= TERM_COLS; c++)
            {
                ts->buffer[r][c] = 0;
                ts->fg[r][c] = WM_COLOR_TEXT;
            }
        }
        ts->cursor_row = 0;
        ts->cursor_col = 0;
        ts->scroll_offset = 0;
        ts->total_lines = 1;
        /* Same: caller shows prompt */
        return;
    }

    /* Set global hook to capture command output */
    active_term_state = ts;
    set_terminal_hook(term_console_hook);

    /* Dispatch command - output will be redirected via hook */
    int result = cmd_dispatch(upper_cmd);

    /* Restore console output */
    set_terminal_hook(NULL);
    active_term_state = NULL;

    if (result == -255)
    {
        term_puts_color(ts, "Bad command or file name\n", 0x00E04050);
    }
}

/* Forward declaration so terminal_on_draw can call it */
static void terminal_draw_ctx(Window *win);

/* Terminal draw callback */
static void terminal_on_draw(Window *win)
{
    TerminalState *ts = (TerminalState *)win->user_data;
    if (!ts)
        return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* Background */
    wm_fill_rect(win, 0, 0, cw, ch, 0xFF12121E);

    /* Calculate visible rows */
    int visible_rows = ch / 14;
    if (visible_rows > TERM_ROWS)
        visible_rows = TERM_ROWS;

    /* Bottom row that would be shown with no scrollback */
    int bottom_row = ts->cursor_row;
    /* Apply scroll offset (PgUp increases offset, PgDn decreases) */
    int view_bottom = bottom_row - ts->scroll_offset;
    int start_row = view_bottom - visible_rows + 1;
    if (start_row < 0)
        start_row = 0;

    /* Draw text */
    for (int r = start_row; r <= view_bottom && r < TERM_ROWS; r++)
    {
        int screen_y = (r - start_row) * 14 + 4;
        if (screen_y + 14 > ch)
            break;

        for (int c = 0; c < TERM_COLS && ts->buffer[r][c]; c++)
        {
            int screen_x = c * 8 + 4;
            if (screen_x + 8 > cw)
                break;

            /* Check if this character is selected */
            bool is_sel = false;
            if (ts->sel_start_row != -1) {
                int s_r = ts->sel_start_row, s_c = ts->sel_start_col;
                int e_r = ts->sel_end_row,   e_c = ts->sel_end_col;
                /* Normalize selection order */
                if (s_r > e_r || (s_r == e_r && s_c > e_c)) {
                    int tmp_r = s_r; s_r = e_r; e_r = tmp_r;
                    int tmp_c = s_c; s_c = e_c; e_c = tmp_c;
                }
                if (r > s_r && r < e_r) is_sel = true;
                else if (r == s_r && r == e_r) { if (c >= s_c && c < e_c) is_sel = true; }
                else if (r == s_r) { if (c >= s_c) is_sel = true; }
                else if (r == e_r) { if (c < e_c) is_sel = true; }
            }

            uint32_t bg = is_sel ? 0xFF4A6CF7 : 0xFF12121E;
            uint32_t fg = is_sel ? 0xFFFFFFFF : ts->fg[r][c];

            if (is_sel) wm_fill_rect(win, screen_x, screen_y, 8, 14, bg);
            wm_draw_char(win, screen_x, screen_y, (uint8_t)ts->buffer[r][c], fg, bg);
        }
    }

    /* Scrollback indicator */
    if (ts->scroll_offset > 0)
    {
        wm_draw_string(win, cw - 80, 4, (const uint8_t *)"[SCROLL]",
                       0xFF818CF8, 0xFF12121E);
    }

    /* Blinking cursor - only show when not scrolled back */
    if (ts->input_active && ts->scroll_offset == 0 && (get_ticks() % 36) < 18)
    {
        int cursor_screen_row = ts->cursor_row - start_row;
        if (cursor_screen_row >= 0 && cursor_screen_row < visible_rows)
        {
            int cx = ts->cursor_col * 8 + 4;
            int cy = cursor_screen_row * 14 + 4;
            wm_fill_rect(win, cx, cy, 8, 12, WM_COLOR_ACCENT);
        }
    }

    /* Right-click context menu drawn on top of everything */
    terminal_draw_ctx(win);
}

/* Terminal key callback */
static void terminal_on_key(Window *win, uint16_t key)
{
    TerminalState *ts = (TerminalState *)win->user_data;
    if (!ts || !ts->input_active)
        return;

    uint8_t ascii = key & 0xFF;
    uint8_t scan = (key >> 8) & 0xFF;

    /* Enter - execute command */
    if (ascii == 13 || ascii == 10)
    {
        ts->scroll_offset = 0;
        term_putchar(ts, '\n');
        if (ts->input_pos > 0)
        {
            ts->input[ts->input_pos] = 0;
            term_execute(ts, ts->input);
        }
        ts->input_pos = 0;
        ts->input[0] = 0;
        if (desktop_exit_reason < 0)
        {
            term_show_prompt(ts);
        }
        return;
    }

    /* Backspace */
    if (ascii == 8 || scan == 0x0E)
    {
        if (ts->input_pos > 0)
        {
            ts->input_pos--;
            ts->input[ts->input_pos] = 0;
            if (ts->cursor_col > 0)
            {
                ts->cursor_col--;
                ts->buffer[ts->cursor_row][ts->cursor_col] = ' ';
            }
        }
        return;
    }

    /* Up arrow - history */
    if (scan == 0x48)
    {
        if (ts->history_count > 0 && ts->history_pos > 0)
        {
            ts->history_pos--;
            /* Clear current input from screen */
            while (ts->input_pos > 0)
            {
                ts->input_pos--;
                if (ts->cursor_col > 0)
                {
                    ts->cursor_col--;
                    ts->buffer[ts->cursor_row][ts->cursor_col] = ' ';
                }
            }
            /* Copy from history */
            int i = 0;
            while (ts->history[ts->history_pos][i] && i < TERM_MAX_INPUT - 1)
            {
                ts->input[i] = ts->history[ts->history_pos][i];
                term_putchar(ts, ts->input[i]);
                i++;
            }
            ts->input[i] = 0;
            ts->input_pos = i;
        }
        return;
    }

    /* Down arrow - history */
    if (scan == 0x50)
    {
        if (ts->history_pos < ts->history_count - 1)
        {
            ts->history_pos++;
            while (ts->input_pos > 0)
            {
                ts->input_pos--;
                if (ts->cursor_col > 0)
                {
                    ts->cursor_col--;
                    ts->buffer[ts->cursor_row][ts->cursor_col] = ' ';
                }
            }
            int i = 0;
            while (ts->history[ts->history_pos][i] && i < TERM_MAX_INPUT - 1)
            {
                ts->input[i] = ts->history[ts->history_pos][i];
                term_putchar(ts, ts->input[i]);
                i++;
            }
            ts->input[i] = 0;
            ts->input_pos = i;
        }
        return;
    }

    /* PgUp - scroll back through terminal history */
    if (scan == 0x49)
    {
        int max_scroll = ts->cursor_row;
        if (ts->scroll_offset < max_scroll)
        {
            ts->scroll_offset += 5;
            if (ts->scroll_offset > max_scroll)
                ts->scroll_offset = max_scroll;
        }
        return;
    }

    /* PgDn - scroll forward toward current output */
    if (scan == 0x51)
    {
        if (ts->scroll_offset > 0)
        {
            ts->scroll_offset -= 5;
            if (ts->scroll_offset < 0)
                ts->scroll_offset = 0;
        }
        return;
    }

    /* Any typing resets scroll to bottom */
    ts->scroll_offset = 0;

    /* Printable characters */
    if (ascii >= 32 && ascii < 127 && ts->input_pos < TERM_MAX_INPUT - 1)
    {
        char mapped = keyboard_remap((char)ascii);
        ts->input[ts->input_pos] = mapped;
        ts->input_pos++;
        ts->input[ts->input_pos] = 0;
        term_putchar(ts, mapped);
    }
}

/* Terminal right-click context menu state */
typedef struct {
    bool open;
    int x, y;          /* screen position of menu */
    int hover;         /* -1 = none, 0 = Copy, 1 = Paste */
} TermCtxMenu;

static TermCtxMenu term_ctx = {false, 0, 0, -1};

/* A tiny OS-level clipboard - shared between terminal and notepad */
char os_clipboard[512] = {0};
int  os_clipboard_len   = 0;

static void terminal_on_mouse(Window *win, int lx, int ly, bool left, bool right)
{
    TerminalState *ts = (TerminalState *)win->user_data;
    if (!ts) return;

    static bool prev_left  = false;
    static bool prev_right = false;
    bool lclick  = left  && !prev_left;
    bool rclick  = right && !prev_right;
    prev_left  = left;
    prev_right = right;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);
    int visible_rows = ch / 14;
    int bottom_row = ts->cursor_row;
    int view_bottom = bottom_row - ts->scroll_offset;
    int start_row = view_bottom - visible_rows + 1;
    if (start_row < 0) start_row = 0;

    /* Right-click opens the context menu */
    if (rclick) {
        term_ctx.open  = true;
        term_ctx.x     = lx;
        term_ctx.y     = ly;
        term_ctx.hover = -1;
        return;
    }

    if (term_ctx.open) {
        int menu_w = 120, row_h = 24, pad = 2;
        int menu_h = row_h * 2 + pad * 2;
        /* Update hover */
        term_ctx.hover = -1;
        if (lx >= term_ctx.x && lx < term_ctx.x + menu_w &&
            ly >= term_ctx.y && ly < term_ctx.y + menu_h) {
            term_ctx.hover = (ly - term_ctx.y - pad) / row_h;
        }
        if (lclick) {
            if (term_ctx.hover == 0) {
                /* Copy Selection */
                if (ts->sel_start_row != -1) {
                    int s_r = ts->sel_start_row, s_c = ts->sel_start_col;
                    int e_r = ts->sel_end_row,   e_c = ts->sel_end_col;
                    if (s_r > e_r || (s_r == e_r && s_c > e_c)) {
                        int tmp_r = s_r; s_r = e_r; e_r = tmp_r;
                        int tmp_c = s_c; s_c = e_c; e_c = tmp_c;
                    }
                    int out_idx = 0;
                    for (int r = s_r; r <= e_r && out_idx < 510; r++) {
                        int start = (r == s_r) ? s_c : 0;
                        int end   = (r == e_r) ? e_c : TERM_COLS;
                        for (int c = start; c < end && out_idx < 510; c++) {
                            char ch_c = ts->buffer[r][c];
                            if (ch_c == 0) break;
                            os_clipboard[out_idx++] = ch_c;
                        }
                        if (r < e_r) os_clipboard[out_idx++] = '\n';
                    }
                    os_clipboard[out_idx] = 0;
                    os_clipboard_len = out_idx;
                } else {
                    /* Fallback: copy input line */
                    int n = ts->input_pos;
                    if (n > 511) n = 511;
                    for (int i = 0; i < n; i++) os_clipboard[i] = ts->input[i];
                    os_clipboard[n] = 0;
                    os_clipboard_len = n;
                }
            } else if (term_ctx.hover == 1) {
                /* Paste - insert clipboard into input */
                for (int i = 0; i < os_clipboard_len && ts->input_pos < TERM_MAX_INPUT - 1; i++) {
                    char c = os_clipboard[i];
                    if (c == '\n' || c == '\r') continue;
                    ts->input[ts->input_pos++] = c;
                    ts->input[ts->input_pos]   = 0;
                    term_putchar(ts, c);
                }
            }
            term_ctx.open = false;
        }
        /* Click outside menu closes it */
        if (lclick && (lx < term_ctx.x || lx >= term_ctx.x + 120 ||
                       ly < term_ctx.y || ly >= term_ctx.y + menu_h)) {
            term_ctx.open = false;
        }
        return;
    }

    /* Text Selection Logic */
    if (left) {
        int r = start_row + (ly - 4) / 14;
        int c = (lx - 4) / 8;
        if (r < 0) r = 0; if (r >= TERM_ROWS) r = TERM_ROWS - 1;
        if (c < 0) c = 0; if (c >= TERM_COLS) c = TERM_COLS - 1;

        if (lclick) {
            ts->sel_start_row = r;
            ts->sel_start_col = c;
            ts->sel_end_row   = r;
            ts->sel_end_col   = c;
            ts->selecting     = true;
        } else if (ts->selecting) {
            ts->sel_end_row = r;
            ts->sel_end_col = c;
        }
    } else {
        if (ts->selecting) {
            /* If it was just a click with no movement, clear selection */
            if (ts->sel_start_row == ts->sel_end_row && 
                ts->sel_start_col == ts->sel_end_col) {
                ts->sel_start_row = -1;
            }
            ts->selecting = false;
        }
    }
}

/* Draw the terminal right-click menu - called from terminal_on_draw */
static void terminal_draw_ctx(Window *win)
{
    if (!term_ctx.open) return;

    int menu_w = 120, row_h = 24, pad = 2;
    int menu_h = row_h * 2 + pad * 2;
    int mx = term_ctx.x, my = term_ctx.y;

    const char *labels[2] = {"Copy", "Paste"};

    wm_fill_rect(win, mx, my, menu_w, menu_h, 0xFF1E1E30);
    wm_fill_rect(win, mx, my, menu_w, 1, 0xFF3A3A54);
    wm_fill_rect(win, mx, my + menu_h - 1, menu_w, 1, 0xFF3A3A54);
    wm_fill_rect(win, mx, my, 1, menu_h, 0xFF3A3A54);
    wm_fill_rect(win, mx + menu_w - 1, my, 1, menu_h, 0xFF3A3A54);

    for (int i = 0; i < 2; i++) {
        int iy = my + pad + i * row_h;
        bool hov = (i == term_ctx.hover);
        uint32_t bg = hov ? WM_COLOR_ACCENT : 0xFF1E1E30;
        uint32_t fg = hov ? WM_COLOR_WHITE  : 0xFFD0D0E0;
        wm_fill_rect(win, mx + 1, iy, menu_w - 2, row_h, bg);
        wm_draw_string(win, mx + 8, iy + 6, labels[i], fg, bg);
        if (i == 0 && !hov)
            wm_fill_rect(win, mx + 4, iy + row_h - 1, menu_w - 8, 1, 0xFF2A2A42);
    }
}

/* Terminal close callback */
static void terminal_on_close(Window *win)
{
    (void)win;
}

static int fs_initialized = 0;

/* Create a terminal window */
void terminal_create(void)
{
    /* Ensure filesystem is initialized (GUI mode bypasses shell_main) */
    if (!fs_initialized)
    {
        cmd_init_silent();
        fs_initialized = 1;
    }

    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();

    int w = sw * 2 / 3;
    int h = sh * 2 / 3;
    if (w < 400)
        w = 400;
    if (h < 300)
        h = 300;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    c_puts("[TERM] creating window...\n");
    Window *win = wm_create_window("Terminal", x, y, w, h);
    if (!win)
    {
        c_puts("[TERM] FAILED to create window!\n");
        return;
    }
    c_puts("[TERM] window created\n");

    win->min_w = 320;
    win->min_h = 200;

    /* TerminalState is ~32KB - far too large for app_data[4096].
     * Use the static term_state instead. */
    TerminalState *ts = &term_state;
    win->user_data = ts;

    /* Zero out the state */
    for (int i = 0; i < (int)sizeof(TerminalState); i++)
    {
        ((char *)ts)[i] = 0;
    }

    c_puts("[TERM] initializing terminal state...\n");
    term_init(ts);
    c_puts("[TERM] terminal state initialized\n");

    win->on_draw  = terminal_on_draw;
    win->on_key   = terminal_on_key;
    win->on_mouse = terminal_on_mouse;
    win->on_close = terminal_on_close;
}
