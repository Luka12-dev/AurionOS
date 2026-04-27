/*
 * Blaze Browser - Main Header
 * Full-featured web browser for AurionOS
 */

#ifndef BLAZE_H
#define BLAZE_H

#include <stdint.h>
#include <stdbool.h>

/* Browser Configuration */
#define BLAZE_MAX_TABS 2
#define BLAZE_MAX_URL_LEN 1024
#define BLAZE_MAX_TITLE_LEN 128
#define BLAZE_MAX_HISTORY 100
#define BLAZE_MAX_BOOKMARKS 50
#define BLAZE_MAX_DOM_NODES 1024
#define BLAZE_MAX_CSS_RULES 512
#define BLAZE_VIEWPORT_W 1024
#define BLAZE_VIEWPORT_H 768

/* Unset / Auto Constants */
#define SIZE_UNSET -1000000
#define SIZE_AUTO -1000001
#define SIZE_NONE -1000002
#define Z_UNSET -1000000
#define Z_AUTO -1000001
#define COLOR_UNSET 0

/* Display Types */
#define DISPLAY_NONE 0
#define DISPLAY_BLOCK 1
#define DISPLAY_INLINE 2
#define DISPLAY_INLINE_BLOCK 3
#define DISPLAY_FLEX 4
#define DISPLAY_INLINE_FLEX 5
#define DISPLAY_GRID 6
#define DISPLAY_TABLE 7
#define DISPLAY_TABLE_ROW 8
#define DISPLAY_TABLE_CELL 9
#define DISPLAY_LIST_ITEM 10
#define DISPLAY_TABLE_ROW_GROUP 11
#define DISPLAY_TABLE_COLUMN_GROUP 12
#define DISPLAY_TABLE_COLUMN 13
#define DISPLAY_TABLE_CAPTION 14
#define DISPLAY_TABLE_HEADER_GROUP 15
#define DISPLAY_TABLE_FOOTER_GROUP 16
#define DISPLAY_FLOW_ROOT 17
#define DISPLAY_INLINE_GRID 18
#define DISPLAY_CONTENTS 19
#define DISPLAY_UNSET 255

/* Position Types */
#define POS_STATIC 0
#define POS_RELATIVE 1
#define POS_ABSOLUTE 2
#define POS_FIXED 3
#define POS_STICKY 4
#define POS_UNSET 255

/* Visibility Types */
#define VISIBILITY_VISIBLE 0
#define VISIBILITY_HIDDEN 1
#define VISIBILITY_COLLAPSE 2
#define VISIBILITY_UNSET 255

/* White-space Types */
#define WS_NORMAL 0
#define WS_NOWRAP 1
#define WS_PRE 2
#define WS_PRE_WRAP 3
#define WS_PRE_LINE 4
#define WS_BREAK_SPACES 5
#define WS_UNSET 255

/* Text-transform Types */
#define TT_NONE 0
#define TT_UPPERCASE 1
#define TT_LOWERCASE 2
#define TT_CAPITALIZE 3
#define TT_FULL_WIDTH 4
#define TT_UNSET 255

/* Cursor Types */
#define CURSOR_DEFAULT 0
#define CURSOR_POINTER 1
#define CURSOR_TEXT 2
#define CURSOR_MOVE 3
#define CURSOR_NOT_ALLOWED 4
#define CURSOR_CROSSHAIR 5
#define CURSOR_WAIT 6
#define CURSOR_HELP 7
#define CURSOR_GRAB 8
#define CURSOR_GRABBING 9
#define CURSOR_COL_RESIZE 10
#define CURSOR_ROW_RESIZE 11
#define CURSOR_N_RESIZE 12
#define CURSOR_S_RESIZE 13
#define CURSOR_E_RESIZE 14
#define CURSOR_W_RESIZE 15
#define CURSOR_NE_RESIZE 16
#define CURSOR_NW_RESIZE 17
#define CURSOR_SE_RESIZE 18
#define CURSOR_SW_RESIZE 19
#define CURSOR_ZOOM_IN 20
#define CURSOR_ZOOM_OUT 21
#define CURSOR_NONE 22
#define CURSOR_PROGRESS 23
#define CURSOR_UNSET 255

