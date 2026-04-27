#include "window_manager.h"
#include <stdint.h>
#include <stdbool.h>

/* External functions from kernel/fs */
extern uint32_t get_ticks(void);
extern int save_file_content(const char *filename, const char *data, int len);
extern int load_file_content(const char *filename, char *buffer, int max_len);
extern int cmd_useradd(const char *args);
extern int cmd_mkdir(const char *args);
extern int cmd_passwd(const char *args);
extern int str_len(const char *s);
extern void str_copy(char *dst, const char *src, int max);
extern void c_puts(const char *s);
extern int fs_save_to_disk(void);
extern bool ata_is_available(void);

/* External WM functions */
extern void wm_set_installer_mode(bool enabled);
extern void wm_draw_desktop_bg(void);
extern int wm_get_screen_w(void);
extern int wm_get_screen_h(void);
extern void wm_fill_rect(Window *w, int x, int y, int rw, int rh, uint32_t c);
extern void wm_draw_rect(Window *w, int x, int y, int rw, int rh, uint32_t c);
extern void wm_draw_string(Window *w, int x, int y, const char *s, uint32_t fg, uint32_t bg);
extern void wm_fill_rect_blend(Window *w, int x, int y, int rw, int rh, uint32_t c, uint8_t a);

typedef enum
{
    STEP_WELCOME = 0,
    STEP_KEYBOARD = 1,
    STEP_CREDENTIALS = 2,
    STEP_APPS = 3,
    STEP_INSTALLING = 4,
    STEP_FINISHED = 5,
    STEP_COUNT = 6
} InstallerStep;

typedef struct
{
    int x, y, w, h;
} Rect;

typedef struct
{
    InstallerStep step;
    char username[32];
    char password[32];
    char password_confirm[32];
    int active_field; /* 0=username, 1=password, 2=confirm */
    bool app_selected[5];
    int progress;
    uint32_t last_tick;
    uint32_t step_enter_tick;
    bool prev_left;
    char test_input[64];

    /* Hover tracking */
    int hover_btn;    /* -1=none, 0=back, 1=next/install, 2=continue */
    int hover_app;    /* -1=none, 0..4=app index */
    int hover_layout; /* -1=none, 0=english, 1=serbian */

    /* Validation */
    char error_msg[64];
    uint32_t error_tick;

    /* Cursor blink */
    uint32_t cursor_tick;
    bool cursor_visible;

    /* Installation result tracking */
    bool install_ok;
    bool disk_available;
    int install_substep;
} InstallerState;

extern int keyboard_layout;
extern char keyboard_remap(char c);

/* Color Palette */
/* Dark Glass Mode Color Palette */
#define COL_BG 0xFF0A0A0A
#define COL_GLASS_BG 0xFF121212
#define COL_GLASS_ALPHA 180
#define COL_HEADER_BG 0xFF161616
#define COL_ACCENT 0xFF22C55E /* Emerald Green */
#define COL_ACCENT_HOVER 0xFF4ADE80
#define COL_ACCENT_DIM 0xFF14532D
#define COL_DANGER 0xFFEF4444
#define COL_WARNING 0xFFF59E0B
#define COL_TEXT_PRIMARY 0xFFF8FAFC
#define COL_TEXT_SECONDARY 0xFF94A3B8
#define COL_TEXT_MUTED 0xFF64748B
#define COL_TEXT_DARK 0xFF020617
#define COL_INPUT_BG 0xFF1E293B
#define COL_INPUT_BORDER 0xFF334155
#define COL_INPUT_ACTIVE 0xFF22C55E
#define COL_BTN_SECONDARY 0xFF334155
#define COL_BTN_SEC_HOVER 0xFF475569
#define COL_BTN_SEC_TEXT 0xFFF1F5F9
#define COL_CARD_BG 0xFF1E293B
#define COL_CARD_BORDER 0xFF334155
#define COL_PROGRESS_BG 0xFF334155
#define COL_CHECKBOX_BG 0xFF1E293B
#define COL_LINK_BLUE 0xFF60A5FA
#define COL_SUCCESS_GREEN 0xFF22C55E
#define COL_CONTAINER_GLOW 0xFF22C55E

static const char *app_names[] = {
    "Terminal", "Notepad", "Paint", "Calculator", "Browser"};
static const char *app_descriptions[] = {
    "Command-line interface",
    "Text editor",
    "Drawing application",
    "Math utilities",
    "Web browser"};
static const char *internal_names[] = {
    "Terminal", "Notepad", "Paint", "Calculator", "Blaze"};

static const char *install_messages[] = {
    "Copying AurionOS files",
    "Expanding AurionOS files",
    "Installing features",
    "Installing updates",
    "Completing installation"};
#define INSTALL_MSG_COUNT 5

/* Utility */
static bool rect_contains(Rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void set_error(InstallerState *s, const char *msg)
{
    str_copy(s->error_msg, msg, 63);
    s->error_tick = get_ticks();
}

static bool has_error(InstallerState *s)
{
    if (s->error_msg[0] == 0)
        return false;
    if (get_ticks() - s->error_tick > 300)
    {
        s->error_msg[0] = 0;
        return false;
    }
    return true;
}

static bool is_valid_username_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static bool strings_equal(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] && b[i]; i++)
    {
        if (a[i] != b[i])
            return false;
    }
    return a[i] == b[i];
}

/* Drawing helpers */
static void draw_win7_btn(Window *win, Rect r, const char *label,
                          uint32_t bg, uint32_t fg, bool hovered)
{
    uint32_t color = hovered ? (bg == COL_ACCENT ? COL_ACCENT_HOVER : COL_BTN_SEC_HOVER) : bg;

    /* Rounded button body */
    wm_draw_rounded_rect(win, r.x, r.y, r.w, r.h, color, 6);

    /* Top highlight for depth */
    if (hovered)
    {
        wm_fill_rect(win, r.x + 2, r.y + 1, r.w - 4, 1, 0xFFFFFFFF);
    }

    /* Subtle border */
    wm_draw_rect(win, r.x, r.y, r.w, r.h, hovered ? 0xFF888888 : 0xFF555555);

    int text_len = str_len(label);
    int tx = r.x + (r.w - text_len * 8) / 2;
    int ty = r.y + (r.h - 8) / 2;
    wm_draw_string(win, tx, ty, label, fg, color);
}

