/*
 * Blaze Browser - CSS Engine
 * Parses CSS and applies styles to DOM
*/

#include "blaze.h"

/* Apply default browser styles */
void blaze_apply_default_styles(BlazeTab *tab) {
    /* Default body style */
    CSSRule *rule = &tab->rules[tab->rule_count++];
    blaze_str_copy(rule->selector, "body", 128);
    rule->bg_color = 0xFF000000;
    rule->fg_color = 0xFFFFFFFF;
    rule->padding[0] = rule->padding[1] = rule->padding[2] = rule->padding[3] = 8;
    rule->margin[0] = rule->margin[1] = rule->margin[2] = rule->margin[3] = 0;
    rule->font_size = 14;
    rule->display_block = true;
    
    /* Headings */
    const char *headings[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
    int sizes[] = {32, 24, 20, 18, 16, 14};
    for (int i = 0; i < 6 && tab->rule_count < BLAZE_MAX_CSS_RULES; i++) {
        rule = &tab->rules[tab->rule_count++];
        blaze_str_copy(rule->selector, headings[i], 128);
        rule->fg_color = 0xFFFFFFFF;
        rule->font_size = sizes[i];
        rule->margin[0] = rule->margin[2] = 16;
        rule->display_block = true;
    }
    
    /* Paragraph */
    rule = &tab->rules[tab->rule_count++];
    blaze_str_copy(rule->selector, "p", 128);
    rule->margin[0] = rule->margin[2] = 12;
    rule->fg_color = 0xFFCCCCCC;
    rule->display_block = true;
    
    /* Links */
    rule = &tab->rules[tab->rule_count++];
    blaze_str_copy(rule->selector, "a", 128);
    rule->fg_color = 0xFF4ADE80; /* Nice green for links */
    rule->margin[1] = 20; /* right margin */
    rule->margin[3] = 20; /* left margin */
    
    /* Div */
    rule = &tab->rules[tab->rule_count++];
    blaze_str_copy(rule->selector, "div", 128);
    rule->display_block = true;

    /* Structural tags */
    const char *structural[] = {"header", "footer", "nav", "section", "article", "main", "aside"};
    for (int i = 0; i < 7 && tab->rule_count < BLAZE_MAX_CSS_RULES; i++) {
        rule = &tab->rules[tab->rule_count++];
        blaze_str_copy(rule->selector, structural[i], 128);
        rule->display_block = true;
    }
}

/* Check if selector matches node */
bool blaze_selector_matches(DOMNode *node, const char *selector) {
    if (!node || !selector) return false;
    
    /* Tag selector */
    if (selector[0] != '.' && selector[0] != '#') {
        return blaze_str_cmp(node->tag, selector) == 0;
    }
    
    /* ID selector */
    if (selector[0] == '#') {
        return blaze_str_cmp(node->id, selector + 1) == 0;
    }
    
    /* Class selector */
    if (selector[0] == '.') {
        /* Check if class is in class_list */
        const char *class_name = selector + 1;
        const char *p = node->class_list;
        while (*p) {
            const char *c = class_name;
            while (*c && *p == *c) { c++; p++; }
            if (*c == 0 && (*p == ' ' || *p == 0)) return true;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
    }
    
    return false;
}

/* Apply styles to node */
static void apply_style_to_node(DOMNode *node, CSSRule *rule) {
    if (rule->bg_color != 0) node->bg_color = rule->bg_color;
    if (rule->fg_color != 0) node->fg_color = rule->fg_color;
    if (rule->border_width > 0) {
        node->border_width = rule->border_width;
        node->border_color = rule->border_color;
    }
    for (int i = 0; i < 4; i++) {
        if (rule->padding[i] > 0) node->padding[i] = rule->padding[i];
        if (rule->margin[i] > 0) node->margin[i] = rule->margin[i];
    }
}

/* Recursively apply styles to DOM tree */
static void apply_styles_recursive(DOMNode *node, CSSRule *rules, int rule_count) {
    if (!node) return;
    
    /* Apply matching rules */
    for (int i = 0; i < rule_count; i++) {
        if (blaze_selector_matches(node, rules[i].selector)) {
            apply_style_to_node(node, &rules[i]);
        }
    }
    
    /* Recurse to children */
    DOMNode *child = node->first_child;
    while (child) {
        apply_styles_recursive(child, rules, rule_count);
        child = child->next_sibling;
    }
}

/* Apply all styles to DOM */
void blaze_apply_styles(BlazeTab *tab) {
    /* Apply default styles first */
    blaze_apply_default_styles(tab);
    
    /* Apply custom rules */
    apply_styles_recursive(tab->document, tab->rules, tab->rule_count);
}

/* Helper: Skip whitespace */
static const char *skip_whitespace(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* Helper: Parse integer value */
static int parse_int(const char **p) {
    int val = 0;
    bool negative = false;
    if (**p == '-') { negative = true; (*p)++; }
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return negative ? -val : val;
}

/* Helper: Extract property name */
static void extract_property(const char **p, char *prop, int max) {
    int i = 0;
    while (**p && **p != ':' && **p != ';' && **p != '}' && i < max - 1) {
        if (**p != ' ' && **p != '\t' && **p != '\n' && **p != '\r') {
            prop[i++] = **p;
        }
        (*p)++;
    }
    prop[i] = 0;
}

/* Helper: Extract property value */
static void extract_value(const char **p, char *val, int max) {
    *p = skip_whitespace(*p);
    if (**p == ':') (*p)++;
    *p = skip_whitespace(*p);
    
    int i = 0;
    while (**p && **p != ';' && **p != '}' && i < max - 1) {
        val[i++] = **p;
        (*p)++;
    }
    
    /* Trim trailing whitespace */
    while (i > 0 && (val[i-1] == ' ' || val[i-1] == '\t' || val[i-1] == '\n' || val[i-1] == '\r')) i--;
    val[i] = 0;
    
    if (**p == ';') (*p)++;
}

/* Helper: Parse padding/margin shorthand */
static void parse_box_values(const char *val, int *box) {
    const char *p = val;
    int values[4] = {0, 0, 0, 0};
    int count = 0;
    
    /* Parse up to 4 values */
    while (*p && count < 4) {
        p = skip_whitespace(p);
        if (*p >= '0' && *p <= '9') {
            values[count++] = parse_int(&p);
            /* Skip 'px' or other units */
            while (*p && *p != ' ' && *p != '\t') p++;
        } else {
            break;
        }
    }
    
    /* Apply CSS box model rules */
    if (count == 1) {
        /* All sides */
        box[0] = box[1] = box[2] = box[3] = values[0];
    } else if (count == 2) {
        /* top/bottom, left/right */
        box[0] = box[2] = values[0];
        box[1] = box[3] = values[1];
    } else if (count == 3) {
        /* top, left/right, bottom */
        box[0] = values[0];
        box[1] = box[3] = values[1];
        box[2] = values[2];
    } else if (count == 4) {
        /* top, right, bottom, left */
        box[0] = values[0];
        box[1] = values[1];
        box[2] = values[2];
        box[3] = values[3];
    }
}

/* Helper: Apply property to rule */
static void apply_property(CSSRule *rule, const char *prop, const char *val) {
    /* Background color */
    if (blaze_str_cmp(prop, "background-color") == 0 || blaze_str_cmp(prop, "background") == 0) {
        rule->bg_color = blaze_parse_color(val);
    }
    /* Text color */
    else if (blaze_str_cmp(prop, "color") == 0) {
        rule->fg_color = blaze_parse_color(val);
    }
    /* Padding */
    else if (blaze_str_cmp(prop, "padding") == 0) {
        parse_box_values(val, rule->padding);
    }
    else if (blaze_str_cmp(prop, "padding-top") == 0) {
        const char *p = val;
        rule->padding[0] = parse_int(&p);
    }
    else if (blaze_str_cmp(prop, "padding-right") == 0) {
        const char *p = val;
        rule->padding[1] = parse_int(&p);
    }
    else if (blaze_str_cmp(prop, "padding-bottom") == 0) {
        const char *p = val;
        rule->padding[2] = parse_int(&p);
    }
    else if (blaze_str_cmp(prop, "padding-left") == 0) {
        const char *p = val;
        rule->padding[3] = parse_int(&p);
    }
    /* Margin */
    else if (blaze_str_cmp(prop, "margin") == 0) {
        parse_box_values(val, rule->margin);
    }
    else if (blaze_str_cmp(prop, "margin-top") == 0) {
        const char *p = val;
        rule->margin[0] = parse_int(&p);
    }
    else if (blaze_str_cmp(prop, "margin-right") == 0) {
        const char *p = val;
        rule->margin[1] = parse_int(&p);
    }
    else if (blaze_str_cmp(prop, "margin-bottom") == 0) {
        const char *p = val;
        rule->margin[2] = parse_int(&p);
    }
    else if (blaze_str_cmp(prop, "margin-left") == 0) {
        const char *p = val;
        rule->margin[3] = parse_int(&p);
    }
    /* Border */
    else if (blaze_str_cmp(prop, "border-width") == 0) {
        const char *p = val;
        rule->border_width = parse_int(&p);
    }
    else if (blaze_str_cmp(prop, "border-color") == 0) {
        rule->border_color = blaze_parse_color(val);
    }
    else if (blaze_str_cmp(prop, "border") == 0) {
        /* Parse "1px solid #000" format */
        const char *p = val;
        p = skip_whitespace(p);
        if (*p >= '0' && *p <= '9') {
            rule->border_width = parse_int(&p);
            /* Skip 'px' */
            while (*p && *p != ' ') p++;
            p = skip_whitespace(p);
            /* Skip 'solid', 'dashed', etc */
            while (*p && *p != ' ' && *p != '#') p++;
            p = skip_whitespace(p);
            /* Parse color */
            if (*p) rule->border_color = blaze_parse_color(p);
        }
    }
    /* Font size */
    else if (blaze_str_cmp(prop, "font-size") == 0) {
        const char *p = val;
        rule->font_size = parse_int(&p);
    }
    /* Display */
    else if (blaze_str_cmp(prop, "display") == 0) {
        if (blaze_str_cmp(val, "block") == 0) {
            rule->display_block = true;
        } else if (blaze_str_cmp(val, "inline") == 0) {
            rule->display_block = false;
        }
    }
}

/* Parse CSS */
void blaze_parse_css(BlazeTab *tab, const char *css, uint32_t len) {
    if (!css || len == 0 || tab->rule_count >= BLAZE_MAX_CSS_RULES) return;
    
    const char *p = css;
    const char *end = css + len;
    
    while (p < end && *p && tab->rule_count < BLAZE_MAX_CSS_RULES) {
        /* Skip whitespace and comments */
        p = skip_whitespace(p);
        
        /* Skip CSS comments */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p < end && !(p[0] == '*' && p[1] == '/')) p++;
            if (p < end) p += 2;
            continue;
        }
        
        if (*p == 0 || p >= end) break;
        
        /* Extract selector */
        char selector[128] = {0};
        int sel_idx = 0;
        while (*p && *p != '{' && sel_idx < 127) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                selector[sel_idx++] = *p;
            }
            p++;
        }
        selector[sel_idx] = 0;
        
        if (*p != '{') break;
        p++; /* Skip '{' */
        
        /* Create new rule */
        CSSRule *rule = &tab->rules[tab->rule_count];
        blaze_str_copy(rule->selector, selector, 128);
        
        /* Initialize rule with defaults */
        rule->bg_color = 0;
        rule->fg_color = 0;
        rule->padding[0] = rule->padding[1] = rule->padding[2] = rule->padding[3] = 0;
        rule->margin[0] = rule->margin[1] = rule->margin[2] = rule->margin[3] = 0;
        rule->border_width = 0;
        rule->border_color = 0;
        rule->font_size = 0;
        rule->display_block = false;
        
        /* Parse properties */
        while (*p && *p != '}' && p < end) {
            p = skip_whitespace(p);
            if (*p == '}') break;
            
            /* Extract property name */
            char prop[64] = {0};
            extract_property(&p, prop, 64);
            
            /* Extract property value */
            char val[256] = {0};
            extract_value(&p, val, 256);
            
            /* Apply property to rule */
            if (prop[0] && val[0]) {
                apply_property(rule, prop, val);
            }
            
            p = skip_whitespace(p);
        }
        
        if (*p == '}') p++; /* Skip '}' */
        
        /* Only add rule if selector is valid */
        if (selector[0]) {
            tab->rule_count++;
        }
    }
}
