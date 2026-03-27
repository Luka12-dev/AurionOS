#include "blaze.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

/* Helper: Skip whitespace */
static const char *skip_whitespace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Helper: Check if character is valid in a tag name */
static bool is_tag_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* Create a new element node */
DOMNode *blaze_create_element(BlazeTab *tab, const char *tag) {
    if (tab->node_count >= BLAZE_MAX_DOM_NODES) return NULL;
    DOMNode *node = &tab->nodes[tab->node_count++];
    for (int i = 0; i < (int)sizeof(DOMNode); i++) ((char *)node)[i] = 0;
    node->type = NODE_ELEMENT;
    blaze_str_copy(node->tag, tag, 32);
    node->bg_color = 0; /* Transparent by default */
    node->fg_color = 0xFFFFFFFF; /* White by default for AurionOS Dark Theme */
    node->fixed_w = node->fixed_h = 0;
    node->border_radius = 0;
    node->font_size = 14;
    return node;
}

/* Create a new text node */
DOMNode *blaze_create_text(BlazeTab *tab, const char *text) {
    if (tab->node_count >= BLAZE_MAX_DOM_NODES) return NULL;
    DOMNode *node = &tab->nodes[tab->node_count++];
    for (int i = 0; i < (int)sizeof(DOMNode); i++) ((char *)node)[i] = 0;
    node->type = NODE_TEXT;
    blaze_str_copy(node->text, text, 256);
    node->fg_color = 0xFFFFFFFF; /* White by default */
    return node;
}

/* Append child to parent */
void blaze_append_child(DOMNode *parent, DOMNode *child) {
    if (!parent || !child) return;
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    }
}

