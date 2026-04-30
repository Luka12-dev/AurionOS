#ifndef AURIONUI_H
#define AURIONUI_H

#include <stdint.h>
#include <stdbool.h>

typedef void* UIWindow;
typedef void* UIWidget;
typedef void* UIMenu;
typedef void* UIIcon;
typedef void* UIImage;
typedef void* UICanvas;
typedef void* UIDialog;
typedef void* UIFont;
typedef void* UIVar;

#define UI_TOP 0
#define UI_BOTTOM 1
#define UI_LEFT 2
#define UI_RIGHT 3
#define UI_NONE 0
#define UI_X 1
#define UI_Y 2
#define UI_BOTH 3
#define UI_CENTER 4
#define UI_N 5
#define UI_S 6
#define UI_E 7
#define UI_W 8
#define UI_NE 9
#define UI_NW 10
#define UI_SE 11
#define UI_SW 12
#define UI_HORIZONTAL 0
#define UI_VERTICAL 1
#define UI_DETERMINATE 0
#define UI_INDETERMINATE 1
#define UI_END -1

extern UIWindow ui_window_create(const char *title, int width, int height);
extern void ui_window_destroy(UIWindow win);
extern void ui_window_mainloop(UIWindow win);
extern void ui_window_update(UIWindow win);
extern void ui_window_set_title(UIWindow win, const char *title);
extern void ui_window_set_icon(UIWindow win, const char *icon_path);
extern void ui_window_set_menu(UIWindow win, UIMenu menu);
extern void ui_window_center(UIWindow win);
extern void ui_window_maximize(UIWindow win);
extern void ui_window_minimize(UIWindow win);
extern void ui_window_set_bg(UIWindow win, const char *color);
extern void ui_window_destroy(UIWindow win);

extern UIWidget ui_label_create(UIWindow parent, const char *text);
extern UIWidget ui_button_create(UIWindow parent, const char *text, void (*command)(void));
extern UIWidget ui_entry_create(UIWindow parent, const char *placeholder);
extern UIWidget ui_textbox_create(UIWindow parent, int width, int height);
extern UIWidget ui_checkbutton_create(UIWindow parent, const char *text, bool *var);
extern UIWidget ui_radiobutton_create(UIWindow parent, const char *text, int *var, int value);
extern UIWidget ui_listbox_create(UIWindow parent, int height);
extern UIWidget ui_canvas_create(UIWindow parent, int width, int height);
extern UIWidget ui_frame_create(UIWindow parent);
extern UIWidget ui_progressbar_create(UIWindow parent, int length, int mode);
extern UIWidget ui_slider_create(UIWindow parent, float from, float to, int orient);
extern UIWidget ui_spinbox_create(UIWindow parent, float from, float to);
extern UIWidget ui_combobox_create(UIWindow parent, const char **values, int count);
extern UIWidget ui_treeview_create(UIWindow parent, const char **columns, int ncols);
extern UIWidget ui_tabview_create(UIWindow parent);
extern UIWidget ui_menu_bar_create(UIWindow parent, void *items);
extern UIWidget ui_menu_create(const char *label);
extern UIWidget ui_menu_item_create(const char *label, void (*command)(void));
extern UIWidget ui_menu_separator_create(void);

extern void ui_widget_destroy(UIWidget w);
extern void ui_widget_pack(UIWidget w, int side, int fill, int expand, int anchor, int padx, int pady, int ipadx, int ipady);
extern void ui_widget_configure(UIWidget w, const char *prop, void *value);
extern void ui_label_set_text(UIWidget w, const char *text);
extern const char *ui_label_get_text(UIWidget w);
extern void ui_entry_set_text(UIWidget w, const char *text);
extern const char *ui_entry_get_text(UIWidget w);
extern void ui_textbox_insert(UIWidget w, const char *text);
extern const char *ui_textbox_get(UIWidget w, int start, int end);
extern void ui_button_set_text(UIWidget w, const char *text);
extern void ui_progressbar_set(UIWidget w, float value);
extern float ui_progressbar_get(UIWidget w);
extern void ui_progressbar_start(UIWidget w);
extern void ui_progressbar_stop(UIWidget w);
extern void ui_slider_set_value(UIWidget w, float value);
extern float ui_slider_get_value(UIWidget w);
extern void ui_spinbox_set_value(UIWidget w, float value);
extern float ui_spinbox_get_value(UIWidget w);
extern void ui_combobox_set(UIWidget w, const char *value);
extern const char *ui_combobox_get(UIWidget w);
extern void ui_listbox_insert(UIWidget w, int index, const char *item);
extern void ui_listbox_delete(UIWidget w, int index);
extern int ui_listbox_size(UIWidget w);
extern int ui_listbox_curselection(UIWidget w);
extern const char *ui_listbox_get(UIWidget w, int index);
extern void ui_treeview_insert(UIWidget w, const char *parent, const char *text, const char **values, int nvalues);
extern void ui_tabview_add(UIWidget w, const char *title);
extern UIWindow ui_tabview_get_tab(UIWidget w, int index);
extern void ui_canvas_clear(UIWidget w, const char *color);
extern void ui_canvas_draw_line(UIWidget w, int x1, int y1, int x2, int y2, const char *color, int width);
extern void ui_canvas_draw_rect(UIWidget w, int x, int y, int w, int h, const char *fill, const char *outline);
extern void ui_canvas_draw_oval(UIWidget w, int x, int y, int w, int h, const char *fill, const char *outline);
extern void ui_canvas_draw_text(UIWidget w, int x, int y, const char *text, const char *color);
extern void ui_canvas_draw_image(UIWidget w, int x, int y, const char *image_path);
extern void ui_canvas_draw_polygon(UIWidget w, int *points, int npoints, const char *fill, const char *outline);
extern void ui_widget_bind(UIWidget w, const char *event, void (*handler)(void *, int, int));
extern void ui_widget_unbind(UIWidget w, const char *event);
extern void ui_window_after(UIWindow win, int ms, void (*handler)(void *), void *arg);

