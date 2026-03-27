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

/* Forward declaration for post-install transition */
extern void wm_set_installer_mode(bool enabled);

typedef enum {
    STEP_WELCOME     = 0,
    STEP_KEYBOARD    = 1,
    STEP_CREDENTIALS = 2,
    STEP_APPS        = 3,
    STEP_INSTALLING  = 4,
    STEP_FINISHED    = 5,
    STEP_COUNT       = 6
} InstallerStep;

typedef struct {
    int x, y, w, h;
} Rect;

typedef struct {
    InstallerStep step;
    char username[32];
    char password[32];
    char password_confirm[32];
    int active_field;       /* 0=username, 1=password, 2=confirm */
    bool app_selected[5];
    int progress;
    uint32_t last_tick;
    uint32_t step_enter_tick;
    bool prev_left;
    char test_input[64];

    /* Hover tracking */
    int hover_btn;          /* -1=none, 0=back, 1=next/install, 2=continue */
    int hover_app;          /* -1=none, 0..4=app index */
    int hover_layout;       /* -1=none, 0=english, 1=serbian */

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
#define COL_BG             0xFF0A0A0A
#define COL_HEADER_BG      0xFF0F0F0F
#define COL_HEADER_LINE    0xFF1A1A1A
#define COL_ACCENT         0xFF4ADE80
#define COL_ACCENT_HOVER   0xFF6EE7A0
#define COL_ACCENT_DIM     0xFF2D8B52
#define COL_DANGER         0xFFEF4444
#define COL_WARNING        0xFFFBBF24
#define COL_TEXT_PRIMARY   0xFFFFFFFF
#define COL_TEXT_SECONDARY 0xFFAAAAAA
#define COL_TEXT_MUTED     0xFF666666
#define COL_TEXT_DARK      0xFF000000
#define COL_INPUT_BG       0xFF111111
#define COL_INPUT_BORDER   0xFF333333
#define COL_INPUT_ACTIVE   0xFF4ADE80
#define COL_BTN_SECONDARY  0xFF1A1A1A
#define COL_BTN_SEC_HOVER  0xFF252525
#define COL_BTN_SEC_TEXT   0xFFBBBBBB
#define COL_CARD_BG        0xFF0D0D0D
#define COL_CARD_BORDER    0xFF1A1A1A
#define COL_PROGRESS_BG    0xFF1A1A1A
#define COL_CHECKBOX_BG    0xFF111111

static const char *app_names[] = {
    "Terminal", "Notepad", "Paint", "Calculator", "Browser"
};
static const char *app_descriptions[] = {
    "Command-line interface",
    "Text editor",
    "Drawing application",
    "Math utilities",
    "Web browser"
};
static uint32_t app_colors[] = {
    0xFF6366F1, 0xFFEAB308, 0xFFEC4899, 0xFF10B981, 0xFF3B82F6
};
static const char *internal_names[] = {
    "Terminal", "Notepad", "Paint", "Calculator", "Blaze"
};

static const char *install_messages[] = {
    "Preparing disk partitions...",
    "Extracting kernel modules...",
    "Installing base system...",
    "Extracting UI assets...",
    "Creating user profiles...",
    "Configuring system services...",
    "Setting up network stack...",
    "Configuring users...",
    "Writing boot configuration...",
    "Persisting to disk...",
    "Verifying installation..."
};
#define INSTALL_MSG_COUNT 11

static const char *step_titles[] = {
    "WELCOME", "KEYBOARD", "ACCOUNT", "COMPONENTS", "INSTALLING", "COMPLETE"
};

/* Utility */
static bool rect_contains(Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int clamp_val(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void set_error(InstallerState *s, const char *msg) {
    str_copy(s->error_msg, msg, 63);
    s->error_tick = get_ticks();
}

static bool has_error(InstallerState *s) {
    if (s->error_msg[0] == 0) return false;
    if (get_ticks() - s->error_tick > 300) {
        s->error_msg[0] = 0;
        return false;
    }
    return true;
}

static bool is_valid_username_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static bool strings_equal(const char *a, const char *b) {
    int i;
    for (i = 0; a[i] && b[i]; i++) {
        if (a[i] != b[i]) return false;
    }
    return a[i] == b[i];
}

/* Drawing helpers */
static void draw_rounded_btn(Window *win, Rect r, const char *label,
                             uint32_t bg, uint32_t fg, bool hovered) {
    uint32_t color = hovered ? (bg == COL_ACCENT ? COL_ACCENT_HOVER : COL_BTN_SEC_HOVER) : bg;
    wm_fill_rect(win, r.x, r.y, r.w, r.h, color);
    wm_fill_rect(win, r.x, r.y, r.w, 1, hovered ? 0xFF333333 : 0xFF222222);

    int text_len = str_len(label);
    int tx = r.x + (r.w - text_len * 8) / 2;
    int ty = r.y + (r.h - 8) / 2;
    wm_draw_string(win, tx, ty, label, fg, color);
}

static void draw_input_field(Window *win, Rect r, const char *content,
                             bool active, bool is_password, bool show_cursor) {
    uint32_t border = active ? COL_INPUT_ACTIVE : COL_INPUT_BORDER;
    wm_fill_rect(win, r.x, r.y, r.w, r.h, COL_INPUT_BG);
    wm_draw_rect(win, r.x, r.y, r.w, r.h, border);

    if (active) {
        wm_fill_rect(win, r.x, r.y, 3, r.h, COL_ACCENT);
    }

    int tx = r.x + 12;
    int ty = r.y + (r.h - 8) / 2;

    if (is_password) {
        int pl = str_len(content);
        char stars[33];
        int i;
        for (i = 0; i < pl && i < 32; i++) stars[i] = '*';
        stars[i] = 0;
        wm_draw_string(win, tx, ty, stars, COL_TEXT_PRIMARY, COL_INPUT_BG);
        if (active && show_cursor) {
            wm_fill_rect(win, tx + pl * 8, ty - 2, 2, 12, COL_ACCENT);
        }
    } else {
        wm_draw_string(win, tx, ty, content, COL_TEXT_PRIMARY, COL_INPUT_BG);
        if (active && show_cursor) {
            int cl = str_len(content);
            wm_fill_rect(win, tx + cl * 8, ty - 2, 2, 12, COL_ACCENT);
        }
    }
}

static void draw_progress_indicator(Window *win, int x, int y, int total_steps, int current) {
    int dot_size = 8;
    int spacing = 24;
    int total_w = (total_steps - 1) * spacing + dot_size;
    int sx = x - total_w / 2;

    for (int i = 0; i < total_steps; i++) {
        int dx = sx + i * spacing;
        if (i < total_steps - 1) {
            uint32_t line_col = (i < current) ? COL_ACCENT_DIM : 0xFF222222;
            wm_fill_rect(win, dx + dot_size, y + 3, spacing - dot_size, 2, line_col);
        }
        uint32_t dot_col = (i <= current) ? COL_ACCENT : 0xFF222222;
        wm_fill_rect(win, dx, y, dot_size, dot_size, dot_col);
        if (i == current) {
            wm_fill_rect(win, dx - 1, y - 1, dot_size + 2, dot_size + 2, dot_col);
        }
    }
}

static void draw_checkbox(Window *win, int x, int y, bool checked, bool hovered) {
    int size = 18;
    uint32_t border = checked ? COL_ACCENT : (hovered ? 0xFF555555 : COL_INPUT_BORDER);
    wm_fill_rect(win, x, y, size, size, checked ? COL_ACCENT_DIM : COL_CHECKBOX_BG);
    wm_draw_rect(win, x, y, size, size, border);
    if (checked) {
        wm_fill_rect(win, x + 3, y + 3, size - 6, size - 6, COL_ACCENT);
    }
}

/* Header */
static void draw_header(Window *win, InstallerState *state, int cw) {
    int header_h = 80;
    wm_fill_rect(win, 0, 0, cw, header_h, COL_HEADER_BG);
    wm_fill_rect(win, 0, header_h - 1, cw, 1, COL_HEADER_LINE);

    wm_draw_string(win, 30, 20, "AURION OS", COL_ACCENT, COL_HEADER_BG);
    wm_draw_string(win, 30, 38, "System Setup", COL_TEXT_MUTED, COL_HEADER_BG);

    if (state->step < STEP_INSTALLING) {
        int indicator_x = cw - 120;
        draw_progress_indicator(win, indicator_x, 36, 4, (int)state->step);
    }

    if ((int)state->step < STEP_COUNT) {
        wm_draw_string(win, cw - 200, 20, step_titles[state->step],
                       COL_TEXT_SECONDARY, COL_HEADER_BG);
    }
}

/* Error banner */
static void draw_error_banner(Window *win, InstallerState *state, int cw, int y) {
    if (!has_error(state)) return;
    wm_fill_rect(win, 30, y, cw - 60, 30, 0xFF2A1215);
    wm_draw_rect(win, 30, y, cw - 60, 30, COL_DANGER);
    wm_draw_string(win, 45, y + 10, state->error_msg, COL_DANGER, 0xFF2A1215);
}

/* STEP 0: Welcome */
static void draw_welcome(Window *win, InstallerState *state, int cw, int ch) {
    int cx = cw / 2;
    int cy = ch / 2 - 40;

    wm_draw_string(win, cx - 80, cy - 20, "Welcome to Aurion", COL_TEXT_PRIMARY, COL_BG);
    wm_draw_string(win, cx - 168, cy + 8,
                   "A fast, modern operating system built from scratch.",
                   COL_TEXT_SECONDARY, COL_BG);

    int fy = cy + 50;
    int feature_x = cx - 120;
    const char *features[] = {
        "Lightweight kernel",
        "Graphical window manager",
        "Built-in applications",
        "Persistent file system"
    };
    for (int i = 0; i < 4; i++) {
        wm_fill_rect(win, feature_x, fy, 8, 8, COL_ACCENT);
        wm_draw_string(win, feature_x + 16, fy, features[i], COL_TEXT_SECONDARY, COL_BG);
        fy += 22;
    }

    /* Disk status indicator */
    int status_y = cy + 155;
    if (state->disk_available) {
        wm_fill_rect(win, cx - 120, status_y, 8, 8, COL_ACCENT);
        wm_draw_string(win, cx - 104, status_y,
                       "Hard disk detected - ready to install",
                       COL_ACCENT, COL_BG);
    } else {
        wm_fill_rect(win, cx - 120, status_y, 8, 8, COL_WARNING);
        wm_draw_string(win, cx - 104, status_y,
                       "No hard disk - changes won't persist!",
                       COL_WARNING, COL_BG);
    }

    Rect btn = {cx - 80, cy + 185, 160, 42};
    draw_rounded_btn(win, btn, "BEGIN SETUP", COL_ACCENT, COL_TEXT_DARK,
                     state->hover_btn == 1);

    wm_draw_string(win, cx - 80, cy + 235,
                   "This will take about a minute.",
                   COL_TEXT_MUTED, COL_BG);
}

/* STEP 1: Keyboard */
static void draw_keyboard(Window *win, InstallerState *state, int cw, int ch) {
    int left_margin = 40;
    int content_w = cw - 80;
    int y = 100;

    wm_draw_string(win, left_margin, y, "Choose your keyboard layout",
                   COL_TEXT_PRIMARY, COL_BG);
    wm_draw_string(win, left_margin, y + 18,
                   "You can change this later using the KEYBOARD command.",
                   COL_TEXT_MUTED, COL_BG);

    y = 160;

    /* Layout option cards */
    struct { const char *title; const char *subtitle; int layout_val; } layouts[] = {
        {"English (US)", "QWERTY layout", 0},
        {"Serbian (Latin)", "QWERTZ layout", 1}
    };

    for (int li = 0; li < 2; li++) {
        bool selected = (keyboard_layout == layouts[li].layout_val);
        bool hovered  = (state->hover_layout == li);
        uint32_t card_bg = selected ? 0xFF0F1A0F : (hovered ? 0xFF111111 : COL_CARD_BG);
        uint32_t border  = selected ? COL_ACCENT : (hovered ? 0xFF444444 : COL_CARD_BORDER);

        Rect r = {left_margin, y, clamp_val(content_w, 100, 350), 50};
        wm_fill_rect(win, r.x, r.y, r.w, r.h, card_bg);
        wm_draw_rect(win, r.x, r.y, r.w, r.h, border);
        if (selected) wm_fill_rect(win, r.x, r.y, 3, r.h, COL_ACCENT);

        wm_draw_string(win, r.x + 16, r.y + 10, layouts[li].title,
                       selected ? COL_TEXT_PRIMARY : COL_TEXT_SECONDARY, card_bg);
        wm_draw_string(win, r.x + 16, r.y + 28, layouts[li].subtitle,
                       COL_TEXT_MUTED, card_bg);

        y += 65;
    }

    y += 15;

    /* Type test */
    wm_draw_string(win, left_margin, y, "Test your layout:", COL_TEXT_SECONDARY, COL_BG);
    y += 20;
    Rect test_r = {left_margin, y, clamp_val(content_w, 100, 350), 35};
    draw_input_field(win, test_r, state->test_input, true, false, state->cursor_visible);

    /* Navigation */
    int btn_y = ch - 70;
    Rect back = {left_margin, btn_y, 110, 38};
    Rect next = {left_margin + 125, btn_y, 110, 38};
    draw_rounded_btn(win, back, "BACK", COL_BTN_SECONDARY, COL_BTN_SEC_TEXT,
                     state->hover_btn == 0);
    draw_rounded_btn(win, next, "NEXT", COL_ACCENT, COL_TEXT_DARK,
                     state->hover_btn == 1);
}

/* STEP 2: Credentials */
static void draw_credentials(Window *win, InstallerState *state, int cw, int ch) {
    int left_margin = 40;
    int field_w = clamp_val(cw - 80, 100, 350);
    int y = 100;

    wm_draw_string(win, left_margin, y, "Create your account", COL_TEXT_PRIMARY, COL_BG);
    wm_draw_string(win, left_margin, y + 18,
                   "This will be the primary user on the system.",
                   COL_TEXT_MUTED, COL_BG);

    y = 160;

    /* Username */
    wm_draw_string(win, left_margin, y, "Username", COL_TEXT_SECONDARY, COL_BG);
    y += 18;
    Rect user_r = {left_margin, y, field_w, 35};
    draw_input_field(win, user_r, state->username,
                     state->active_field == 0, false,
                     state->active_field == 0 && state->cursor_visible);
    wm_draw_string(win, left_margin, y + 40,
                   "Letters, numbers, underscore, dash only",
                   COL_TEXT_MUTED, COL_BG);

    y += 65;

    /* Password */
    wm_draw_string(win, left_margin, y, "Password", COL_TEXT_SECONDARY, COL_BG);
    y += 18;
    Rect pass_r = {left_margin, y, field_w, 35};
    draw_input_field(win, pass_r, state->password,
                     state->active_field == 1, true,
                     state->active_field == 1 && state->cursor_visible);

    y += 50;

    /* Confirm */
    wm_draw_string(win, left_margin, y, "Confirm Password", COL_TEXT_SECONDARY, COL_BG);
    y += 18;
    Rect conf_r = {left_margin, y, field_w, 35};
    draw_input_field(win, conf_r, state->password_confirm,
                     state->active_field == 2, true,
                     state->active_field == 2 && state->cursor_visible);

    /* Password match indicator */
    if (str_len(state->password_confirm) > 0) {
        bool match = strings_equal(state->password, state->password_confirm);
        uint32_t indicator_col = match ? COL_ACCENT : COL_DANGER;
        const char *indicator_txt = match ? "Passwords match" : "Passwords do not match";
        wm_draw_string(win, left_margin, y + 40, indicator_txt, indicator_col, COL_BG);
    }

    draw_error_banner(win, state, cw, ch - 115);

    /* Navigation */
    int btn_y = ch - 70;
    bool can_proceed = str_len(state->username) > 0 && str_len(state->password) > 0;

    Rect back = {left_margin, btn_y, 110, 38};
    Rect next = {left_margin + 125, btn_y, 110, 38};
    draw_rounded_btn(win, back, "BACK", COL_BTN_SECONDARY, COL_BTN_SEC_TEXT,
                     state->hover_btn == 0);
    draw_rounded_btn(win, next, "NEXT",
                     can_proceed ? COL_ACCENT : 0xFF1A2A1A,
                     can_proceed ? COL_TEXT_DARK : COL_TEXT_MUTED,
                     can_proceed && state->hover_btn == 1);
}

/* STEP 3: App Selection */
static void draw_apps(Window *win, InstallerState *state, int cw, int ch) {
    int left_margin = 40;
    int y = 100;

    wm_draw_string(win, left_margin, y, "Select components to install",
                   COL_TEXT_PRIMARY, COL_BG);
    wm_draw_string(win, left_margin, y + 18,
                   "Unselected apps can be enabled later.",
                   COL_TEXT_MUTED, COL_BG);

    y = 155;

    for (int i = 0; i < 5; i++) {
        int iy = y + (i * 52);
        bool hovered = (state->hover_app == i);
        uint32_t row_bg = hovered ? 0xFF111111 : COL_BG;

        wm_fill_rect(win, left_margin - 5, iy - 5,
                     clamp_val(cw - 70, 100, 380), 44, row_bg);

        draw_checkbox(win, left_margin, iy, state->app_selected[i], hovered);
        wm_fill_rect(win, left_margin + 28, iy, 18, 18, app_colors[i]);
        wm_draw_string(win, left_margin + 55, iy + 1,
                       app_names[i], COL_TEXT_PRIMARY, row_bg);
        wm_draw_string(win, left_margin + 55, iy + 15,
                       app_descriptions[i], COL_TEXT_MUTED, row_bg);
    }

    /* Count */
    int count = 0;
    for (int i = 0; i < 5; i++) {
        if (state->app_selected[i]) count++;
    }
    char count_str[32];
    count_str[0] = '0' + count;
    str_copy(count_str + 1, " of 5 selected", 20);
    wm_draw_string(win, left_margin, y + 5 * 52 + 10, count_str, COL_TEXT_MUTED, COL_BG);

    /* Navigation */
    int btn_y = ch - 70;
    Rect back = {left_margin, btn_y, 110, 38};
    Rect install = {left_margin + 125, btn_y, 130, 38};
    draw_rounded_btn(win, back, "BACK", COL_BTN_SECONDARY, COL_BTN_SEC_TEXT,
                     state->hover_btn == 0);
    draw_rounded_btn(win, install, "INSTALL", COL_ACCENT, COL_TEXT_DARK,
                     state->hover_btn == 1);
}

/* Perform actual system initialization */
static bool do_install(InstallerState *state) {
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
        "/System", "/Keyboard", NULL
    };
    for (int i = 0; dirs[i]; i++) {
        cmd_mkdir(dirs[i]);
    }

    /* 3. Sample files */
    c_puts("[INSTALLER] Writing sample files...\n");
    save_file_content("/Code/hello.py",
                      "print(\"Hello from python code!\")\n", 33);
    save_file_content("/Code/Makefile",
                      "run:\n\tpython hello.py\n", 22);

    /* 4. Set root password */
    str_copy(cmd, "root ", 128);
    str_copy(cmd + 5, state->password, 32);
    cmd_passwd(cmd);

    /* 5. App hidden markers */
    c_puts("[INSTALLER] Configuring app visibility...\n");
    for (int i = 0; i < 5; i++) {
        if (!state->app_selected[i]) {
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

    if (keyboard_layout == 1) {
        save_file_content("/Keyboard/config.sys", "KEYBOARD=SERBIAN", 16);
    } else {
        save_file_content("/Keyboard/config.sys", "KEYBOARD=ENGLISH", 16);
    }

    /* 7. THE CRITICAL FILE — must be last so partial installs don't
          appear complete */
    c_puts("[INSTALLER] Writing installation marker...\n");
    save_file_content("/installed.sys", "AURION_INSTALLED", 16);

    /* 8. Force explicit disk sync */
    c_puts("[INSTALLER] Syncing filesystem to disk...\n");
    int sync_result = fs_save_to_disk();
    if (sync_result != 0) {
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
    if (vr <= 0) {
        c_puts("[INSTALLER] WARNING: Verification failed - marker not readable!\n");
        return false;
    }

    /* Check content */
    if (!strings_equal(verify_buf, "AURION_INSTALLED")) {
        c_puts("[INSTALLER] WARNING: Marker content mismatch!\n");
        return false;
    }

    c_puts("[INSTALLER] Installation verified successfully!\n");
    return true;
}

/* STEP 4: Installing */
static void draw_installing(Window *win, InstallerState *state, int cw, int ch) {
    int cx = cw / 2;
    int cy = ch / 2 - 50;

    wm_draw_string(win, cx - 72, cy - 20, "Installing Aurion OS",
                   COL_TEXT_PRIMARY, COL_BG);

    /* Progress bar */
    int bar_w = clamp_val(cw - 120, 100, 400);
    int bar_x = cx - bar_w / 2;
    int bar_y = cy + 15;
    int bar_h = 10;

    wm_fill_rect(win, bar_x, bar_y, bar_w, bar_h, COL_PROGRESS_BG);
    int pw = (bar_w * state->progress) / 100;
    if (pw > 0) {
        wm_fill_rect(win, bar_x, bar_y, pw, bar_h, COL_ACCENT);
        if (pw > 2) {
            wm_fill_rect(win, bar_x + pw - 2, bar_y, 2, bar_h, COL_ACCENT_HOVER);
        }
    }

    /* Percentage */
    char pct[8];
    int p = state->progress;
    int pi = 0;
    if (p >= 100) {
        pct[0] = '1'; pct[1] = '0'; pct[2] = '0'; pct[3] = '%'; pct[4] = 0;
    } else if (p >= 10) {
        pct[pi++] = '0' + p / 10;
        pct[pi++] = '0' + p % 10;
        pct[pi++] = '%';
        pct[pi] = 0;
    } else {
        pct[pi++] = '0' + p;
        pct[pi++] = '%';
        pct[pi] = 0;
    }
    wm_draw_string(win, bar_x + bar_w + 10, bar_y, pct, COL_TEXT_SECONDARY, COL_BG);

    /* Status message */
    int msg_idx = (state->progress * INSTALL_MSG_COUNT) / 101;
    if (msg_idx >= INSTALL_MSG_COUNT) msg_idx = INSTALL_MSG_COUNT - 1;

    /* Clear previous message area to prevent overlap */
    wm_fill_rect(win, bar_x - 50, bar_y + 25, bar_w + 100, 16, COL_BG);
    wm_draw_string(win, cx - 100, bar_y + 25,
                   install_messages[msg_idx], COL_TEXT_MUTED, COL_BG);

    /* Spinner dots */
    uint32_t tick = get_ticks();
    int dot = (tick / 20) % 4;
    int dx = cx - 15;
    int dy = bar_y + 55;
    for (int di = 0; di < 4; di++) {
        uint32_t dcol = (di == dot) ? COL_ACCENT : 0xFF222222;
        wm_fill_rect(win, dx + di * 12, dy, 6, 6, dcol);
    }

    /* Progress tick */
    uint32_t now = get_ticks();
    if (now - state->last_tick >= 2) {
        state->progress++;
        state->last_tick = now;

        if (state->progress >= 100) {
            /* Do the actual installation */
            state->install_ok = do_install(state);
            state->step = STEP_FINISHED;
            state->step_enter_tick = get_ticks();
        }
    }
}

/* STEP 5: Finished */
static void draw_finished(Window *win, InstallerState *state, int cw, int ch) {
    int cx = cw / 2;
    int cy = ch / 2 - 80;

    /* Success / warning icon */
    uint32_t icon_col = state->install_ok ? COL_ACCENT : COL_WARNING;
    wm_fill_rect(win, cx - 15, cy - 40, 30, 30, icon_col);

    if (state->install_ok) {
        wm_draw_string(win, cx - 60, cy + 5, "Setup Complete!", COL_ACCENT, COL_BG);
        wm_draw_string(win, cx - 96, cy + 30,
                       "Aurion OS has been installed.", COL_TEXT_PRIMARY, COL_BG);
    } else {
        wm_draw_string(win, cx - 80, cy + 5, "Setup Complete (RAM only)",
                       COL_WARNING, COL_BG);
        wm_draw_string(win, cx - 128, cy + 30,
                       "No disk detected. Changes live in RAM only.",
                       COL_TEXT_PRIMARY, COL_BG);
    }

    /* Info card */
    int card_x = cx - 175;
    int card_y = cy + 60;
    int card_w = 350;

    if (state->install_ok) {
        /* Disk available and installation succeeded */
        int card_h = 55;
        wm_fill_rect(win, card_x, card_y, card_w, card_h, 0xFF0F1A0F);
        wm_draw_rect(win, card_x, card_y, card_w, card_h, COL_ACCENT);
        wm_fill_rect(win, card_x, card_y, 3, card_h, COL_ACCENT);
        wm_draw_string(win, card_x + 14, card_y + 10,
                       "Installation saved to disk successfully.",
                       COL_ACCENT, 0xFF0F1A0F);
        wm_draw_string(win, card_x + 14, card_y + 28,
                       "Click Reboot to restart your system.",
                       COL_TEXT_SECONDARY, 0xFF0F1A0F);
    } else {
        /* Installation failed or no disk */
        int card_h = 70;
        wm_fill_rect(win, card_x, card_y, card_w, card_h, 0xFF1A1008);
        wm_draw_rect(win, card_x, card_y, card_w, card_h, COL_WARNING);
        wm_fill_rect(win, card_x, card_y, 3, card_h, COL_WARNING);
        wm_draw_string(win, card_x + 14, card_y + 10,
                       "WARNING: Installation failed!",
                       COL_WARNING, 0xFF1A1008);
        wm_draw_string(win, card_x + 14, card_y + 28,
                       "Changes are in RAM only.",
                       COL_WARNING, 0xFF1A1008);
        wm_draw_string(win, card_x + 14, card_y + 46,
                       "After reboot, boot from HDD for persistence.",
                       COL_WARNING, 0xFF1A1008);
    }

    /* Buttons */
    int btn_y = cy + 165;

    /* "Reboot" — centered button */
    Rect reboot_btn = {cx - 80, btn_y, 160, 42};
    draw_rounded_btn(win, reboot_btn, "REBOOT NOW",
                     COL_ACCENT, COL_TEXT_DARK,
                     state->hover_btn == 1);
}

/* Main draw callback */
static void installer_on_draw(Window *win) {
    InstallerState *state = (InstallerState *)win->user_data;
    if (!state) return;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);

    /* Cursor blink update */
    uint32_t now = get_ticks();
    if (now - state->cursor_tick >= 30) {
        state->cursor_visible = !state->cursor_visible;
        state->cursor_tick = now;
    }

    wm_fill_rect(win, 0, 0, cw, ch, COL_BG);

    if (state->step < STEP_INSTALLING) {
        draw_header(win, state, cw);
    }

    switch (state->step) {
        case STEP_WELCOME:     draw_welcome(win, state, cw, ch); break;
        case STEP_KEYBOARD:    draw_keyboard(win, state, cw, ch); break;
        case STEP_CREDENTIALS: draw_credentials(win, state, cw, ch); break;
        case STEP_APPS:        draw_apps(win, state, cw, ch); break;
        case STEP_INSTALLING:  draw_installing(win, state, cw, ch); break;
        case STEP_FINISHED:    draw_finished(win, state, cw, ch); break;
        default: break;
    }
}

/* Key callback */
static void installer_on_key(Window *win, uint16_t key) {
    InstallerState *state = (InstallerState *)win->user_data;
    if (!state) return;

    char c = (char)(key & 0xFF);

    if (state->step == STEP_KEYBOARD) {
        if (key == 0x08) {
            int l = str_len(state->test_input);
            if (l > 0) state->test_input[l - 1] = 0;
        } else if (c >= 32 && c <= 126) {
            char mapped = keyboard_remap(c);
            int l = str_len(state->test_input);
            if (l < 63) {
                state->test_input[l] = mapped;
                state->test_input[l + 1] = 0;
            }
        }
        return;
    }

    if (state->step != STEP_CREDENTIALS) return;

    char *target;
    int max_len = 31;

    if (state->active_field == 0) {
        target = state->username;
    } else if (state->active_field == 1) {
        target = state->password;
    } else {
        target = state->password_confirm;
    }

    int len = str_len(target);

    if (key == 0x08) {
        if (len > 0) target[len - 1] = 0;
    } else if (key == 0x09) {
        state->active_field = (state->active_field + 1) % 3;
    } else if (c >= 32 && c <= 126 && len < max_len) {
        char mapped = keyboard_remap(c);
        if (state->active_field == 0 && !is_valid_username_char(mapped)) {
            set_error(state, "Username: only letters, numbers, _ and -");
            return;
        }
        target[len] = mapped;
        target[len + 1] = 0;
    }
}

/* Mouse callback */
static void installer_on_mouse(Window *win, int lx, int ly, bool left, bool right) {
    InstallerState *state = (InstallerState *)win->user_data;
    if (!state) return;
    (void)right;

    bool click = left && !state->prev_left;
    state->prev_left = left;

    int cw = wm_client_w(win);
    int ch = wm_client_h(win);
    int cx = cw / 2;
    int left_margin = 40;
    int btn_y = ch - 70;

    /* Reset hover */
    state->hover_btn = -1;
    state->hover_app = -1;
    state->hover_layout = -1;

    if (state->step == STEP_WELCOME) {
        int cy = ch / 2 - 40;
        Rect btn = {cx - 80, cy + 185, 160, 42};
        if (rect_contains(btn, lx, ly)) {
            state->hover_btn = 1;
            if (click) {
                state->step = STEP_KEYBOARD;
                state->step_enter_tick = get_ticks();
            }
        }
    } else if (state->step == STEP_KEYBOARD) {
        int content_w = clamp_val(cw - 80, 100, 350);

        Rect en = {left_margin, 160, content_w, 50};
        Rect sr = {left_margin, 225, content_w, 50};
        if (rect_contains(en, lx, ly)) {
            state->hover_layout = 0;
            if (click) keyboard_layout = 0;
        }
        if (rect_contains(sr, lx, ly)) {
            state->hover_layout = 1;
            if (click) keyboard_layout = 1;
        }

        Rect back = {left_margin, btn_y, 110, 38};
        Rect next = {left_margin + 125, btn_y, 110, 38};
        if (rect_contains(back, lx, ly)) {
            state->hover_btn = 0;
            if (click) state->step = STEP_WELCOME;
        }
        if (rect_contains(next, lx, ly)) {
            state->hover_btn = 1;
            if (click) {
                state->step = STEP_CREDENTIALS;
                state->step_enter_tick = get_ticks();
            }
        }
    } else if (state->step == STEP_CREDENTIALS) {
        int field_w = clamp_val(cw - 80, 100, 350);
        Rect user_r = {left_margin, 178, field_w, 35};
        Rect pass_r = {left_margin, 261, field_w, 35};
        Rect conf_r = {left_margin, 329, field_w, 35};

        if (rect_contains(user_r, lx, ly) && click) state->active_field = 0;
        if (rect_contains(pass_r, lx, ly) && click) state->active_field = 1;
        if (rect_contains(conf_r, lx, ly) && click) state->active_field = 2;

        Rect back = {left_margin, btn_y, 110, 38};
        Rect next = {left_margin + 125, btn_y, 110, 38};

        if (rect_contains(back, lx, ly)) {
            state->hover_btn = 0;
            if (click) state->step = STEP_KEYBOARD;
        }
        if (rect_contains(next, lx, ly)) {
            state->hover_btn = 1;
            if (click) {
                if (str_len(state->username) == 0) {
                    set_error(state, "Please enter a username.");
                } else if (str_len(state->password) == 0) {
                    set_error(state, "Please enter a password.");
                } else if (!strings_equal(state->password, state->password_confirm)) {
                    set_error(state, "Passwords do not match.");
                } else {
                    state->step = STEP_APPS;
                    state->step_enter_tick = get_ticks();
                }
            }
        }
    } else if (state->step == STEP_APPS) {
        for (int i = 0; i < 5; i++) {
            int iy = 155 + (i * 52);
            Rect row = {left_margin - 5, iy - 5, clamp_val(cw - 70, 100, 380), 44};
            if (rect_contains(row, lx, ly)) {
                state->hover_app = i;
                if (click) state->app_selected[i] = !state->app_selected[i];
            }
        }

        Rect back = {left_margin, btn_y, 110, 38};
        Rect install = {left_margin + 125, btn_y, 130, 38};

        if (rect_contains(back, lx, ly)) {
            state->hover_btn = 0;
            if (click) state->step = STEP_CREDENTIALS;
        }
        if (rect_contains(install, lx, ly)) {
            state->hover_btn = 1;
            if (click) {
                state->step = STEP_INSTALLING;
                state->progress = 0;
                state->last_tick = get_ticks();
                state->step_enter_tick = get_ticks();
            }
        }
    } else if (state->step == STEP_FINISHED) {
        int cy_finish = ch / 2 - 80;
        int finish_btn_y = cy_finish + 165;

        /* Reboot button - centered */
        Rect reboot_btn = {cx - 80, finish_btn_y, 160, 42};
        if (rect_contains(reboot_btn, lx, ly)) {
            state->hover_btn = 1;
            if (click) {
                c_puts("[INSTALLER] Rebooting...\n");

                /* Extra flush before reboot */
                fs_save_to_disk();

                __asm__ volatile(
                    "outb %0, %1"
                    :
                    : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64)
                );
            }
        }
    }
}

/* Entry point */
void app_installer_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();

    Window *win = wm_create_window("Aurion OS Setup", 0, 0, sw, sh);
    if (!win) return;

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
    if (state->disk_available) {
        c_puts("[INSTALLER] Hard disk detected - installation will persist\n");
    } else {
        c_puts("[INSTALLER] No hard disk - installation will be RAM-only\n");
    }

    for (int i = 0; i < 5; i++) state->app_selected[i] = true;

    win->on_draw = installer_on_draw;
    win->on_mouse = installer_on_mouse;
    win->on_key = (void *)installer_on_key;

    wm_focus_window(win);
}