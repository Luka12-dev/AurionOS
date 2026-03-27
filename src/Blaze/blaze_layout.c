/*
 * Blaze Browser - Layout Engine
 * Computes positions and sizes for DOM nodes
*/

#include "blaze.h"

/* Check if node is block-level */
static bool is_block_element(DOMNode *node) {
    if (!node || node->type != NODE_ELEMENT) return false;
    
    /* Check CSS display property */
    /* Note: In our current simple model, everything is either block or inline. */
    /* We don't have inline-block yet, but we'll treat nodes with display_block as block. */
    
    const char *tag = node->tag;
    if (blaze_str_cmp(tag, "div") == 0 ||
        blaze_str_cmp(tag, "p") == 0 ||
        blaze_str_cmp(tag, "h1") == 0 ||
        blaze_str_cmp(tag, "h2") == 0 ||
        blaze_str_cmp(tag, "h3") == 0 ||
        blaze_str_cmp(tag, "h4") == 0 ||
        blaze_str_cmp(tag, "h5") == 0 ||
        blaze_str_cmp(tag, "h6") == 0 ||
        blaze_str_cmp(tag, "body") == 0 ||
        blaze_str_cmp(tag, "html") == 0 ||
        blaze_str_cmp(tag, "nav") == 0 ||
        blaze_str_cmp(tag, "header") == 0 ||
        blaze_str_cmp(tag, "footer") == 0 ||
        blaze_str_cmp(tag, "section") == 0 ||
        blaze_str_cmp(tag, "article") == 0 ||
        blaze_str_cmp(tag, "main") == 0 ||
        blaze_str_cmp(tag, "aside") == 0 ||
        blaze_str_cmp(tag, "ul") == 0 ||
        blaze_str_cmp(tag, "li") == 0) {
        return true;
    }
    
    return false;
}