/* Forward declaration */
static void draw_check(Window *win, int x, int y, uint32_t color);

/* Step Progress Indicator */
static void draw_step_dots(Window *win, int cx, int cy, int c_w, InstallerStep current)
{
    int dot_sz = 8;
    int gap = 50;
    int total = (STEP_COUNT - 1) * gap;
    int sx = cx + (c_w - total) / 2;
    int dy = cy + 18;

    for (int i = 0; i < STEP_COUNT; i++)
    {
        int dx = sx + i * gap;
        if (i < (int)current)
        {
            /* Completed step - green with check */
            wm_fill_rect(win, dx, dy, dot_sz, dot_sz, COL_ACCENT);
            draw_check(win, dx + 1, dy + 1, 0xFF020617);
        }
        else if (i == (int)current)
        {
            /* Current step - glow + filled */
            wm_fill_rect_blend(win, dx - 3, dy - 3, dot_sz + 6, dot_sz + 6, COL_ACCENT, 50);
            wm_fill_rect(win, dx, dy, dot_sz, dot_sz, COL_ACCENT);
            wm_draw_rect(win, dx - 1, dy - 1, dot_sz + 2, dot_sz + 2, 0xFFFFFFFF);
        }
        else
        {
            /* Future step - muted */
            wm_fill_rect(win, dx, dy, dot_sz, dot_sz, 0xFF1E293B);
            wm_draw_rect(win, dx, dy, dot_sz, dot_sz, COL_TEXT_MUTED);
        }
        /* Connector line */
        if (i < STEP_COUNT - 1)
        {
            uint32_t lc = (i < (int)current) ? COL_ACCENT : 0xFF333333;
            wm_fill_rect(win, dx + dot_sz + 2, dy + 3, gap - dot_sz - 4, 2, lc);
        }
    }
}

/* Separator line */
static void draw_separator(Window *win, int x, int y, int w)
{
    wm_fill_rect(win, x, y, w, 1, 0xFF333333);
}

/* Forward declaration */
static void draw_check(Window *win, int x, int y, uint32_t color)
{
    /* Glowing checkmark */
    wm_fill_rect(win, x, y + 4, 2, 2, color);
    wm_fill_rect(win, x + 2, y + 6, 2, 2, color);
    wm_fill_rect(win, x + 4, y + 8, 2, 2, color);
    wm_fill_rect(win, x + 6, y + 6, 2, 2, color);
    wm_fill_rect(win, x + 8, y + 4, 2, 2, color);
    wm_fill_rect(win, x + 10, y + 2, 2, 2, color);
    wm_fill_rect(win, x + 12, y, 2, 2, color);
}

static void draw_input_field(Window *win, Rect r, const char *content,
                             bool active, bool is_password, bool show_cursor)
{
    uint32_t border = active ? COL_INPUT_ACTIVE : COL_INPUT_BORDER;
    wm_draw_rounded_rect(win, r.x, r.y, r.w, r.h, COL_INPUT_BG, 4);
    wm_draw_rect(win, r.x, r.y, r.w, r.h, border);

    int tx = r.x + 8;
    int ty = r.y + (r.h - 8) / 2;

    if (is_password)
    {
        int pl = str_len(content);
        char stars[33];
        int i;
        for (i = 0; i < pl && i < 32; i++)
            stars[i] = '*';
        stars[i] = 0;
        wm_draw_string(win, tx, ty, stars, 0xFFFFFFFF, COL_INPUT_BG);
        if (active && show_cursor)
        {
            wm_fill_rect(win, tx + pl * 8, ty - 2, 2, 12, COL_ACCENT);
        }
    }
    else
    {
        wm_draw_string(win, tx, ty, content, 0xFFFFFFFF, COL_INPUT_BG);
        if (active && show_cursor)
        {
            int cl = str_len(content);
            wm_fill_rect(win, tx + cl * 8, ty - 2, 2, 12, COL_ACCENT);
        }
    }
}

static void draw_checkbox(Window *win, int x, int y, bool checked, bool hovered)
{
    int size = 16;
    uint32_t border = hovered ? COL_ACCENT : COL_INPUT_BORDER;
    wm_draw_rounded_rect(win, x, y, size, size, COL_CHECKBOX_BG, 3);
    wm_draw_rect(win, x, y, size, size, border);
    if (checked)
    {
        draw_check(win, x + 2, y + 3, COL_SUCCESS_GREEN);
    }
}

/* Footer / Bottom Bar */
static void draw_bottom_bar(Window *win, InstallerState *state, int cw, int ch)
{
    int bar_h = 48;
    int y = ch - bar_h;

    /* Transparent blur look bar at bottom */
    wm_fill_rect_blend(win, 0, y, cw, bar_h, 0xFF000000, 140);
    wm_fill_rect(win, 0, y, cw, 1, 0xFF333333);

    /* Progress items */
    int tx = 30;
    int ty = y + 15;

    /* 1. Collecting information */
    bool s1_done = state->step >= STEP_INSTALLING;
    uint32_t c1 = s1_done ? COL_TEXT_MUTED : COL_TEXT_PRIMARY;

    if (s1_done)
    {
        draw_check(win, tx, ty + 2, COL_ACCENT);
        wm_draw_string(win, tx + 20, ty, "Step 1: Collected information", c1, 0);
    }
    else
    {
        wm_draw_string(win, tx, ty, "Step 1: Collecting information", c1, 0);
    }

    /* 2. Installing AurionOS */
    int tx2 = cw / 2;
    uint32_t c2 = (state->step >= STEP_INSTALLING) ? COL_ACCENT : COL_TEXT_MUTED;
    wm_draw_string(win, tx2, ty, "Step 2: Installing AurionOS", c2, 0);

    /* Progress % */
    if (state->step == STEP_INSTALLING)
    {
        char pstr[16];
        int pi = 0;
        int p = state->progress;
        if (p >= 100)
        {
            pstr[pi++] = '1';
            pstr[pi++] = '0';
            pstr[pi++] = '0';
        }
        else if (p >= 10)
        {
            pstr[pi++] = '0' + p / 10;
            pstr[pi++] = '0' + p % 10;
        }
        else
            pstr[pi++] = '0' + p;
        pstr[pi++] = '%';
        pstr[pi] = 0;
        wm_draw_string(win, cw - 60, ty, pstr, COL_ACCENT, 0);
    }
}

