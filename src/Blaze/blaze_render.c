/*
 * Blaze Browser - Rendering Engine
 * Paints DOM nodes to screen
*/

#include "blaze.h"
#include "../window_manager.h"

/* Paint a single node */
void blaze_paint_node(DOMNode *node, void *window, int scroll_y, int offset_y) {
    if (!node) return;
    
    Window *win = (Window *)window;
    int screen_y = node->y - scroll_y + offset_y;
    
    /* CRITICAL: Never draw above offset_y (the navbar area) */
    if (screen_y < offset_y && screen_y + node->h < offset_y) {
        /* Node is entirely above the content area - skip completely including children */
        return;
    }
    
    /* Calculate viewport bounds - content area only, not including navbar */
    int viewport_bottom = offset_y + (wm_client_h(win) - offset_y);
    
    /* Skip rendering this node's own visuals if it's entirely off-screen.
     * BUT: always recurse into children of element containers, because
     * a parent div might be partially off-screen while its children
     * are still visible in the viewport. Only skip leaf text nodes. */
    bool self_visible = true;
    if (node->w > 0 && node->h > 0) {
        if (screen_y + node->h < offset_y || screen_y > viewport_bottom) {
            if (node->type == NODE_TEXT) {
                return; /* Text leaf is fully off-screen - safe to skip */
            }
            self_visible = false; /* Container off-screen but still check children */
        }
    }
    
    /* Only draw this node's own visuals if it's on-screen and not above navbar */
    if (self_visible && screen_y >= offset_y - node->h) {
        /* Draw background - clip to content area */
        if ((node->bg_color & 0xFF000000) != 0) {
            uint32_t bg = node->bg_color;
            int draw_y = screen_y;
            int draw_h = node->h;
            
            /* Clip top edge if drawing into navbar */
            if (draw_y < offset_y) {
                int clip_amount = offset_y - draw_y;
                draw_y = offset_y;
                draw_h -= clip_amount;
            }
            
            if (draw_h > 0) {
                if (node->border_radius > 0) {
                    int r = node->border_radius;
                    if (r > node->w / 2) r = node->w / 2;
                    if (r > node->h / 2) r = node->h / 2;
                    
                    wm_fill_rect(win, node->x + r, draw_y, node->w - r * 2, draw_h, bg);
                    wm_fill_rect(win, node->x, draw_y + r, node->w, draw_h - r * 2, bg);
                    
                    wm_fill_rect(win, node->x, draw_y, r, r, bg);
                    wm_fill_rect(win, node->x + node->w - r, draw_y, r, r, bg);
                    wm_fill_rect(win, node->x, draw_y + draw_h - r, r, r, bg);
                    wm_fill_rect(win, node->x + node->w - r, draw_y + draw_h - r, r, r, bg);
                } else {
                    wm_fill_rect(win, node->x, draw_y, node->w, draw_h, bg);
                }
            }
        }
        
        /* Draw border */
        if (node->border_width > 0 && screen_y >= offset_y) {
            for (int i = 0; i < node->border_width; i++) {
                wm_draw_rect(win, node->x + i, screen_y + i, 
                            node->w - i * 2, node->h - i * 2, node->border_color);
            }
        }
        
        /* Draw text content */
        if (node->type == NODE_TEXT && node->text[0]) {
            int text_x = node->x;
            int text_y = screen_y;
            
            /* Word wrap */
            int max_w = node->w;
            if (max_w <= 0) max_w = 1000; /* Fallback */
            int line_x = 0;
            
            for (int i = 0; node->text[i]; i++) {
                char c = node->text[i];
                
                if (c == '\n') {
                    line_x = 0;
                    text_y += 16;
                    continue;
                }
                
                if (line_x + 8 > max_w) {
                    line_x = 0;
                    text_y += 16;
                }
                
                /* Only draw if within content area */
                if (text_y >= offset_y && text_y < wm_client_h(win)) {
                    wm_draw_char(win, text_x + line_x, text_y, (uint8_t)c, 
                               node->fg_color, 0);
                }
                
                line_x += 8;
            }
        }
    }
    
    /* Always recurse to children - they might be visible even if parent isn't */
    DOMNode *child = node->first_child;
    while (child) {
        blaze_paint_node(child, window, scroll_y, offset_y);
        child = child->next_sibling;
    }
}