/* Layout a single node */
void blaze_layout_node(DOMNode *node, int parent_x, int parent_y, int parent_w) {
    if (!node) return;
    
    /* Text nodes */
    if (node->type == NODE_TEXT) {
        int text_len = blaze_str_len(node->text);
        node->x = parent_x;
        node->y = parent_y;
        node->w = text_len * 8;
        node->h = 16;
        return;
    }
    
    /* Element nodes */
    if (node->type != NODE_ELEMENT) return;
    
    /* Apply margins */
    int left_margin = node->margin[3];
    int right_margin = node->margin[1];
    
    /* Support for margin: auto (very basic) */
    if (node->fixed_w > 0 && node->fixed_w < parent_w) {
        /* If both margins are large, assume it's margin: auto */
        /* Our parser currently sets all margins to the same value if 'margin: X' is used,
           but let's check if we can detect 'auto' or just center fixed-width blocks by default 
           if they have a certain ID/class for now, or just improve the parser later. */
        if (blaze_str_cmp(node->id, "card") == 0 || blaze_str_cmp(node->id, "anim_card") == 0) {
            left_margin = (parent_w - node->fixed_w) / 2;
        }
    }

    int x = parent_x + left_margin;
    int y = parent_y + node->margin[0]; /* top margin */
    
    /* Apply padding */
    int content_x = x + node->padding[3];
    int content_y = y + node->padding[0];
    int content_w = (node->fixed_w > 0 ? node->fixed_w : parent_w) - node->padding[1] - node->padding[3];
    
    node->x = x;
    node->y = y;
    
    /* Block elements take full width */
    if (is_block_element(node)) {
        node->w = (node->fixed_w > 0) ? node->fixed_w : (parent_w - left_margin - right_margin);
        
        /* Layout children */
        int child_y = content_y;
        DOMNode *child = node->first_child;
        while (child) {
            if (is_block_element(child)) {
                /* Regular block child gets its own row */
                blaze_layout_node(child, content_x, child_y, content_w);
                if (node->text_align == 1) {
                    int dx = (content_w - child->w) / 2;
                    blaze_offset_node(child, dx, 0);
                } else {
                    blaze_offset_node(child, child->margin[3], 0);
                }
                blaze_offset_node(child, 0, child->margin[0]);
                child_y += child->h + child->margin[0] + child->margin[2];
                child = child->next_sibling;
            } else {
                /* Inline run: group all consecutive inline siblings on this line */
                int line_h = 16;
                int child_x = content_x;
                DOMNode *line_start = child;
                while (child && !is_block_element(child)) {
                    blaze_layout_node(child, child_x, child_y, content_w - (child_x - content_x));
                    
                    /* Positioning */
                    child->x = child_x + child->margin[3];
                    child->y = child_y + child->margin[0];
                    if (child->h + child->margin[0] + child->margin[2] > line_h) {
                        line_h = child->h + child->margin[0] + child->margin[2];
                    }

                    /* Simple wrap if exceeds width */
                    if (child_x + child->w + child->margin[3] + child->margin[1] > content_x + content_w) {
                        child_x = content_x;
                        child_y += line_h;
                        child->x = child_x + child->margin[3];
                        child->y = child_y + child->margin[0];
                    }
                    
                    child_x += child->w + child->margin[3] + child->margin[1];
                    child = child->next_sibling;
                }
                
                /* Centering for the inline run if needed */
                if (node->text_align == 1) {
                    int run_w = child_x - content_x;
                    if (run_w > content_w) run_w = content_w;
                    int offset = (content_w - run_w) / 2;
                    DOMNode *l = line_start;
                    while (l != child) {
                        blaze_offset_node(l, offset, 0);
                        l = l->next_sibling;
                    }
                }
                
                child_y += line_h;
            }
        }
        
        /* Calculate total height */
        int total_h = child_y - content_y;
        node->h = (node->fixed_h > 0) ? node->fixed_h : (total_h + node->padding[0] + node->padding[2]);
    }
    /* Simple Inline elements (e.g. text nodes that are not inside a block) */
    else {
        int child_x = content_x;
        int child_y = content_y;
        int line_h = 16;
        int max_w = 0;
        
        DOMNode *child = node->first_child;
        DOMNode *line_start = child;
        while (child) {
            blaze_layout_node(child, child_x, child_y, content_w - (child_x - content_x));
            
            /* Apply child margins for positioning */
            child->x = child_x + child->margin[3];
            child->y = child_y + child->margin[0];

            if (child->h + child->margin[0] + child->margin[2] > line_h) {
                line_h = child->h + child->margin[0] + child->margin[2];
            }

            /* Wrap to next line if needed */
            if (child_x + child->w + child->margin[3] + child->margin[1] > content_x + content_w) {
                /* Handle centering for the finished line */
                if (node->text_align == 1) {
                    int line_w = child_x - content_x;
                    int offset = (content_w - line_w) / 2;
                    DOMNode *l = line_start;
                    while (l != child) {
                        blaze_offset_node(l, offset, 0);
                        l = l->next_sibling;
                    }
                }
                
                child_x = content_x;
                child_y += line_h;
                /* Already positioned by recursively inside the loop, reset starting x/y */
                /* Wait, in the second pass we'll update it properly */
                line_start = child;
            }
            
            child_x += child->w + child->margin[3] + child->margin[1];
            if (child_x - content_x > max_w) max_w = child_x - content_x;
            
            child = child->next_sibling;
            
            /* Handle centering for the very last line */
            if (!child && node->text_align == 1) {
                int line_w = child_x - content_x;
                int offset = (content_w - line_w) / 2;
                DOMNode *l = line_start;
                while (l) {
                    blaze_offset_node(l, offset, 0);
                    l = l->next_sibling;
                }
            }
        }
        
        node->w = (node->fixed_w > 0) ? node->fixed_w : (max_w + node->padding[1] + node->padding[3]);
        node->h = (node->fixed_h > 0) ? node->fixed_h : ((child_y - content_y) + line_h + node->padding[0] + node->padding[2]);
    }
}

/* Rekursively offset a node and its children */
void blaze_offset_node(DOMNode *node, int dx, int dy) {
    if (!node) return;
    node->x += dx;
    node->y += dy;
    DOMNode *child = node->first_child;
    while (child) {
        blaze_offset_node(child, dx, dy);
        child = child->next_sibling;
    }
}

/* Layout entire page */
void blaze_layout(BlazeTab *tab, int viewport_w, int viewport_h) {
    if (!tab->document) {
        return;
    }
    
    /* Find the body element */
    DOMNode *body = NULL;
    DOMNode *html_node = NULL;
    
    /* First check if document has body as direct child */
    DOMNode *child = tab->document->first_child;
    while (child) {
        if (child->type == NODE_ELEMENT) {
            if (blaze_str_cmp(child->tag, "html") == 0) {
                html_node = child;
            }
            if (blaze_str_cmp(child->tag, "body") == 0) {
                body = child;
            }
        }
        child = child->next_sibling;
    }
    
    /* If we found html, look for body inside it */
    if (html_node) {
        child = html_node->first_child;
        while (child) {
            if (child->type == NODE_ELEMENT && blaze_str_cmp(child->tag, "body") == 0) {
                body = child;
                break;
            }
            child = child->next_sibling;
        }
    }
    
    /* If no body found, use first element (fallback) */
    if (!body) {
        body = tab->document->first_child;
    }
    
    if (!body) {
        return;
    }
    
    /* Layout body to fill full viewport width */
    body->w = viewport_w;
    blaze_layout_node(body, 0, 0, viewport_w);
    
    /* Ensure body fills viewport height */
    if (body->h < viewport_h) {
        body->h = viewport_h;
    }
    
    /* Center the document if body has children that are smaller than body */
}