/* STEP 0: Welcome */
static void draw_welcome(Window *win, InstallerState *state, int cw, int ch)
{
    int c_w = 640;
    int c_h = 480;
    int cx = (cw - c_w) / 2;
    int cy = (ch - c_h) / 2;

    /* Main Glass Container */
    wm_fill_rect_blend(win, cx, cy, c_w, c_h, COL_GLASS_BG, COL_GLASS_ALPHA);
    wm_draw_rect(win, cx, cy, c_w, c_h, 0xFF444444);
    /* Subtle glow border */
    wm_draw_rect(win, cx - 1, cy - 1, c_w + 2, c_h + 2, 0xFF222222);

    /* Step indicator */
    draw_step_dots(win, cx, cy, c_w, state->step);
    draw_separator(win, cx + 40, cy + 40, c_w - 80);

    wm_draw_string(win, cx + 40, cy + 52, "AURION OS", COL_ACCENT, 0);
    wm_draw_string(win, cx + 120, cy + 52, "Installer", COL_TEXT_SECONDARY, 0);

    int tx = cx + 40;
    int ty = cy + 110;
    wm_draw_string(win, tx, ty, "Experience the Next Era.", COL_TEXT_PRIMARY, 0);
    ty += 25;
    wm_draw_string(win, tx, ty, "Modern. Fast. Secure.", COL_TEXT_SECONDARY, 0);

    ty += 60;
    const char *features[] = {
        "Nanokernel architecture",
        "Direct-hardware graphics pipeline",
        "Encrypted user persistence",
        "Cloud-ready networking"};
    for (int i = 0; i < 4; i++)
    {
        draw_check(win, tx, ty, COL_ACCENT);
        wm_draw_string(win, tx + 24, ty, features[i], COL_TEXT_SECONDARY, 0);
        ty += 34;
    }

    /* Disk status */
    ty = cy + c_h - 100;
    if (state->disk_available)
    {
        wm_draw_string(win, tx, ty, "Disk: High-speed SSD detected (LBA mapped)", COL_ACCENT, 0);
    }
    else
    {
        wm_draw_string(win, tx, ty, "Disk: Local-only storage active.", COL_WARNING, 0);
    }

    Rect btn = {cx + c_w - 150, cy + c_h - 60, 110, 36};
    draw_win7_btn(win, btn, "Next", COL_ACCENT, COL_TEXT_DARK, state->hover_btn == 1);
}

/* STEP 1: Keyboard */
static void draw_keyboard(Window *win, InstallerState *state, int cw, int ch)
{
    int c_w = 640;
    int c_h = 480;
    int cx = (cw - c_w) / 2;
    int cy = (ch - c_h) / 2;

    wm_fill_rect_blend(win, cx, cy, c_w, c_h, COL_GLASS_BG, COL_GLASS_ALPHA);
    wm_draw_rect(win, cx, cy, c_w, c_h, 0xFF444444);

    /* Step indicator */
    draw_step_dots(win, cx, cy, c_w, state->step);
    draw_separator(win, cx + 40, cy + 40, c_w - 80);

    int tx = cx + 40;
    int ty = cy + 52;
    wm_draw_string(win, tx, ty, "Keyboard Configuration", COL_ACCENT, 0);
    ty += 50;

    struct
    {
        const char *title;
        int val;
    } layouts[] = {
        {"English (US)", 0},
        {"Serbian (Latin)", 1}};

    for (int li = 0; li < 2; li++)
    {
        bool selected = (keyboard_layout == layouts[li].val);
        bool hovered = (state->hover_layout == li);

        Rect r = {tx, ty, 300, 40};
        if (selected)
        {
            wm_draw_rounded_rect(win, r.x, r.y, r.w, r.h, 0xFF14532D, 8);
            wm_draw_rect(win, r.x, r.y, r.w, r.h, COL_ACCENT);
        }
        else if (hovered)
        {
            wm_draw_rounded_rect(win, r.x, r.y, r.w, r.h, 0xFF1E293B, 8);
            wm_draw_rect(win, r.x, r.y, r.w, r.h, COL_ACCENT);
        }
        else
        {
            wm_draw_rounded_rect(win, r.x, r.y, r.w, r.h, 0xFF1E293B, 8);
            wm_draw_rect(win, r.x, r.y, r.w, r.h, 0xFF334155);
        }

        wm_draw_string(win, r.x + 12, r.y + 14, layouts[li].title, COL_TEXT_PRIMARY, 0);
        if (selected)
            draw_check(win, r.x + 270, r.y + 14, COL_ACCENT);

        ty += 50;
    }

    ty += 30;
    wm_draw_string(win, tx, ty, "Testing area:", COL_TEXT_SECONDARY, 0);
    ty += 28;
    Rect test_r = {tx, ty, 300, 32};
    draw_input_field(win, test_r, state->test_input, true, false, state->cursor_visible);

    int btn_y = cy + c_h - 60;
    Rect back = {cx + c_w - 275, btn_y, 110, 36};
    Rect next = {cx + c_w - 150, btn_y, 110, 36};
    draw_win7_btn(win, back, "Back", COL_BTN_SECONDARY, COL_BTN_SEC_TEXT, state->hover_btn == 0);
    draw_win7_btn(win, next, "Next", COL_ACCENT, COL_TEXT_DARK, state->hover_btn == 1);
}