/* Parse attributes in a tag */
static void parse_attributes(DOMNode *node, const char *p) {
    while (*p && *p != '>') {
        p = skip_whitespace(p);
        if (*p == '>' || *p == '/') break;
        
        /* Attribute name */
        char attr_name[64];
        int ai = 0;
        while (*p && *p != '=' && *p != ' ' && *p != '>' && ai < 63) attr_name[ai++] = *p++;
        attr_name[ai] = 0;
        
        p = skip_whitespace(p);
        if (*p != '=') continue;
        p++; /* skip '=' */
        p = skip_whitespace(p);
        
        /* Attribute value */
        char quote = 0;
        if (*p == '"' || *p == '\'') quote = *p++;
        
        char attr_value[256];
        int vi = 0;
        if (quote) {
            while (*p && *p != quote && vi < 255) attr_value[vi++] = *p++;
            if (*p == quote) p++;
        } else {
            while (*p && *p != ' ' && *p != '>' && vi < 255) attr_value[vi++] = *p++;
        }
        attr_value[vi] = 0;
        
        /* Apply attributes */
        if (blaze_str_cmp(attr_name, "id") == 0) {
            blaze_str_copy(node->id, attr_value, 64);
        } else if (blaze_str_cmp(attr_name, "class") == 0) {
            blaze_str_copy(node->class_list, attr_value, 128);
        } else if (blaze_str_cmp(attr_name, "href") == 0) {
            blaze_str_copy(node->href, attr_value, 256);
        } else if (blaze_str_cmp(attr_name, "src") == 0) {
            blaze_str_copy(node->src, attr_value, 256);
        } else if (blaze_str_cmp(attr_name, "alt") == 0) {
            blaze_str_copy(node->alt, attr_value, 64);
        } else if (blaze_str_cmp(attr_name, "onclick") == 0) {
            blaze_str_copy(node->onclick_script, attr_value, 256);
        } else if (blaze_str_cmp(attr_name, "style") == 0) {
            /* Parse inline style for background, color, and more */
            const char *s = attr_value;
            while (*s) {
                while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
                if (*s == 0) break;

                if (blaze_str_starts_with(s, "background:")) {
                    s += 11; while (*s == ' ' || *s == '\t') s++;
                    char col[32]; int ci = 0;
                    while (*s && *s != ';' && *s != '"' && *s != '\'' && ci < 31) col[ci++] = *s++;
                    col[ci] = 0;
                    node->bg_color = blaze_parse_color(col);
                } else if (blaze_str_starts_with(s, "color:")) {
                    s += 6; while (*s == ' ' || *s == '\t') s++;
                    char col[32]; int ci = 0;
                    while (*s && *s != ';' && *s != '"' && *s != '\'' && ci < 31) col[ci++] = *s++;
                    col[ci] = 0;
                    node->fg_color = blaze_parse_color(col);
                } else if (blaze_str_starts_with(s, "font-size:")) {
                    s += 10; while (*s == ' ' || *s == '\t') s++;
                    int val = 0;
                    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                    node->font_size = val;
                } else if (blaze_str_starts_with(s, "padding:")) {
                    s += 8; while (*s == ' ' || *s == '\t') s++;
                    int val = 0;
                    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                    for (int i = 0; i < 4; i++) node->padding[i] = val;
                } else if (blaze_str_starts_with(s, "margin:")) {
                    s += 7; while (*s == ' ' || *s == '\t') s++;
                    int val = 0;
                    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                    for (int i = 0; i < 4; i++) node->margin[i] = val;
                } else if (blaze_str_starts_with(s, "border-radius:")) {
                    s += 14; while (*s == ' ' || *s == '\t') s++;
                    int val = 0;
                    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                    node->border_radius = val;
                } else if (blaze_str_starts_with(s, "border-color:")) {
                    s += 13; while (*s == ' ' || *s == '\t') s++;
                    char col[32]; int ci = 0;
                    while (*s && *s != ';' && *s != '"' && *s != '\'' && ci < 31) col[ci++] = *s++;
                    col[ci] = 0;
                    node->border_color = blaze_parse_color(col);
                } else if (blaze_str_starts_with(s, "border:")) {
                    s += 7; while (*s == ' ' || *s == '\t') s++;
                    /* Parse width */
                    int val = 0;
                    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                    node->border_width = val;
                    if (*s == 'p' && *(s+1) == 'x') s += 2;
                    while (*s == ' ' || *s == '\t') s++;
                    /* Skip style (solid, etc) */
                    while (*s && *s != ' ' && *s != ';' && *s != '"' && *s != '\'') s++;
                    while (*s == ' ' || *s == '\t') s++;
                    /* Parse color */
                    char col[32]; int ci = 0;
                    while (*s && *s != ';' && *s != '"' && *s != '\'' && ci < 31) col[ci++] = *s++;
                    col[ci] = 0;
                    if (ci > 0) node->border_color = blaze_parse_color(col);
                } else if (blaze_str_starts_with(s, "width:")) {
                    s += 6; while (*s == ' ' || *s == '\t') s++;
                    int val = 0;
                    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                    node->fixed_w = val;
                } else if (blaze_str_starts_with(s, "height:")) {
                    s += 7; while (*s == ' ' || *s == '\t') s++;
                    int val = 0;
                    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                    node->fixed_h = val;
                } else if (blaze_str_starts_with(s, "text-align:")) {
                    s += 11; while (*s == ' ' || *s == '\t') s++;
                    if (blaze_str_starts_with(s, "center")) node->text_align = 1;
                }
                
                int safety = 0;
                while (*s && *s != ';' && safety++ < 500) s++;
                if (*s == ';') s++;
            }
        }
    }
}

/* Check if tag is self-closing */
static bool is_self_closing(const char *tag) {
    if (blaze_str_cmp(tag, "img") == 0) return true;
    if (blaze_str_cmp(tag, "br") == 0) return true;
    if (blaze_str_cmp(tag, "hr") == 0) return true;
    if (blaze_str_cmp(tag, "input") == 0) return true;
    if (blaze_str_cmp(tag, "meta") == 0) return true;
    if (blaze_str_cmp(tag, "link") == 0) return true;
    return false;
}