/* List Types */
#define LIST_NONE 0
#define LIST_UNORDERED 1
#define LIST_ORDERED 2

/* Input Types */
#define INPUT_TEXT 0
#define INPUT_PASSWORD 1
#define INPUT_EMAIL 2
#define INPUT_NUMBER 3
#define INPUT_TEL 4
#define INPUT_URL 5
#define INPUT_SEARCH 6
#define INPUT_CHECKBOX 7
#define INPUT_RADIO 8
#define INPUT_SUBMIT 9
#define INPUT_RESET 10
#define INPUT_BUTTON 11
#define INPUT_HIDDEN 12
#define INPUT_FILE 13
#define INPUT_IMAGE 14
#define INPUT_COLOR 15
#define INPUT_DATE 16
#define INPUT_TIME 17
#define INPUT_RANGE 18

/* DOM Limits */
#define DOM_MAX_CHILDREN 64

/* JavaScript Value Types */
typedef enum
{
    VAL_UNDEFINED = 0,
    VAL_NULL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STRING,
    VAL_OBJECT,
    VAL_FUNCTION,
    VAL_ARRAY,
    VAL_NATIVE_FUNC,
    VAL_PROMISE,
    VAL_ERROR,
    VAL_REGEXP,
    VAL_DATE,
    VAL_SYMBOL,
    VAL_MAP,
    VAL_SET,
    VAL_WEAKMAP,
    VAL_WEAKSET,
    VAL_GENERATOR,
    VAL_PROXY,
    VAL_ARRAYBUFFER,
    VAL_TYPEDARRAY,
    VAL_ITERATOR,
    VAL_REFERENCE /* Internal: reference to a variable/property for assignment */
} JSValueType;

/* Value flags */
#define VAL_FLAG_NONE 0x00
#define VAL_FLAG_FROZEN 0x01
#define VAL_FLAG_BORROWED 0x02 /* Don't free string/object */
#define VAL_FLAG_RETURNED 0x04

typedef struct JSObject JSObject;
typedef struct JSContext JSContext;
typedef struct JSFunction JSFunction;
typedef struct JSPromise JSPromise;
typedef struct JSScope JSScope;

/* Native function signature */
typedef struct JSValue JSValue;
typedef JSValue (*NativeFunc)(JSContext *ctx, JSValue this_val, JSValue *args, int argc);

/* JS Value */
struct JSValue
{
    JSValueType type;
    union
    {
        bool boolean;
        double number;
        char *string;
        JSObject *object;
        JSFunction *function;
        NativeFunc native_func;
        struct JSPromise *promise;
        void *ptr;
        struct
        {
            JSScope *scope;
            char *name;
            JSObject *obj;
            char *prop_name;
        } reference;
    } as;
    uint32_t flags;
};

/* DOM Node Types */
typedef enum
{
    NODE_ELEMENT,
    NODE_TEXT,
    NODE_COMMENT,
    NODE_DOCUMENT
} NodeType;