/* STEP 2: Credentials */
static void draw_credentials(Window *win, InstallerState *state, int cw, int ch)
{
    int c_w = 640;
    int c_h = 480;
    int cx = (cw - c_w) / 2;
    int cy = (ch - c_h) / 2;

    wm_fill_rect_blend(win, cx, cy, c_w, c_h, COL_GLASS_BG, COL_GLASS_ALPHA);
    wm_draw_rect(win, cx, cy, c_w, c_h, 0xFF444444);

    /* Step indicator */
    draw_step_dots(win, cx, cy, c_w, state->step);
    draw_separator(win, cx + 40, cy + 40, c_w - 80);

    int tx = cx + 40;
    int ty = cy + 52;
    wm_draw_string(win, tx, ty, "Account Configuration", COL_ACCENT, 0);
    ty += 50;

    wm_draw_string(win, tx, ty, "Establish your system credentials:", COL_TEXT_PRIMARY, 0);

    ty += 40;
    wm_draw_string(win, tx, ty, "Username:", COL_TEXT_SECONDARY, 0);
    Rect user_r = {tx + 140, ty - 8, 200, 32};
    draw_input_field(win, user_r, state->username, state->active_field == 0, false, state->active_field == 0 && state->cursor_visible);

    ty += 45;
    wm_draw_string(win, tx, ty, "Password:", COL_TEXT_SECONDARY, 0);
    Rect pass_r = {tx + 140, ty - 8, 200, 32};
    draw_input_field(win, pass_r, state->password, state->active_field == 1, true, state->active_field == 1 && state->cursor_visible);

    ty += 45;
    wm_draw_string(win, tx, ty, "Confirm Password:", COL_TEXT_SECONDARY, 0);
    Rect conf_r = {tx + 140, ty - 8, 200, 32};
    draw_input_field(win, conf_r, state->password_confirm, state->active_field == 2, true, state->active_field == 2 && state->cursor_visible);

    if (str_len(state->password_confirm) > 0)
    {
        bool match = strings_equal(state->password, state->password_confirm);
        wm_draw_string(win, tx + 140, ty + 40, match ? "Authentication verified." : "Passwords mismatch.", match ? COL_ACCENT : COL_DANGER, 0);
    }

    if (has_error(state))
    {
        wm_draw_string(win, tx, cy + c_h - 110, state->error_msg, COL_DANGER, 0);
    }

    int btn_y = cy + c_h - 60;
    Rect back = {cx + c_w - 275, btn_y, 110, 36};
    Rect next = {cx + c_w - 150, btn_y, 110, 36};
    draw_win7_btn(win, back, "Back", COL_BTN_SECONDARY, COL_BTN_SEC_TEXT, state->hover_btn == 0);
    draw_win7_btn(win, next, "Next", COL_ACCENT, COL_TEXT_DARK, state->hover_btn == 1);
}

/* STEP 3: App Selection */
static void draw_apps(Window *win, InstallerState *state, int cw, int ch)
{
    int c_w = 640;
    int c_h = 480;
    int cx = (cw - c_w) / 2;
    int cy = (ch - c_h) / 2;

    wm_fill_rect_blend(win, cx, cy, c_w, c_h, COL_GLASS_BG, COL_GLASS_ALPHA);
    wm_draw_rect(win, cx, cy, c_w, c_h, 0xFF444444);

    /* Step indicator */
    draw_step_dots(win, cx, cy, c_w, state->step);
    draw_separator(win, cx + 40, cy + 40, c_w - 80);

    int tx = cx + 40;
    int ty = cy + 52;
    wm_draw_string(win, tx, ty, "Software Synergy", COL_ACCENT, 0);
    ty += 50;
    wm_draw_string(win, tx, ty, "Choose your initial ecosystem components:", COL_TEXT_PRIMARY, 0);

    ty += 40;
    for (int i = 0; i < 5; i++)
    {
        bool hovered = (state->hover_app == i);

        Rect card = {tx, ty - 5, 450, 40};
        if (hovered)
        {
            wm_draw_rounded_rect(win, card.x, card.y, card.w, card.h, 0xFF1E293B, 6);
            wm_draw_rect(win, card.x, card.y, card.w, card.h, COL_ACCENT);
        }

        draw_checkbox(win, tx + 5, ty + 5, state->app_selected[i], hovered);
        wm_draw_string(win, tx + 40, ty + 10, app_names[i], COL_TEXT_PRIMARY, 0);
        wm_draw_string(win, tx + 150, ty + 10, app_descriptions[i], COL_TEXT_MUTED, 0);

        ty += 45;
    }

    int btn_y = cy + c_h - 60;
    Rect back = {cx + c_w - 275, btn_y, 110, 36};
    Rect install = {cx + c_w - 150, btn_y, 110, 36};
    draw_win7_btn(win, back, "Back", COL_BTN_SECONDARY, COL_BTN_SEC_TEXT, state->hover_btn == 0);
    draw_win7_btn(win, install, "Deploy", COL_ACCENT, COL_TEXT_DARK, state->hover_btn == 1);
}

