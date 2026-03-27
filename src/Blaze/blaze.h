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
#define BLAZE_MAX_DOM_NODES 256
#define BLAZE_MAX_CSS_RULES 128
#define BLAZE_VIEWPORT_W 1024
#define BLAZE_VIEWPORT_H 768

/* DOM Node Types */
typedef enum {
    NODE_ELEMENT,
    NODE_TEXT,
    NODE_COMMENT,
    NODE_DOCUMENT
} NodeType;

/* DOM Node */
typedef struct DOMNode {
    NodeType type;
    char tag[32];
    char text[256];
    char id[64];
    char class_list[128];
    struct DOMNode *parent;
    struct DOMNode *first_child;
    struct DOMNode *last_child;
    struct DOMNode *next_sibling;
    struct DOMNode *prev_sibling;
    
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
    int font_size;
    
    /* Attributes */
    char href[256];
    char src[256];
    char alt[64];
    int text_align; /* 0=left, 1=center */
    char onclick_script[256];
    
    /* Event handlers */
    void (*onclick)(struct DOMNode *node);
} DOMNode;

/* CSS Rule */
typedef struct {
    char selector[128];
    uint32_t bg_color;
    uint32_t fg_color;
    int padding[4];
    int margin[4];
    int border_width;
    uint32_t border_color;
    int font_size;
    bool display_block;
} CSSRule;

/* Browser Tab */
typedef struct {
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
} BlazeTab;

/* Browser State */
typedef struct {
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

/* CSS Engine */
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

#endif /* BLAZE_H */