/* DOM Node */
typedef struct DOMNode
{
    NodeType type;
    char tag[32];
    char text[256];
    char text_content[1024];
    char id[64];
    char class_list[128];
    struct DOMNode *parent;
    struct DOMNode *first_child;
    struct DOMNode *last_child;
    struct DOMNode *next_sibling;
    struct DOMNode *prev_sibling;
    struct DOMNode *children[DOM_MAX_CHILDREN];

    /* Computed style */
    uint32_t bg_color;
    uint32_t fg_color;
    int x, y, w, h;
    int fixed_w, fixed_h;
    int padding[4];
    int margin[4];
    int border_width;
    uint32_t border_color;
    int border_radius;
    int border_radius_corners[4];
    int font_size;

    uint8_t display;
    uint8_t visibility;
    uint8_t opacity;
    int z_index;
    bool overflow_hidden;

    /* Font style */
    bool is_bold;
    bool is_italic;
    bool is_underline;
    bool is_strikethrough;
    uint8_t text_transform;
    uint8_t white_space;
    char font_family[64];

    /* Layout properties */
    uint8_t position;
    int pos_top, pos_left, pos_right, pos_bottom;
    int min_w, max_w, min_h, max_h;
    int line_height;
    int letter_spacing;
    int word_spacing;
    int text_indent;

    /* Flexbox/Grid */
    uint8_t flex_direction;
    uint8_t justify_content;
    uint8_t align_items;
    int gap;
    bool flex_wrap;
    int flex_grow;
    int flex_shrink;
    int flex_basis;

    /* Grid layout properties */
    int grid_template_columns[16];
    int grid_template_rows[16];
    int grid_column_count;
    int grid_row_count;
    int grid_gap;
    int grid_column_start;
    int grid_column_end;
    int grid_row_start;
    int grid_row_end;
    bool is_grid_container;

    /* Attributes */
    char href[256];
    char src[256];
    char alt[64];
    char title_attr[128];
    char name[64];
    char value[256];
    char placeholder[128];
    char action[256];
    char method[16];
    char target[32];
    char rel[64];
    char type_attr[32];
    uint8_t input_type;

    int colspan;
    int rowspan;
    int tabindex;
    int maxlength;
    char min_attr[32];
    char max_attr[32];
    char step_attr[32];
    char pattern[128];
    char for_attr[64];
    char role[32];
    char lang[16];
    char dir_attr[8];
    char data_attr[128];
    char aria_label[128];

    int text_align; /* 0=left, 1=center, 2=right, 3=justify */
    uint8_t cursor_type;

    /* List properties */
    uint8_t list_type;
    int list_index;

    /* Special effects */
    bool has_shadow;
    bool has_text_shadow;
    char transform[64];
    bool has_transition;
    bool has_animation;
    int outline_width;
    uint32_t outline_color;

    /* States */
    bool is_hovered;
    bool is_active;
    bool has_focus;
    bool is_disabled;
    bool is_checked;
    bool is_readonly;
    bool is_hidden;
    bool is_required;
    bool is_autofocus;
    bool is_multiple;
    bool is_selected;
    bool is_autoplay;
    bool has_controls;
    bool is_loop;
    bool is_muted;
    bool is_defer;
    bool is_async;
    bool is_open;
    bool is_novalidate;
    bool is_draggable;
    bool is_contenteditable;
    bool is_spellcheck;

    /* Event handlers */
    char onclick_script[256];
    char onmouseover_script[256];
    char onmouseout_script[256];
    char onsubmit_script[256];
    char oninput_script[256];
    char onkeydown_script[256];
    char onkeyup_script[256];
    char onfocus_script[256];
    char onchange_script[256];
    char onload_script[256];
    char onblur_script[256];

    void (*onclick)(struct DOMNode *node);

    /* Tree state */
    int child_count;
    int scroll_x, scroll_y;
} DOMNode;

/* CSS Rule */
typedef struct
{
    char selector[128];
    uint32_t bg_color;
    uint32_t fg_color;
    int padding[4];
    int margin[4];
    int border_width;
    uint32_t border_color;
    int border_radius;
    int border_radius_corners[4];
    int font_size;
    uint8_t display;
    uint8_t position;
    int flex_grow;
    int flex_shrink;
    int flex_basis;
    bool is_important;
    int specificity;
    bool display_block;
    /* Extended author stylesheet (blaze_css.c) */
    int text_align; /* -1 = unset; 0–3 = left/center/right/justify */
    int width_px;   /* 0 = unset */
    int height_px;  /* 0 = unset */
    bool rule_font_bold;
    bool rule_font_italic;
    bool rule_font_weight_set;
    bool rule_font_style_set;
    bool display_specified;
    /* Grid layout properties */
    int grid_template_columns[16];
    int grid_template_rows[16];
    int grid_column_count;
    int grid_row_count;
    char grid_template_areas[256];
    int grid_gap;
    int grid_column_start;
    int grid_column_end;
    int grid_row_start;
    int grid_row_end;
    int justify_items;
    int align_items;
    bool is_grid_container;
} CSSRule;