/* Perform actual system initialization */
static bool do_install(InstallerState *state)
{
    c_puts("[INSTALLER] Starting system initialization...\n");

    char cmd[128];
    int l;

    /* 1. Create user */
    c_puts("[INSTALLER] Creating user: ");
    c_puts(state->username);
    c_puts("\n");

    str_copy(cmd, state->username, 32);
    l = str_len(cmd);
    cmd[l++] = ' ';
    str_copy(cmd + l, state->password, 32);
    cmd_useradd(cmd);

    /* 2. Create directory structure */
    c_puts("[INSTALLER] Creating directories...\n");

    char upath[64];
    str_copy(upath, "/users/", 64);
    str_copy(upath + 7, state->username, 32);
    cmd_mkdir(upath);

    const char *dirs[] = {
        "/Code", "/Documents", "/Music", "/Pictures",
        "/Downloads", "/Videos", "/Desktop", "/Desktop/Applications",
        "/System", "/Keyboard", "/Development", NULL};
    for (int i = 0; dirs[i]; i++)
    {
        cmd_mkdir(dirs[i]);
    }

    /* 3. Sample files */
    c_puts("[INSTALLER] Writing sample files...\n");
    save_file_content("/Code/hello.py",
                      "print(\"Hello from python code!\")\n", 33);
    save_file_content("/Code/Makefile",
                      "run:\n\tpython hello.py\n", 22);
    save_file_content("/Development/hello.py",
                      "print(\"Hello from Development/hello.py\")\n", 43);
    save_file_content("/Development/math_demo.py",
                      "a = 12\nb = 7\nprint(\"sum:\", a + b)\nprint(\"mul:\", a * b)\n", 57);
    save_file_content("/Development/loop_demo.py",
                      "for i in range(5):\n    print(\"tick\", i)\n", 40);
    save_file_content("/Development/files_demo.py",
                      "print(\"Create files with: touch notes.txt\")\n", 42);
    save_file_content("/Development/syntax_error.py",
                      "print(\"Hello world\")s\n", 22);
    save_file_content("/Development/Makefile",
                      "run:\n\tpython hello.py\n"
                      "math:\n\tpython math_demo.py\n"
                      "loop:\n\tpython loop_demo.py\n"
                      "files:\n\tpython files_demo.py\n"
                      "syntax:\n\tpython syntax_error.py\n", 141);

    /* 4. Set root password */
    str_copy(cmd, "root ", 128);
    str_copy(cmd + 5, state->password, 32);
    cmd_passwd(cmd);

    /* 5. App hidden markers */
    c_puts("[INSTALLER] Configuring app visibility...\n");
    for (int i = 0; i < 5; i++)
    {
        if (!state->app_selected[i])
        {
            char hpath[128];
            str_copy(hpath, "/Desktop/Applications/.hidden_", 128);
            int pl = str_len(hpath);
            str_copy(hpath + pl, internal_names[i], 32);
            save_file_content(hpath, "HIDDEN", 6);
        }
    }

    /* 6. System config */
    c_puts("[INSTALLER] Writing system configuration...\n");
    save_file_content("/System/current_user",
                      state->username, str_len(state->username));

    if (keyboard_layout == 1)
    {
        save_file_content("/Keyboard/config.sys", "KEYBOARD=SERBIAN", 16);
    }
    else
    {
        save_file_content("/Keyboard/config.sys", "KEYBOARD=ENGLISH", 16);
    }

    /* 6.5. Copy Wallpaper to System and User Home */
    c_puts("[INSTALLER] Deploying official wallpaper folder...\n");
    extern void *kmalloc(uint32_t size);
    extern void kfree(void *ptr);
    extern bool iso9660_find_file(const char *filename, uint32_t *out_lba, uint32_t *out_size);
    extern int disk_read_lba_cdrom(uint32_t lba, uint32_t count, void *buffer);
    extern int disk_read_lba_hdd(uint32_t lba, uint32_t count, void *buffer);

    char *wp_tmp = kmalloc(4 * 1024 * 1024); // 4MB buffer for wallpaper
    if (wp_tmp)
    {
        bool wp_found = false;
        uint32_t wp_lba, wp_size = 0;

        /* Try to read from ISO first */
        if (iso9660_find_file("WP.BMP", &wp_lba, &wp_size))
        {
            if (wp_size > 0 && wp_size <= 4 * 1024 * 1024)
            {
                if (disk_read_lba_cdrom(wp_lba * 4, (wp_size + 511) / 512, wp_tmp) == 0)
                {
                    wp_found = true;
                }
            }
        }

        /* Fallback to raw LBA 10000 */
        if (!wp_found)
        {
            if (disk_read_lba_hdd(10000, 1, wp_tmp) == 0 && wp_tmp[0] == 'B' && wp_tmp[1] == 'M')
            {
                wp_size = *(uint32_t *)(wp_tmp + 2);
                if (wp_size > 0 && wp_size <= 4 * 1024 * 1024)
                {
                    disk_read_lba_hdd(10000, (wp_size + 511) / 512, wp_tmp);
                    wp_found = true;
                }
            }
        }

        if (wp_found)
        {
            /* Create /System/wallpaper.bmp for the Window Manager */
            save_file_content("/System/wallpaper.bmp", wp_tmp, wp_size);

            /* Create global /Wallpapers folder */
            cmd_mkdir("/Wallpapers");
            save_file_content("/Wallpapers/wallpaper.bmp", wp_tmp, wp_size);
            save_file_content("/Wallpapers/Wallpaper1.bmp", wp_tmp, wp_size);

            /* Check if installer background exists and deploy it too */
            uint32_t bg_lba, bg_size;
            if (iso9660_find_file("BG_INST.BMP", &bg_lba, &bg_size))
            {
                char *bg_buf = kmalloc(bg_size + 512);
                if (bg_buf)
                {
                    if (disk_read_lba_cdrom(bg_lba * 4, (bg_size + 511) / 512, bg_buf) == 0)
                    {
                        save_file_content("/Wallpapers/Background_installer.bmp", bg_buf, bg_size);
                        c_puts("[INSTALLER] Background_installer.bmp deployed.\n");
                    }
                    kfree(bg_buf);
                }
            }

            /* Create user-specific /users/NAME/Wallpapers folder and file */
            char user_wp_dir[128];
            str_copy(user_wp_dir, "/users/", 128);
            str_copy(user_wp_dir + 7, state->username, 32);
            int pl = str_len(user_wp_dir);
            str_copy(user_wp_dir + pl, "/Wallpapers", 32);
            cmd_mkdir(user_wp_dir);

            str_copy(user_wp_dir + pl + 11, "/wallpaper.bmp", 32);
            save_file_content(user_wp_dir, wp_tmp, wp_size);

            c_puts("[INSTALLER] Wallpaper deployed to user home and /Wallpapers.\n");
        }
        kfree(wp_tmp);
    }
    else
    {
        c_puts("[INSTALLER] Not enough memory for wallpaper copy.\n");
    }

    /* 7. THE CRITICAL FILE — must be last so partial installs don't
          appear complete */
    c_puts("[INSTALLER] Writing installation marker...\n");
    save_file_content("/installed.sys", "AURION_INSTALLED", 16);

    /* 8. Force explicit disk sync */
    c_puts("[INSTALLER] Syncing filesystem to disk...\n");
    int sync_result = fs_save_to_disk();
    if (sync_result != 0)
    {
        c_puts("[INSTALLER] WARNING: Disk sync failed! (code ");
        char r = '0' + (-sync_result);
        c_puts(&r);
        c_puts(")\n");
        c_puts("[INSTALLER] Data is in RAM but may not survive reboot.\n");
        return false;
    }

    /* 9. Verify by reading back */
    c_puts("[INSTALLER] Verifying installation marker...\n");
    char verify_buf[20];
    verify_buf[0] = 0;
    int vr = load_file_content("/installed.sys", verify_buf, 16);
    if (vr <= 0)
    {
        c_puts("[INSTALLER] WARNING: Verification failed - marker not readable!\n");
        return false;
    }

    /* Check content */
    if (!strings_equal(verify_buf, "AURION_INSTALLED"))
    {
        c_puts("[INSTALLER] WARNING: Marker content mismatch!\n");
        return false;
    }

    c_puts("[INSTALLER] Installation verified successfully!\n");
    return true;
}