/* Parse HTML string into DOM */
void blaze_parse_html(BlazeTab *tab, const char *html, uint32_t len) {
    tab->node_count = 0;
    tab->document = blaze_create_element(tab, "document");
    blaze_str_copy(tab->document->tag, "document", 32);
    
    /* Debug: Check if we have any HTML content */
    if (!html || len == 0) {
        /* Create a fallback body with error message */
        DOMNode *body = blaze_create_element(tab, "body");
        if (body) {
            body->bg_color = 0xFF000000; /* Black background */
            blaze_append_child(tab->document, body);
            
            DOMNode *text = blaze_create_text(tab, "ERROR: No HTML content loaded");
            if (text) {
                text->fg_color = 0xFFFF0000; /* Red text */
                blaze_append_child(body, text);
            }
        }
        return;
    }
    
    DOMNode *stack[32];
    int stack_top = 0;
    stack[stack_top++] = tab->document;
    
    const char *p = html;
    const char *end = html + len;
    
    while (p < end) {
        p = skip_whitespace(p);
        if (p >= end) break;
        
        if (*p == '<') {
            p++;
            if (p >= end) break;
            
            /* Comment */
            if (blaze_str_starts_with(p, "!--")) {
                p += 3;
                while (p < end) {
                    if (blaze_str_starts_with(p, "-->")) {
                        p += 3;
                        break;
                    }
                    p++;
                }
                continue;
            }
            
            /* Doctype */
            if (*p == '!') {
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }
            
            /* Closing tag */
            if (*p == '/') {
                p++;
                /* 
                char tag[32];
                int tag_len = 0;
                while (is_tag_char(*p) && tag_len < 31) {
                    tag[tag_len++] = (*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p;
                    p++;
                }
                tag[tag_len] = 0;
                */
                while (is_tag_char(*p)) p++;

                /* Pop from stack */
                if (stack_top > 1) {
                    stack_top--;
                }
                
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }
            
            /* Opening tag */
            char tag[32];
            int tag_len = 0;
            while (is_tag_char(*p) && tag_len < 31) {
                tag[tag_len++] = (*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p;
                p++;
            }
            tag[tag_len] = 0;
            
            /* Handle script, style, and title tags */
            if (blaze_str_cmp(tag, "script") == 0 || blaze_str_cmp(tag, "style") == 0 || blaze_str_cmp(tag, "title") == 0) {
                const char *content_start = p;
                while (content_start < end && *content_start != '>') content_start++;
                if (content_start < end && *content_start == '>') content_start++;
                
                const char *content_end = content_start;
                /* Find closing tag */
                while (content_end < end) {
                    if (*content_end == '<' && content_end[1] == '/') {
                        const char *check = content_end + 2;
                        bool match = true;
                        for (int i = 0; tag[i]; i++) {
                            if ((*check >= 'A' && *check <= 'Z' ? *check + 32 : *check) != tag[i]) {
                                match = false;
                                break;
                            }
                            check++;
                        }
                        if (match) break;
                    }
                    content_end++;
                }
                
                if (blaze_str_cmp(tag, "script") == 0 && content_end > content_start) {
                    int script_len = content_end - content_start;
                    char *script_buf = (char *)kmalloc(script_len + 1);
                    if (script_buf) {
                        for (int i = 0; i < script_len; i++) script_buf[i] = content_start[i];
                        script_buf[script_len] = 0;
                        blaze_js_execute(tab, script_buf);
                        kfree(script_buf);
                    }
                } else if (blaze_str_cmp(tag, "title") == 0 && content_end > content_start) {
                    int title_len = content_end - content_start;
                    if (title_len >= BLAZE_MAX_TITLE_LEN) title_len = BLAZE_MAX_TITLE_LEN - 1;
                    for (int i = 0; i < title_len; i++) tab->title[i] = content_start[i];
                    tab->title[title_len] = 0;
                }
                
                p = content_end;
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }
            
            DOMNode *node = blaze_create_element(tab, tag);
            if (node) {
                /* Inherit styles from parent */
                DOMNode *parent = stack[stack_top - 1];
                if (parent) {
                    node->fg_color = parent->fg_color;
                    node->font_size = parent->font_size;
                }

                blaze_append_child(parent, node);
                parse_attributes(node, p);
                
                if (!is_self_closing(tag)) {
                    if (stack_top < 31) stack[stack_top++] = node;
                }
            }
            
            while (p < end && *p != '>') p++;
            if (p < end) p++;
        } else {
            /* Text node */
            char text[256];
            int ti = 0;
            while (p < end && *p != '<' && ti < 255) {
                text[ti++] = *p++;
            }
            text[ti] = 0;
            
            /* Only add if not just whitespace */
            bool has_content = false;
            for (int i = 0; i < ti; i++) {
                if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
                    has_content = true;
                    break;
                }
            }
            
            if (has_content) {
                DOMNode *node = blaze_create_text(tab, text);
                if (node) {
                    /* Inherit styles from parent */
                    DOMNode *parent = stack[stack_top - 1];
                    if (parent) {
                        node->fg_color = parent->fg_color;
                    }
                    blaze_append_child(parent, node);
                }
            }
        }
    }
}