/* Browser Tab */
typedef struct
{
    char url[BLAZE_MAX_URL_LEN];
    char title[BLAZE_MAX_TITLE_LEN];
    bool loading;
    int scroll_y;

    /* DOM */
    DOMNode *document;
    DOMNode nodes[BLAZE_MAX_DOM_NODES];
    int node_count;

    /* CSS */
    CSSRule rules[BLAZE_MAX_CSS_RULES];
    int rule_count;

    /* Page content */
    char *html_content;
    uint32_t html_len;

    /* Render cache */
    uint32_t *render_buffer;
    bool needs_reflow;
    bool needs_render;

    /* JavaScript */
    JSContext *js_context;
} BlazeTab;

/* Browser State */
typedef struct
{
    BlazeTab tabs[BLAZE_MAX_TABS];
    int active_tab;
    int tab_count;

    /* UI State */
    char address_bar[BLAZE_MAX_URL_LEN];
    bool address_bar_focused;
    int address_bar_cursor;

    /* History */
    char history[BLAZE_MAX_HISTORY][BLAZE_MAX_URL_LEN];
    int history_count;
    int history_pos;

    /* Bookmarks */
    char bookmarks[BLAZE_MAX_BOOKMARKS][BLAZE_MAX_URL_LEN];
    char bookmark_titles[BLAZE_MAX_BOOKMARKS][BLAZE_MAX_TITLE_LEN];
    int bookmark_count;

    /* Developer console */
    bool console_open;
    char console_log[4096];
    int console_len;
    int console_scroll_y;
} BlazeState;

/* Utilities */
void blaze_log(BlazeState *state, const char *msg);
uint32_t blaze_parse_color(const char *color_str);
void blaze_str_copy(char *dst, const char *src, int max);
int blaze_str_cmp(const char *a, const char *b);
int blaze_str_len(const char *s);
bool blaze_str_starts_with(const char *str, const char *prefix);
const char *blaze_str_strstr(const char *haystack, const char *needle);
bool blaze_str_contains(const char *str, const char *needle);

/* Core Functions */
void blaze_init(BlazeState *state);
void blaze_navigate(BlazeState *state, const char *url);
void blaze_render(BlazeState *state, void *window);
void blaze_handle_input(BlazeState *state, uint16_t key, int mx, int my, bool click);
void blaze_go_back(BlazeState *state);
void blaze_go_forward(BlazeState *state);
void blaze_reload(BlazeState *state);
void blaze_add_bookmark(BlazeState *state);
void blaze_load_bookmarks(BlazeState *state);
int blaze_new_tab(BlazeState *state);
void blaze_close_tab(BlazeState *state, int idx);

/* HTML Parser */
void blaze_parse_html(BlazeTab *tab, const char *html, uint32_t len);
DOMNode *blaze_create_element(BlazeTab *tab, const char *tag);
DOMNode *blaze_create_text(BlazeTab *tab, const char *text);
void blaze_append_child(DOMNode *parent, DOMNode *child);
DOMNode *blaze_get_element_by_id(BlazeTab *tab, const char *id);
void blaze_set_inner_html(BlazeTab *tab, DOMNode *node, const char *html);

/* CSS Engine */
void blaze_css_parse(BlazeTab *tab, const char *css);
void blaze_parse_css(BlazeTab *tab, const char *css, uint32_t len);
void blaze_apply_styles(BlazeTab *tab);
bool blaze_selector_matches(DOMNode *node, const char *selector);

/* Layout Engine */
void blaze_layout(BlazeTab *tab, int viewport_w, int viewport_h);
void blaze_layout_node(DOMNode *node, int parent_x, int parent_y, int parent_w);

/* Rendering */
void blaze_paint(BlazeTab *tab, void *window, int scroll_y, int viewport_h);
void blaze_paint_node(DOMNode *node, void *window, int scroll_y, int offset_y);

/* Network */
int blaze_fetch(const char *url, char **out_content, uint32_t *out_len);
int blaze_fetch_https(const char *host, const char *path, char **out_content, uint32_t *out_len);

/* JavaScript Engine */
void blaze_js_init(BlazeTab *tab);
void blaze_js_execute(BlazeTab *tab, const char *script);
void blaze_js_fire_dom_content_loaded(BlazeTab *tab);

#endif /* BLAZE_H */