/* STEP 4: Installing */
static void draw_installing(Window *win, InstallerState *state, int cw, int ch)
{
    int c_w = 640;
    int c_h = 480;
    int cx = (cw - c_w) / 2;
    int cy = (ch - c_h) / 2;

    /* Main Container Content */
    wm_fill_rect_blend(win, cx, cy, c_w, c_h, COL_GLASS_BG, COL_GLASS_ALPHA);
    wm_draw_rect(win, cx, cy, c_w, c_h, 0xFF444444);

    /* Step indicator */
    draw_step_dots(win, cx, cy, c_w, state->step);
    draw_separator(win, cx + 40, cy + 40, c_w - 80);

    int tx = cx + 40;
    int ty = cy + 52;

    /* Large Title */
    wm_draw_string(win, tx, ty, "SYSTEM DEPLOYMENT ACTIVE", COL_ACCENT, 0);
    ty += 50;

    /* Subtitle from the image */
    wm_draw_string(win, tx, ty, "Finalizing kernel hooks and file expansion.", COL_TEXT_SECONDARY, 0);
    ty += 18;
    wm_draw_string(win, tx, ty, "System will synchronize and reboot automatically.", COL_TEXT_SECONDARY, 0);
    ty += 50;

    /* Checklist */
    int current_msg_idx = (state->progress * INSTALL_MSG_COUNT) / 101;
    if (current_msg_idx >= INSTALL_MSG_COUNT)
        current_msg_idx = INSTALL_MSG_COUNT - 1;

    for (int i = 0; i < INSTALL_MSG_COUNT; i++)
    {
        bool done = i < current_msg_idx;
        bool active = i == current_msg_idx;

        if (done)
        {
            draw_check(win, tx, ty, COL_ACCENT);
        }
        else if (active)
        {
            wm_fill_rect(win, tx, ty + 2, 8, 8, COL_ACCENT);
        }

        uint32_t txt_col = (done || active) ? COL_TEXT_PRIMARY : COL_TEXT_MUTED;
        wm_draw_string(win, tx + 24, ty, install_messages[i], txt_col, 0);
        ty += 34;
    }

    /* Progress bar (Internal) */
    int bar_w = c_w - 80;
    int bar_x = cx + 40;
    int bar_y = cy + c_h - 80;
    int bar_h = 16;

    wm_draw_rounded_rect(win, bar_x, bar_y, bar_w, bar_h, 0xFF1E293B, 4);
    wm_draw_rect(win, bar_x, bar_y, bar_w, bar_h, 0xFF333333);

    int pw = (bar_w * state->progress) / 100;
    if (pw > 0)
    {
        /* Glowy green progress */
        wm_draw_rounded_rect(win, bar_x, bar_y, pw, bar_h, COL_ACCENT, 4);
        wm_fill_rect(win, bar_x, bar_y, pw, 2, 0xFFFFFFFF);
    }

    /* Progress tick */
    uint32_t now = get_ticks();
    if (now - state->last_tick >= 1)
    {
        state->progress++;
        state->last_tick = now;

        if (state->progress >= 100)
        {
            state->install_ok = do_install(state);
            state->step = STEP_FINISHED;
            state->step_enter_tick = get_ticks();
        }
    }
}

