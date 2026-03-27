/*
 * Blaze Browser - Application Entry Point
 * Integrates browser with AurionOS window manager
*/

#include "blaze.h"
#include "../window_manager.h"

/* External functions */
extern uint16_t c_getkey_nonblock(void);
extern int c_kb_hit(void);

void blaze_free_tab_content(BlazeTab *tab);
int blaze_get_page_height(BlazeState *state);

/* Browser state */
BlazeState browser_state;

/* Window callbacks */
static void blaze_on_draw(Window *win) {
    blaze_render(&browser_state, win);
}

static void blaze_on_key(Window *win, uint16_t key) {
    (void)win;
    blaze_handle_input(&browser_state, key, 0, 0, false);
}

static void blaze_on_mouse(Window *win, int lx, int ly, bool left, bool right) {
    (void)win;
    (void)right;
    
    static bool prev_left = false;
    bool click = left && !prev_left;
    prev_left = left;
    
    int cw = wm_client_w(win);
    BlazeTab *tab = &browser_state.tabs[browser_state.active_tab];
    
    if (click) {
        /* Back button */
        if (lx >= 8 && lx < 32 && ly >= 8 && ly < 32) {
            blaze_go_back(&browser_state);
            return;
        }
        
        /* Forward button */
        if (lx >= 36 && lx < 60 && ly >= 8 && ly < 32) {
            blaze_go_forward(&browser_state);
            return;
        }
        
        /* Refresh button 'R' */
        if (lx >= 64 && lx < 88 && ly >= 8 && ly < 32) {
            blaze_navigate(&browser_state, tab->url);
            return;
        }

        /* Address bar */
        int addr_x = 96;
        int addr_w = cw - 230;
        if (lx >= addr_x && lx < addr_x + addr_w && ly >= 8 && ly < 32) {
            browser_state.address_bar_focused = true;
            blaze_str_copy(browser_state.address_bar, tab->url, BLAZE_MAX_URL_LEN);
            browser_state.address_bar_cursor = blaze_str_len(browser_state.address_bar);
            return;
        }

        /* Console button */
        if (lx >= cw - 120 && lx < cw - 50 && ly >= 4 && ly < 36) {
            browser_state.console_open = !browser_state.console_open;
            return;
        }

        /* Bookmark button */
        if (lx >= cw - 36 && lx < cw - 12 && ly >= 4 && ly < 36) {
            blaze_add_bookmark(&browser_state);
            return;
        }
        
        /* Tab clicks */
        if (ly >= 40 && ly < 68) {
            int tab_idx = lx / 160;
            if (tab_idx < browser_state.tab_count) {
                /* Close button */
                int tab_x = tab_idx * 160;
                if (lx >= tab_x + 134 && lx < tab_x + 150) {
                    blaze_close_tab(&browser_state, tab_idx);
                } else {
                    browser_state.active_tab = tab_idx;
                }
            } else if (lx >= browser_state.tab_count * 160 && lx < browser_state.tab_count * 160 + 32) {
                /* New tab button */
                int nidx = blaze_new_tab(&browser_state);
                if (nidx >= 0) {
                    blaze_navigate(&browser_state, "/Documents/index.html");
                }
            }
            return;
        }
        
        /* Content area clicks - check for links */
        if (ly >= 68) {
            int doc_x = lx;
            int doc_y = ly - 68 + tab->scroll_y;
            
            extern DOMNode* blaze_find_node_at(BlazeTab *tab, int x, int y);
            DOMNode *node = blaze_find_node_at(tab, doc_x, doc_y);
            
            while (node) {
                if (node->type == NODE_ELEMENT) {
                    if (node->onclick_script[0]) {
                        extern void blaze_js_execute(BlazeTab *tab, const char *script);
                        blaze_js_execute(tab, node->onclick_script);
                        return;
                    }
                    if (node->href[0]) {
                        blaze_navigate(&browser_state, node->href);
                        return;
                    }
                }
                node = node->parent;
            }
        }
    }
    
    static int last_drag_y = -1;

    if (left) {
        if (!click && ly >= 68) {
            /* Mouse is held down but not just clicked - handle scrolling */
            if (last_drag_y != -1) {
                int dy = ly - last_drag_y;
                int page_h = blaze_get_page_height(&browser_state);
                int ch = wm_client_h(win);
                int viewport_h = ch - 68;
                int max_scroll = (page_h > viewport_h) ? (page_h - viewport_h) : 0;
                
                tab->scroll_y -= dy;
                if (tab->scroll_y > max_scroll) tab->scroll_y = max_scroll;
                if (tab->scroll_y < 0) tab->scroll_y = 0;
            }
            last_drag_y = ly;
        } else {
            last_drag_y = ly;
        }
    } else {
        last_drag_y = -1;
    }
}

static void blaze_on_close(Window *win) {
    (void)win;
    /* Cleanup */
    for (int i = 0; i < browser_state.tab_count; i++) {
        BlazeTab *tab = &browser_state.tabs[i];
        if (tab->html_content) {
            extern void kfree(void *ptr);
            kfree(tab->html_content);
        }
        if (tab->render_buffer) {
            extern void kfree(void *ptr);
            kfree(tab->render_buffer);
        }
    }
}

/* Create browser window */
void app_blaze_create(void) {
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    
    /* Bigger browser window - almost fullscreen */
    int w = sw - 100;
    int h = sh - WM_TASKBAR_HEIGHT - 100;
    
    if (w > 1400) w = 1400;
    if (h > 1000) h = 1000;
    if (w < 800) w = 800;
    if (h < 600) h = 600;

    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;
    
    Window *win = wm_create_window("Blaze Browser", x, y, w, h);
    if (!win) return;
    
    /* Initialize browser */
    blaze_init(&browser_state);
    blaze_load_bookmarks(&browser_state);
    
    /* Navigate to local start page */
    blaze_navigate(&browser_state, "/Documents/index.html");
    
    /* Set callbacks */
    win->on_draw = blaze_on_draw;
    win->on_key = blaze_on_key;
    win->on_mouse = blaze_on_mouse;
    win->on_close = blaze_on_close;
}