/* Paint entire page */
void blaze_paint(BlazeTab *tab, void *window, int scroll_y, int viewport_h) {
    Window *win = (Window *)window;
    int content_y = 40 + 28; /* Toolbar + Tab bar */
    int client_w = wm_client_w(win);
    
    /* Catch-all scroll clamp: find the true content bottom by scanning
     * all leaf nodes, then prevent scrolling past it. This runs every
     * frame so scroll_y is ALWAYS valid, no matter how it was modified. */
    {
        int max_bottom = 0;
        for (int i = 0; i < tab->node_count; i++) {
            DOMNode *node = &tab->nodes[i];
            if (node->w <= 0 || node->h <= 0) continue;
            if (node->type == NODE_TEXT || 
                (node->type == NODE_ELEMENT && 
                 blaze_str_cmp(node->tag, "body") != 0 &&
                 blaze_str_cmp(node->tag, "html") != 0 &&
                 blaze_str_cmp(node->tag, "document") != 0)) {
                int bottom = node->y + node->h;
                if (bottom > max_bottom) max_bottom = bottom;
            }
        }
        int max_scroll = (max_bottom > viewport_h) ? (max_bottom - viewport_h) : 0;
        if (scroll_y > max_scroll) scroll_y = max_scroll;
        if (scroll_y < 0) scroll_y = 0;
        tab->scroll_y = scroll_y; /* Write back so keyboard/mouse handlers see it */
    }
    
    /* Clear content area background - use viewport_h to avoid overdrawing navbar */
    wm_fill_rect(win, 0, content_y, client_w, viewport_h, 0xFF000000);
    
    if (!tab->document) {
        wm_draw_string(win, 120, content_y + 20, "ERROR: No document", 0xFF000000, 0);
        return;
    }
    
    /* Find the body element */
    DOMNode *body = NULL;
    DOMNode *html_node = NULL;
    
    /* First check if document has body as direct child */
    DOMNode *child = tab->document->first_child;
    int s1 = 0;
    while (child && s1++ < 200) {
        if (child->type == NODE_ELEMENT) {
            if (blaze_str_cmp(child->tag, "html") == 0) html_node = child;
            if (blaze_str_cmp(child->tag, "body") == 0) body = child;
        }
        child = child->next_sibling;
    }
    
    if (html_node) {
        child = html_node->first_child;
        int s2 = 0;
        while (child && s2++ < 200) {
            if (child->type == NODE_ELEMENT && blaze_str_cmp(child->tag, "body") == 0) {
                body = child;
                break;
            }
            child = child->next_sibling;
        }
    }
    
    if (!body) {
        body = tab->document->first_child;
    }
    
    if (!body) {
        char msg[256];
        int pos = 0;
        const char *err = "ERROR: No body. Nodes: ";
        for (int i = 0; err[i]; i++) msg[pos++] = err[i];
        
        uint32_t nc = (uint32_t)tab->node_count;
        if (nc == 0) { msg[pos++] = '0'; }
        else {
            char num[16]; int np = 0;
            while (nc > 0) { num[np++] = '0' + (nc % 10); nc /= 10; }
            while (np > 0) msg[pos++] = num[--np];
        }
        
        const char *len_msg = ", HTML len: ";
        for (int i = 0; len_msg[i]; i++) msg[pos++] = len_msg[i];
        
        uint32_t hl = tab->html_len;
        if (hl == 0) { msg[pos++] = '0'; }
        else {
            char num[16]; int np = 0;
            while (hl > 0) { num[np++] = '0' + (hl % 10); hl /= 10; }
            while (np > 0) msg[pos++] = num[--np];
        }
        msg[pos] = 0;
        
        wm_draw_string(win, 20, content_y + 40, msg, 0xFFFF0000, 0);
        return;
    }
    
    /* Paint the body and all its children */
    blaze_paint_node(body, window, scroll_y, content_y);
}