/* STEP 5: Finished */
static void draw_finished(Window *win, InstallerState *state, int cw, int ch)
{
    int c_w = 640;
    int c_h = 480;
    int cx = (cw - c_w) / 2;
    int cy = (ch - c_h) / 2;

    wm_fill_rect_blend(win, cx, cy, c_w, c_h, COL_GLASS_BG, COL_GLASS_ALPHA);
    wm_draw_rect(win, cx, cy, c_w, c_h, 0xFF444444);

    /* Step indicator */
    draw_step_dots(win, cx, cy, c_w, state->step);
    draw_separator(win, cx + 40, cy + 40, c_w - 80);

    int tx = cx + 40;
    int ty = cy + 52;
    wm_draw_string(win, tx, ty, "SYNCHRONIZATION COMPLETE", COL_ACCENT, 0);
    ty += 60;

    if (state->install_ok)
    {
        /* Large success checkmark */
        int cx_check = cx + c_w / 2 - 20;
        int cy_check = ty + 10;
        wm_fill_rect(win, cx_check, cy_check + 16, 4, 4, COL_ACCENT);
        wm_fill_rect(win, cx_check + 4, cy_check + 20, 4, 4, COL_ACCENT);
        wm_fill_rect(win, cx_check + 8, cy_check + 24, 4, 4, COL_ACCENT);
        wm_fill_rect(win, cx_check + 12, cy_check + 20, 4, 4, COL_ACCENT);
        wm_fill_rect(win, cx_check + 16, cy_check + 16, 4, 4, COL_ACCENT);
        wm_fill_rect(win, cx_check + 20, cy_check + 12, 4, 4, COL_ACCENT);
        wm_fill_rect(win, cx_check + 24, cy_check + 8, 4, 4, COL_ACCENT);
        wm_fill_rect(win, cx_check + 28, cy_check + 4, 4, 4, COL_ACCENT);

        ty += 60;
        wm_draw_string(win, cx + 40, ty, "AurionOS has been successfully deployed.", COL_TEXT_PRIMARY, 0);
        ty += 24;
        wm_draw_string(win, cx + 40, ty, "Your changes are now stored on the persistent disk.", COL_TEXT_SECONDARY, 0);
        ty += 18;
        wm_draw_string(win, cx + 40, ty, "Please remove the installation media and reboot.", COL_TEXT_SECONDARY, 0);
    }
    else
    {
        wm_draw_string(win, tx, ty, "Deployment completed with local preferences.", COL_ACCENT, 0);
        ty += 30;
        wm_draw_string(win, tx, ty, "AurionOS is ready for experimental use.", COL_TEXT_SECONDARY, 0);
    }

    Rect reboot_btn = {cx + c_w - 280, cy + c_h - 60, 110, 36};
    draw_win7_btn(win, reboot_btn, "Reboot", COL_ACCENT, COL_TEXT_DARK, state->hover_btn == 1);
    Rect continue_btn = {cx + c_w - 150, cy + c_h - 60, 110, 36};
    draw_win7_btn(win, continue_btn, "Continue", COL_SUCCESS_GREEN, COL_TEXT_DARK, state->hover_btn == 2);
}

/* Main draw callback */
static void installer_on_draw(Window *win)
{
    InstallerState *state = (InstallerState *)win->user_data;
    if (!state)
        return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* 1. Draw Desktop Background (Wallpaper) */
    wm_draw_desktop_bg();

    /* 2. Update Cursor bubble */
    uint32_t now = get_ticks();
    if (now - state->cursor_tick >= 30)
    {
        state->cursor_visible = !state->cursor_visible;
        state->cursor_tick = now;
    }

    /* 3. Draw screens */
    switch (state->step)
    {
    case STEP_WELCOME:
        draw_welcome(win, state, cw, ch);
        break;
    case STEP_KEYBOARD:
        draw_keyboard(win, state, cw, ch);
        break;
    case STEP_CREDENTIALS:
        draw_credentials(win, state, cw, ch);
        break;
    case STEP_APPS:
        draw_apps(win, state, cw, ch);
        break;
    case STEP_INSTALLING:
        draw_installing(win, state, cw, ch);
        break;
    case STEP_FINISHED:
        draw_finished(win, state, cw, ch);
        break;
    default:
        break;
    }

    /* 4. Draw Footer Progress Bar */
    draw_bottom_bar(win, state, cw, ch);
}

/* Key callback */
static void installer_on_key(Window *win, uint16_t key)
{
    InstallerState *state = (InstallerState *)win->user_data;
    if (!state)
        return;

    char c = (char)(key & 0xFF);

    if (state->step == STEP_KEYBOARD)
    {
        if (key == 0x08)
        {
            int l = str_len(state->test_input);
            if (l > 0)
                state->test_input[l - 1] = 0;
        }
        else if (c >= 32 && c <= 126)
        {
            char mapped = keyboard_remap(c);
            int l = str_len(state->test_input);
            if (l < 63)
            {
                state->test_input[l] = mapped;
                state->test_input[l + 1] = 0;
            }
        }
        return;
    }

    if (state->step != STEP_CREDENTIALS)
        return;

    char *target;
    int max_len = 31;

    if (state->active_field == 0)
    {
        target = state->username;
    }
    else if (state->active_field == 1)
    {
        target = state->password;
    }
    else
    {
        target = state->password_confirm;
    }

    int len = str_len(target);

    if (key == 0x0D)
    { /* Enter */
        if (state->active_field < 2)
        {
            state->active_field++;
        }
        else
        {
            if (str_len(state->username) == 0)
            {
                set_error(state, "Please enter a username.");
            }
            else if (str_len(state->password) == 0)
            {
                set_error(state, "Please enter a password.");
            }
            else if (!strings_equal(state->password, state->password_confirm))
            {
                set_error(state, "Passwords do not match.");
            }
            else
            {
                state->step = STEP_APPS;
                state->step_enter_tick = get_ticks();
            }
        }
    }
    else if (key == 0x08)
    {
        if (len > 0)
            target[len - 1] = 0;
    }
    else if (key == 0x09)
    {
        state->active_field = (state->active_field + 1) % 3;
    }
    else if (c >= 32 && c <= 126 && len < max_len)
    {
        char mapped = keyboard_remap(c);
        if (state->active_field == 0 && !is_valid_username_char(mapped))
        {
            set_error(state, "Username: only letters, numbers, _ and -");
            return;
        }
        target[len] = mapped;
        target[len + 1] = 0;
    }
}