extern void ui_messagebox_show_info(UIWindow parent, const char *title, const char *message);
extern void ui_messagebox_show_warning(UIWindow parent, const char *title, const char *message);
extern void ui_messagebox_show_error(UIWindow parent, const char *title, const char *message);
extern bool ui_messagebox_ask_yesno(UIWindow parent, const char *title, const char *message);
extern const char *ui_filedialog_open(UIWindow parent, const char *title, const char **filetypes);
extern const char *ui_filedialog_save(UIWindow parent, const char *title, const char *defaultext, const char **filetypes);
extern const char *ui_filedialog_directory(UIWindow parent, const char *title);

extern UIFont ui_font_create(const char *family, int size, int weight, int slant, bool underline);
extern void ui_font_destroy(UIFont font);
extern void ui_widget_set_font(UIWidget w, UIFont font);

extern UIImage ui_image_load(const char *path);
extern void ui_image_destroy(UIImage img);
extern UIImage ui_image_resize(UIImage img, int width, int height);
extern void ui_widget_set_image(UIWidget w, UIImage img);

typedef struct {
    const char *name;
    void (*command)(void);
    struct UIWidgetItem *children;
    int nchildren;
    bool enabled;
} UIMenuItemDef;

typedef struct {
    const char *label;
    UIMenuItemDef *items;
    int nitems;
} UIMenuDef;

typedef struct {
    const char *label;
    UIMenuDef *menus;
    int nmenus;
} UIMenuBarDef;

extern UIMenu ui_menu_bar_build(UIWindow parent, UIMenuBarDef *def);
extern void ui_menu_destroy(UIMenu menu);

typedef struct {
    int pid;
    char name[64];
    float cpu_percent;
    uint32_t memory_kb;
    bool running;
} UIProcessInfo;

typedef struct {
    int pid;
    char title[128];
    bool minimized;
} UIWindowInfo;

extern int ui_os_get_processes(UIProcessInfo *processes, int max_count);
extern int ui_os_get_windows(UIWindowInfo *windows, int max_count);
extern int ui_os_get_system_info(char *cpu_name, int cpu_name_len, uint32_t *total_mem_kb, uint32_t *free_mem_kb);
extern int ui_os_set_window_foreground(int pid);
extern int ui_os_minimize_window(int pid);
extern int ui_os_close_window(int pid);
extern uint32_t ui_os_get_tick_count(void);

typedef struct {
    void (*play)(const char *path);
    void (*pause)(void);
    void (*resume)(void);
    void (*stop)(void);
    void (*set_volume)(float vol);
    float (*get_volume)(void);
    float (*get_position)(void);
    float (*get_duration)(void);
    bool (*is_playing)(void);
} UIMP3Player;

extern UIMP3Player *ui_audio_mp3_create(void);
extern void ui_audio_mp3_destroy(UIMP3Player *player);
extern void ui_audio_mp3_load(UIMP3Player *player, const char *path);
extern void ui_audio_play(const char *path);
extern void ui_audio_stop(void);

typedef struct {
    void *ctx;
    int width;
    int height;
} UIGraphicsContext;

extern UIGraphicsContext *ui_graphics_create_context(int width, int height);
extern void ui_graphics_destroy_context(UIGraphicsContext *ctx);
extern void ui_graphics_clear(UIGraphicsContext *ctx, const char *color);
extern void ui_graphics_set_color(UIGraphicsContext *ctx, const char *color);
extern void ui_graphics_draw_line(UIGraphicsContext *ctx, int x1, int y1, int x2, int y2, int width);
extern void ui_graphics_draw_rect(UIGraphicsContext *ctx, int x, int y, int w, int h);
extern void ui_graphics_draw_oval(UIGraphicsContext *ctx, int x, int y, int w, int h);
extern void ui_graphics_draw_text(UIGraphicsContext *ctx, int x, int y, const char *text);
extern void ui_graphics_present(UIGraphicsContext *ctx);
extern uint32_t *ui_graphics_get_buffer(UIGraphicsContext *ctx);

typedef enum {
    VAR_NONE = 0,
    VAR_STRING,
    VAR_INT,
    VAR_FLOAT,
    VAR_BOOL
} UIVarType;

extern UIVar ui_var_create_string(const char *value);
extern UIVar ui_var_create_int(int value);
extern UIVar ui_var_create_float(double value);
extern UIVar ui_var_create_bool(bool value);
extern void ui_var_destroy(UIVar v);
extern void ui_var_set_string(UIVar v, const char *value);
extern void ui_var_set_int(UIVar v, int value);
extern void ui_var_set_float(UIVar v, double value);
extern void ui_var_set_bool(UIVar v, bool value);
extern const char *ui_var_as_string(UIVar v);
extern int ui_var_as_int(UIVar v);
extern double ui_var_as_float(UIVar v);
extern bool ui_var_as_bool(UIVar v);
extern UIVarType ui_var_type(UIVar v);

extern void ui_run_async(void (*func)(void *), void *arg);
extern void ui_sleep(int ms);

#endif