/* Mouse callback — uses same container coords as draw functions */
static void installer_on_mouse(Window *win, int lx, int ly, bool left, bool right)
{
    InstallerState *state = (InstallerState *)win->user_data;
    if (!state)
        return;
    (void)right;

    bool click = left && !state->prev_left;
    state->prev_left = left;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* Container geometry — must match draw functions exactly */
    int c_w = 640, c_h = 480;
    int cx = (cw - c_w) / 2;
    int cy = (ch - c_h) / 2;
    int tx = cx + 40;
    int btn_y = cy + c_h - 60;

    /* Reset hover */
    state->hover_btn = -1;
    state->hover_app = -1;
    state->hover_layout = -1;

    if (state->step == STEP_WELCOME)
    {
        Rect btn = {cx + c_w - 150, btn_y, 110, 36};
        if (rect_contains(btn, lx, ly))
        {
            state->hover_btn = 1;
            if (click)
            {
                state->step = STEP_KEYBOARD;
                state->step_enter_tick = get_ticks();
            }
        }
    }
    else if (state->step == STEP_KEYBOARD)
    {
        /* Layout cards: match draw_keyboard positions */
        int ty = cy + 40 + 50;
        for (int li = 0; li < 2; li++)
        {
            Rect r = {tx, ty, 300, 40};
            if (rect_contains(r, lx, ly))
            {
                state->hover_layout = li;
                if (click)
                    keyboard_layout = li;
            }
            ty += 50;
        }

        Rect back = {cx + c_w - 275, btn_y, 110, 36};
        Rect next = {cx + c_w - 150, btn_y, 110, 36};
        if (rect_contains(back, lx, ly))
        {
            state->hover_btn = 0;
            if (click)
                state->step = STEP_WELCOME;
        }
        if (rect_contains(next, lx, ly))
        {
            state->hover_btn = 1;
            if (click)
            {
                state->step = STEP_CREDENTIALS;
                state->step_enter_tick = get_ticks();
            }
        }
    }
    else if (state->step == STEP_CREDENTIALS)
    {
        /* Input fields: match draw_credentials positions */
        int ty = cy + 40 + 50 + 40;
        /* Username row */
        Rect user_r = {tx + 140, ty - 8, 200, 32};
        if (rect_contains(user_r, lx, ly) && click)
            state->active_field = 0;
        ty += 45;
        /* Password row */
        Rect pass_r = {tx + 140, ty - 8, 200, 32};
        if (rect_contains(pass_r, lx, ly) && click)
            state->active_field = 1;
        ty += 45;
        /* Confirm row */
        Rect conf_r = {tx + 140, ty - 8, 200, 32};
        if (rect_contains(conf_r, lx, ly) && click)
            state->active_field = 2;

        Rect back = {cx + c_w - 275, btn_y, 110, 36};
        Rect next = {cx + c_w - 150, btn_y, 110, 36};
        if (rect_contains(back, lx, ly))
        {
            state->hover_btn = 0;
            if (click)
                state->step = STEP_KEYBOARD;
        }
        if (rect_contains(next, lx, ly))
        {
            state->hover_btn = 1;
            if (click)
            {
                if (str_len(state->username) == 0)
                {
                    set_error(state, "Please enter a username.");
                }
                else if (str_len(state->password) == 0)
                {
                    set_error(state, "Please enter a password.");
                }
                else if (!strings_equal(state->password, state->password_confirm))
                {
                    set_error(state, "Passwords do not match.");
                }
                else
                {
                    state->step = STEP_APPS;
                    state->step_enter_tick = get_ticks();
                }
            }
        }
    }
    else if (state->step == STEP_APPS)
    {
        /* App rows: match draw_apps positions */
        int ty = cy + 40 + 50 + 40;
        for (int i = 0; i < 5; i++)
        {
            Rect row = {tx, ty - 5, 450, 40};
            if (rect_contains(row, lx, ly))
            {
                state->hover_app = i;
                if (click)
                    state->app_selected[i] = !state->app_selected[i];
            }
            ty += 45;
        }

        Rect back = {cx + c_w - 275, btn_y, 110, 36};
        Rect install = {cx + c_w - 150, btn_y, 110, 36};
        if (rect_contains(back, lx, ly))
        {
            state->hover_btn = 0;
            if (click)
                state->step = STEP_CREDENTIALS;
        }
        if (rect_contains(install, lx, ly))
        {
            state->hover_btn = 1;
            if (click)
            {
                state->step = STEP_INSTALLING;
                state->progress = 0;
                state->last_tick = get_ticks();
                state->step_enter_tick = get_ticks();
            }
        }
    }
    else if (state->step == STEP_FINISHED)
    {
        Rect reboot_btn = {cx + c_w - 280, btn_y, 110, 36};
        Rect continue_btn = {cx + c_w - 150, btn_y, 110, 36};
        if (rect_contains(reboot_btn, lx, ly))
        {
            state->hover_btn = 1;
            if (click)
            {
                c_puts("[INSTALLER] Rebooting...\n");
                fs_save_to_disk();
                __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            }
        }
        if (rect_contains(continue_btn, lx, ly))
        {
            state->hover_btn = 2;
            if (click)
            {
                c_puts("[INSTALLER] Closing installer, continuing to desktop...\n");
                fs_save_to_disk();
                wm_destroy_window(win);
                wm_set_installer_mode(false);
            }
        }
    }
}

/* Entry point */
void app_installer_create(void)
{
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();

    wm_set_installer_mode(true);
    Window *win = wm_create_window("Aurion OS Setup", 0, 0, sw, sh);
    if (!win)
        return;

    win->is_chromeless = true;

    InstallerState *state = (InstallerState *)win->app_data;
    win->user_data = state;

    state->step = STEP_WELCOME;
    state->progress = 0;
    state->last_tick = 0;
    state->step_enter_tick = get_ticks();
    state->username[0] = 0;
    state->password[0] = 0;
    state->password_confirm[0] = 0;
    state->active_field = 0;
    state->prev_left = false;
    state->test_input[0] = 0;
    state->error_msg[0] = 0;
    state->error_tick = 0;
    state->cursor_tick = get_ticks();
    state->cursor_visible = true;
    state->hover_btn = -1;
    state->hover_app = -1;
    state->hover_layout = -1;
    state->install_substep = 0;
    state->install_ok = false;

    /* Check disk availability at startup */
    state->disk_available = ata_is_available();
    if (state->disk_available)
    {
        c_puts("[INSTALLER] Hard disk detected - installation will persist\n");
    }
    else
    {
        c_puts("[INSTALLER] No hard disk - installation will be RAM-only\n");
    }

    for (int i = 0; i < 5; i++)
        state->app_selected[i] = true;

    win->on_draw = installer_on_draw;
    win->on_mouse = installer_on_mouse;
    win->on_key = (void *)installer_on_key;

    wm_focus_window(win);
}