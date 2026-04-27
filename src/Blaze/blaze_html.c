#include "blaze.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern void kmemset(void *ptr, uint8_t val, uint32_t size);
extern void kmemcpy(void *dst, const void *src, uint32_t size);

/* STRING & WHITESPACE HELPERS */

/* Helper: Skip whitespace */
static const char *skip_whitespace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Helper: Check if character is valid in a tag name */
static bool is_tag_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

/* Helper: Check if character is a digit */
static bool is_digit(char c) {
    return (c >= '0' && c <= '9');
}

/* Helper: Parse integer from string, advancing pointer */
static int parse_int(const char **pp) {
    const char *s = *pp;
    int val = 0;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    /* Skip optional unit suffixes */
    if (*s == 'p' && *(s + 1) == 'x') s += 2;
    else if (*s == 'e' && *(s + 1) == 'm') s += 2;
    else if (*s == '%') s++;
    else if (*s == 'p' && *(s + 1) == 't') s += 2;
    else if (*s == 'v' && (*(s + 1) == 'w' || *(s + 1) == 'h')) s += 2;
    else if (*s == 'r' && *(s + 1) == 'e' && *(s + 2) == 'm') s += 3;
    *pp = s;
    return neg ? -val : val;
}

/* Helper: Convert uppercase char to lowercase */
static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

/* Helper: Case-insensitive string compare for tags */
static bool tag_equals(const char *a, const char *b) {
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

/* HTML ENTITY DECODER */

/* Decode a single HTML entity, returns decoded char count */
static int decode_entity(const char *p, char *out, int max_out) {
    if (*p != '&') return 0;
    p++;

    struct { const char *name; const char *replacement; } entities[] = {
        {"amp;",    "&"},
        {"lt;",     "<"},
        {"gt;",     ">"},
        {"quot;",   "\""},
        {"apos;",   "'"},
        {"nbsp;",   " "},
        {"copy;",   "(c)"},
        {"reg;",    "(R)"},
        {"trade;",  "(TM)"},
        {"mdash;",  "--"},
        {"ndash;",  "-"},
        {"laquo;",  "<<"},
        {"raquo;",  ">>"},
        {"bull;",   "*"},
        {"hellip;", "..."},
        {"euro;",   "EUR"},
        {"pound;",  "GBP"},
        {"yen;",    "JPY"},
        {"cent;",   "c"},
        {"sect;",   "S"},
        {"para;",   "P"},
        {"deg;",    "deg"},
        {"plusmn;", "+/-"},
        {"times;",  "x"},
        {"divide;", "/"},
        {"frac12;", "1/2"},
        {"frac14;", "1/4"},
        {"frac34;", "3/4"},
        {"larr;",   "<-"},
        {"rarr;",   "->"},
        {"uarr;",   "^"},
        {"darr;",   "v"},
        {"hearts;", "<3"},
        {"diams;",  "<>"},
        {"spades;", "S"},
        {"clubs;",  "C"},
        {NULL, NULL}
    };

    for (int i = 0; entities[i].name != NULL; i++) {
        const char *n = entities[i].name;
        const char *q = p;
        bool match = true;
        while (*n) {
            if (*q != *n) { match = false; break; }
            n++; q++;
        }
        if (match) {
            const char *r = entities[i].replacement;
            int len = 0;
            while (*r && len < max_out - 1) { out[len++] = *r++; }
            return len;
        }
    }

    /* Numeric entity &#123; or &#x1F; */
    if (*p == '#') {
        p++;
        int codepoint = 0;
        if (*p == 'x' || *p == 'X') {
            p++;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                codepoint *= 16;
                if (*p >= '0' && *p <= '9') codepoint += *p - '0';
                else if (*p >= 'a' && *p <= 'f') codepoint += *p - 'a' + 10;
                else codepoint += *p - 'A' + 10;
                p++;
            }
        } else {
            while (*p >= '0' && *p <= '9') { codepoint = codepoint * 10 + (*p - '0'); p++; }
        }
        if (codepoint > 0 && codepoint < 128 && max_out > 0) {
            out[0] = (char)codepoint;
            return 1;
        } else if (max_out > 0) {
            out[0] = '?';
            return 1;
        }
    }

    /* Unknown entity, output as-is */
    if (max_out > 0) { out[0] = '&'; return 1; }
    return 0;
}

/* Decode all entities in a text buffer in-place */
static void decode_entities_inplace(char *text) {
    char *read = text;
    char *write = text;
    while (*read) {
        if (*read == '&') {
            char decoded[8];
            int len = decode_entity(read, decoded, 8);
            if (len > 0) {
                /* Find end of entity */
                const char *eend = read + 1;
                while (*eend && *eend != ';') eend++;
                if (*eend == ';') eend++;
                for (int i = 0; i < len; i++) *write++ = decoded[i];
                read = (char *)eend;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = 0;
}

/* NODE CREATION */

/* Create a new element node */
DOMNode *blaze_create_element(BlazeTab *tab, const char *tag) {
    if (tab->node_count >= BLAZE_MAX_DOM_NODES) return NULL;
    DOMNode *node = &tab->nodes[tab->node_count++];
    for (int i = 0; i < (int)sizeof(DOMNode); i++) ((char *)node)[i] = 0;
    node->type = NODE_ELEMENT;
    blaze_str_copy(node->tag, tag, 32);
    node->bg_color = 0;            /* Transparent by default */
    node->fg_color = 0xFFFFFFFF;   /* White text - AurionOS Dark Theme */
    node->fixed_w = node->fixed_h = 0;
    node->border_radius = 0;
    node->border_width = 0;
    node->border_color = 0;
    node->font_size = 14;
    node->text_align = 0;
    node->display = DISPLAY_BLOCK;
    node->visibility = 1;
    node->opacity = 255;
    node->z_index = 0;
    node->overflow_hidden = 0;
    node->is_bold = 0;
    node->is_italic = 0;
    node->is_underline = 0;
    node->is_strikethrough = 0;
    node->list_type = LIST_NONE;
    node->list_index = 0;
    node->colspan = 1;
    node->rowspan = 1;
    node->is_disabled = 0;
    node->is_checked = 0;
    node->is_readonly = 0;
    node->is_hidden = 0;
    node->input_type = INPUT_TEXT;
    node->scroll_x = 0;
    node->scroll_y = 0;
    return node;
}

/* Create a new text node */
DOMNode *blaze_create_text(BlazeTab *tab, const char *text) {
    if (tab->node_count >= BLAZE_MAX_DOM_NODES) return NULL;
    DOMNode *node = &tab->nodes[tab->node_count++];
    for (int i = 0; i < (int)sizeof(DOMNode); i++) ((char *)node)[i] = 0;
    node->type = NODE_TEXT;
    blaze_str_copy(node->text, text, 256);
    decode_entities_inplace(node->text);
    node->fg_color = 0xFFFFFFFF;
    node->font_size = 14;
    node->display = DISPLAY_INLINE;
    node->visibility = 1;
    node->opacity = 255;
    return node;
}

/* Create a comment node (stored but not rendered) */
DOMNode *blaze_create_comment(BlazeTab *tab, const char *comment_text) {
    if (tab->node_count >= BLAZE_MAX_DOM_NODES) return NULL;
    DOMNode *node = &tab->nodes[tab->node_count++];
    for (int i = 0; i < (int)sizeof(DOMNode); i++) ((char *)node)[i] = 0;
    node->type = NODE_COMMENT;
    blaze_str_copy(node->text, comment_text, 256);
    node->visibility = 0;
    node->display = DISPLAY_NONE;
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
    parent->child_count++;
}

/* Insert child before a reference node */
void blaze_insert_before(DOMNode *parent, DOMNode *child, DOMNode *ref) {
    if (!parent || !child) return;
    if (!ref) { blaze_append_child(parent, child); return; }

    child->parent = parent;
    child->next_sibling = ref;
    child->prev_sibling = ref->prev_sibling;
    if (ref->prev_sibling) {
        ref->prev_sibling->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    ref->prev_sibling = child;
    parent->child_count++;
}

/* Remove child from parent */
void blaze_remove_child(DOMNode *parent, DOMNode *child) {
    if (!parent || !child || child->parent != parent) return;

    if (child->prev_sibling) child->prev_sibling->next_sibling = child->next_sibling;
    else parent->first_child = child->next_sibling;

    if (child->next_sibling) child->next_sibling->prev_sibling = child->prev_sibling;
    else parent->last_child = child->prev_sibling;

    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;
    parent->child_count--;
}

/* TAG CLASSIFICATION - EVERY HTML TAG */

/* Display type for a given tag */
typedef enum {
    TAG_CAT_BLOCK,
    TAG_CAT_INLINE,
    TAG_CAT_TABLE,
    TAG_CAT_TABLE_ROW,
    TAG_CAT_TABLE_CELL,
    TAG_CAT_LIST,
    TAG_CAT_LIST_ITEM,
    TAG_CAT_FORM,
    TAG_CAT_HEADING,
    TAG_CAT_MEDIA,
    TAG_CAT_METADATA,
    TAG_CAT_SECTIONING,
    TAG_CAT_INTERACTIVE,
    TAG_CAT_EMBEDDED,
    TAG_CAT_SCRIPTING,
    TAG_CAT_VOID,
    TAG_CAT_UNKNOWN
} TagCategory;

typedef struct {
    const char *name;
    TagCategory category;
    bool self_closing;
    bool is_block;
    bool is_formatting;
    uint8_t default_display;  /* DISPLAY_BLOCK, DISPLAY_INLINE, etc. */
} TagInfo;

/* Comprehensive tag database covering all HTML5 tags */
static const TagInfo tag_database[] = {
    /* Document structure */
    {"html",        TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"head",        TAG_CAT_METADATA,   false, true,  false, DISPLAY_NONE},
    {"body",        TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},

    /* Metadata */
    {"title",       TAG_CAT_METADATA,   false, false, false, DISPLAY_NONE},
    {"meta",        TAG_CAT_METADATA,   true,  false, false, DISPLAY_NONE},
    {"link",        TAG_CAT_METADATA,   true,  false, false, DISPLAY_NONE},
    {"base",        TAG_CAT_METADATA,   true,  false, false, DISPLAY_NONE},
    {"style",       TAG_CAT_METADATA,   false, false, false, DISPLAY_NONE},

    /* Sectioning */
    {"header",      TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"footer",      TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"nav",         TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"main",        TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"section",     TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"article",     TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"aside",       TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"search",      TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},
    {"hgroup",      TAG_CAT_SECTIONING, false, true,  false, DISPLAY_BLOCK},

    /* Headings */
    {"h1",          TAG_CAT_HEADING,    false, true,  false, DISPLAY_BLOCK},
    {"h2",          TAG_CAT_HEADING,    false, true,  false, DISPLAY_BLOCK},
    {"h3",          TAG_CAT_HEADING,    false, true,  false, DISPLAY_BLOCK},
    {"h4",          TAG_CAT_HEADING,    false, true,  false, DISPLAY_BLOCK},
    {"h5",          TAG_CAT_HEADING,    false, true,  false, DISPLAY_BLOCK},
    {"h6",          TAG_CAT_HEADING,    false, true,  false, DISPLAY_BLOCK},

    /* Block content */
    {"div",         TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"p",           TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"pre",         TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"blockquote",  TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"address",     TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"figure",      TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"figcaption",  TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"dialog",      TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"details",     TAG_CAT_INTERACTIVE,false, true,  false, DISPLAY_BLOCK},
    {"summary",     TAG_CAT_INTERACTIVE,false, true,  false, DISPLAY_BLOCK},
    {"template",    TAG_CAT_BLOCK,      false, true,  false, DISPLAY_NONE},

    /* Separators */
    {"hr",          TAG_CAT_VOID,       true,  true,  false, DISPLAY_BLOCK},
    {"br",          TAG_CAT_VOID,       true,  false, false, DISPLAY_INLINE},
    {"wbr",         TAG_CAT_VOID,       true,  false, false, DISPLAY_INLINE},

    /* Inline text semantics */
    {"span",        TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"a",           TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"strong",      TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"b",           TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"em",          TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"i",           TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"u",           TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"s",           TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"del",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"ins",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"mark",        TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"small",       TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"sub",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"sup",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"abbr",        TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"cite",        TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"q",           TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"dfn",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"code",        TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"var",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"samp",        TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"kbd",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"time",        TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"data",        TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"bdi",         TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"bdo",         TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"ruby",        TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"rb",          TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"rtc",         TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"rt",          TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"rp",          TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"output",      TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},

    /* Lists */
    {"ul",          TAG_CAT_LIST,       false, true,  false, DISPLAY_BLOCK},
    {"ol",          TAG_CAT_LIST,       false, true,  false, DISPLAY_BLOCK},
    {"li",          TAG_CAT_LIST_ITEM,  false, true,  false, DISPLAY_LIST_ITEM},
    {"dl",          TAG_CAT_LIST,       false, true,  false, DISPLAY_BLOCK},
    {"dt",          TAG_CAT_LIST_ITEM,  false, true,  false, DISPLAY_BLOCK},
    {"dd",          TAG_CAT_LIST_ITEM,  false, true,  false, DISPLAY_BLOCK},
    {"menu",        TAG_CAT_LIST,       false, true,  false, DISPLAY_BLOCK},

    /* Tables */
    {"table",       TAG_CAT_TABLE,      false, true,  false, DISPLAY_TABLE},
    {"thead",       TAG_CAT_TABLE,      false, true,  false, DISPLAY_TABLE_ROW_GROUP},
    {"tbody",       TAG_CAT_TABLE,      false, true,  false, DISPLAY_TABLE_ROW_GROUP},
    {"tfoot",       TAG_CAT_TABLE,      false, true,  false, DISPLAY_TABLE_ROW_GROUP},
    {"tr",          TAG_CAT_TABLE_ROW,  false, true,  false, DISPLAY_TABLE_ROW},
    {"th",          TAG_CAT_TABLE_CELL, false, true,  false, DISPLAY_TABLE_CELL},
    {"td",          TAG_CAT_TABLE_CELL, false, true,  false, DISPLAY_TABLE_CELL},
    {"caption",     TAG_CAT_TABLE,      false, true,  false, DISPLAY_TABLE_CAPTION},
    {"colgroup",    TAG_CAT_TABLE,      false, true,  false, DISPLAY_TABLE_COLUMN_GROUP},
    {"col",         TAG_CAT_VOID,       true,  false, false, DISPLAY_TABLE_COLUMN},

    /* Forms */
    {"form",        TAG_CAT_FORM,       false, true,  false, DISPLAY_BLOCK},
    {"input",       TAG_CAT_VOID,       true,  false, false, DISPLAY_INLINE},
    {"textarea",    TAG_CAT_FORM,       false, false, false, DISPLAY_INLINE},
    {"button",      TAG_CAT_FORM,       false, false, false, DISPLAY_INLINE},
    {"select",      TAG_CAT_FORM,       false, false, false, DISPLAY_INLINE},
    {"option",      TAG_CAT_FORM,       false, false, false, DISPLAY_BLOCK},
    {"optgroup",    TAG_CAT_FORM,       false, false, false, DISPLAY_BLOCK},
    {"label",       TAG_CAT_FORM,       false, false, false, DISPLAY_INLINE},
    {"fieldset",    TAG_CAT_FORM,       false, true,  false, DISPLAY_BLOCK},
    {"legend",      TAG_CAT_FORM,       false, true,  false, DISPLAY_BLOCK},
    {"datalist",    TAG_CAT_FORM,       false, false, false, DISPLAY_NONE},
    {"meter",       TAG_CAT_FORM,       false, false, false, DISPLAY_INLINE},
    {"progress",    TAG_CAT_FORM,       false, false, false, DISPLAY_INLINE},

    /* Media */
    {"img",         TAG_CAT_VOID,       true,  false, false, DISPLAY_INLINE},
    {"audio",       TAG_CAT_MEDIA,      false, false, false, DISPLAY_INLINE},
    {"video",       TAG_CAT_MEDIA,      false, false, false, DISPLAY_INLINE},
    {"source",      TAG_CAT_VOID,       true,  false, false, DISPLAY_NONE},
    {"track",       TAG_CAT_VOID,       true,  false, false, DISPLAY_NONE},
    {"picture",     TAG_CAT_MEDIA,      false, false, false, DISPLAY_INLINE},
    {"canvas",      TAG_CAT_EMBEDDED,   false, false, false, DISPLAY_INLINE},
    {"svg",         TAG_CAT_EMBEDDED,   false, false, false, DISPLAY_INLINE},
    {"map",         TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"area",        TAG_CAT_VOID,       true,  false, false, DISPLAY_NONE},

    /* Embedded */
    {"iframe",      TAG_CAT_EMBEDDED,   false, false, false, DISPLAY_INLINE},
    {"embed",       TAG_CAT_VOID,       true,  false, false, DISPLAY_INLINE},
    {"object",      TAG_CAT_EMBEDDED,   false, false, false, DISPLAY_INLINE},
    {"param",       TAG_CAT_VOID,       true,  false, false, DISPLAY_NONE},

    /* Scripting */
    {"script",      TAG_CAT_SCRIPTING,  false, false, false, DISPLAY_NONE},
    {"noscript",    TAG_CAT_SCRIPTING,  false, false, false, DISPLAY_NONE},
    {"slot",        TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},

    /* Deprecated but still parsed for compat */
    {"center",      TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"font",        TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"big",         TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"tt",          TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"strike",      TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"marquee",     TAG_CAT_BLOCK,      false, true,  false, DISPLAY_BLOCK},
    {"blink",       TAG_CAT_INLINE,     false, false, true,  DISPLAY_INLINE},
    {"nobr",        TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"frame",       TAG_CAT_VOID,       true,  false, false, DISPLAY_NONE},
    {"frameset",    TAG_CAT_BLOCK,      false, true,  false, DISPLAY_NONE},
    {"noframes",    TAG_CAT_BLOCK,      false, true,  false, DISPLAY_NONE},
    {"applet",      TAG_CAT_EMBEDDED,   false, false, false, DISPLAY_INLINE},
    {"acronym",     TAG_CAT_INLINE,     false, false, false, DISPLAY_INLINE},
    {"dir",         TAG_CAT_LIST,       false, true,  false, DISPLAY_BLOCK},

    {NULL, TAG_CAT_UNKNOWN, false, false, false, DISPLAY_INLINE}
};

/* Lookup tag info from database */
static const TagInfo *lookup_tag(const char *tag) {
    for (int i = 0; tag_database[i].name != NULL; i++) {
        if (tag_equals(tag, tag_database[i].name)) {
            return &tag_database[i];
        }
    }
    return NULL;
}

/* Check if tag is self-closing */
static bool is_self_closing(const char *tag) {
    const TagInfo *info = lookup_tag(tag);
    if (info) return info->self_closing;
    return false;
}

/* Check if tag is a raw text element (script/style) */
static bool is_raw_text_element(const char *tag) {
    return tag_equals(tag, "script") || tag_equals(tag, "style");
}

/* Check if tag implies closing another tag (optional end tags) */
static bool tag_auto_closes(const char *new_tag, const char *open_tag) {
    /* <p> is auto-closed by block elements */
    if (tag_equals(open_tag, "p")) {
        if (tag_equals(new_tag, "p") || tag_equals(new_tag, "div") ||
            tag_equals(new_tag, "h1") || tag_equals(new_tag, "h2") ||
            tag_equals(new_tag, "h3") || tag_equals(new_tag, "h4") ||
            tag_equals(new_tag, "h5") || tag_equals(new_tag, "h6") ||
            tag_equals(new_tag, "ul") || tag_equals(new_tag, "ol") ||
            tag_equals(new_tag, "table") || tag_equals(new_tag, "pre") ||
            tag_equals(new_tag, "blockquote") || tag_equals(new_tag, "hr") ||
            tag_equals(new_tag, "header") || tag_equals(new_tag, "footer") ||
            tag_equals(new_tag, "nav") || tag_equals(new_tag, "section") ||
            tag_equals(new_tag, "article") || tag_equals(new_tag, "aside") ||
            tag_equals(new_tag, "main") || tag_equals(new_tag, "figure") ||
            tag_equals(new_tag, "address") || tag_equals(new_tag, "fieldset") ||
            tag_equals(new_tag, "form") || tag_equals(new_tag, "details"))
            return true;
    }
    /* <li> is auto-closed by another <li> */
    if (tag_equals(open_tag, "li") && tag_equals(new_tag, "li")) return true;
    /* <dt>/<dd> auto-close each other */
    if ((tag_equals(open_tag, "dt") || tag_equals(open_tag, "dd")) &&
        (tag_equals(new_tag, "dt") || tag_equals(new_tag, "dd"))) return true;
    /* <tr> auto-closes another <tr> */
    if (tag_equals(open_tag, "tr") && tag_equals(new_tag, "tr")) return true;
    /* <td>/<th> auto-close each other */
    if ((tag_equals(open_tag, "td") || tag_equals(open_tag, "th")) &&
        (tag_equals(new_tag, "td") || tag_equals(new_tag, "th") || tag_equals(new_tag, "tr")))
        return true;
    /* <thead>/<tbody>/<tfoot> auto-close each other */
    if ((tag_equals(open_tag, "thead") || tag_equals(open_tag, "tbody") || tag_equals(open_tag, "tfoot")) &&
        (tag_equals(new_tag, "thead") || tag_equals(new_tag, "tbody") || tag_equals(new_tag, "tfoot")))
        return true;
    /* <option> auto-closed by another <option> or <optgroup> */
    if (tag_equals(open_tag, "option") && (tag_equals(new_tag, "option") || tag_equals(new_tag, "optgroup")))
        return true;
    /* <optgroup> auto-closed by another <optgroup> */
    if (tag_equals(open_tag, "optgroup") && tag_equals(new_tag, "optgroup")) return true;
    /* <head> auto-closed by <body> */
    if (tag_equals(open_tag, "head") && tag_equals(new_tag, "body")) return true;

    return false;
}

/* DEFAULT STYLING BY TAG */

/* Apply default visual styles based on the tag name */
static void apply_default_tag_styles(DOMNode *node, const char *tag) {
    const TagInfo *info = lookup_tag(tag);
    if (info) {
        node->display = info->default_display;
    } else {
        /* WHATWG: unknown HTML tags default to inline */
        node->display = DISPLAY_INLINE;
    }

    /* Headings */
    if (tag_equals(tag, "h1")) {
        node->font_size = 32; node->is_bold = 1;
        for (int i = 0; i < 4; i++) { node->margin[i] = (i < 2) ? 21 : 0; }
    } else if (tag_equals(tag, "h2")) {
        node->font_size = 24; node->is_bold = 1;
        for (int i = 0; i < 4; i++) { node->margin[i] = (i < 2) ? 19 : 0; }
    } else if (tag_equals(tag, "h3")) {
        node->font_size = 19; node->is_bold = 1;
        for (int i = 0; i < 4; i++) { node->margin[i] = (i < 2) ? 18 : 0; }
    } else if (tag_equals(tag, "h4")) {
        node->font_size = 16; node->is_bold = 1;
        for (int i = 0; i < 4; i++) { node->margin[i] = (i < 2) ? 21 : 0; }
    } else if (tag_equals(tag, "h5")) {
        node->font_size = 13; node->is_bold = 1;
        for (int i = 0; i < 4; i++) { node->margin[i] = (i < 2) ? 22 : 0; }
    } else if (tag_equals(tag, "h6")) {
        node->font_size = 11; node->is_bold = 1;
        for (int i = 0; i < 4; i++) { node->margin[i] = (i < 2) ? 25 : 0; }
    }
    /* Paragraph */
    else if (tag_equals(tag, "p")) {
        node->margin[0] = 16; node->margin[1] = 16; /* top/bottom margin */
    }
    /* Bold/Strong */
    else if (tag_equals(tag, "b") || tag_equals(tag, "strong")) {
        node->is_bold = 1;
    }
    /* Italic/Emphasis */
    else if (tag_equals(tag, "i") || tag_equals(tag, "em") || tag_equals(tag, "cite") ||
             tag_equals(tag, "dfn") || tag_equals(tag, "var")) {
        node->is_italic = 1;
    }
    /* Underline */
    else if (tag_equals(tag, "u") || tag_equals(tag, "ins")) {
        node->is_underline = 1;
    }
    /* Strikethrough */
    else if (tag_equals(tag, "s") || tag_equals(tag, "del") || tag_equals(tag, "strike")) {
        node->is_strikethrough = 1;
    }
    /* Code/Pre */
    else if (tag_equals(tag, "code") || tag_equals(tag, "samp") || tag_equals(tag, "kbd") || tag_equals(tag, "tt")) {
        node->font_size = 13;
        node->bg_color = 0xFF2D2D2D;  /* Dark code background */
        for (int i = 0; i < 4; i++) node->padding[i] = 2;
    }
    else if (tag_equals(tag, "pre")) {
        node->font_size = 13;
        node->bg_color = 0xFF1E1E1E;
        for (int i = 0; i < 4; i++) node->padding[i] = 10;
        node->margin[0] = 16; node->margin[1] = 16;
        node->overflow_hidden = 0;  /* Allow scroll */
    }
    /* Mark (highlight) */
    else if (tag_equals(tag, "mark")) {
        node->bg_color = 0xFFFFFF00;  /* Yellow highlight */
        node->fg_color = 0xFF000000;  /* Black text on yellow */
    }
    /* Small */
    else if (tag_equals(tag, "small")) {
        node->font_size = 11;
    }
    /* Big (deprecated) */
    else if (tag_equals(tag, "big")) {
        node->font_size = 18;
    }
    /* Subscript/Superscript */
    else if (tag_equals(tag, "sub") || tag_equals(tag, "sup")) {
        node->font_size = 10;
    }
    /* Links */
    else if (tag_equals(tag, "a")) {
        node->fg_color = 0xFF6CB4EE;  /* AurionOS link color */
        node->is_underline = 1;
    }
    /* Horizontal rule */
    else if (tag_equals(tag, "hr")) {
        node->fixed_h = 2;
        node->bg_color = 0xFF555555;
        node->margin[0] = 8; node->margin[1] = 8;
    }
    /* Lists */
    else if (tag_equals(tag, "ul") || tag_equals(tag, "menu") || tag_equals(tag, "dir")) {
        node->list_type = LIST_UNORDERED;
        node->padding[2] = 40;  /* Left padding */
        node->margin[0] = 16; node->margin[1] = 16;
    }
    else if (tag_equals(tag, "ol")) {
        node->list_type = LIST_ORDERED;
        node->padding[2] = 40;
        node->margin[0] = 16; node->margin[1] = 16;
    }
    else if (tag_equals(tag, "li")) {
        node->display = DISPLAY_LIST_ITEM;
    }
    else if (tag_equals(tag, "dd")) {
        node->padding[2] = 40;  /* Indent */
    }
    /* Blockquote */
    else if (tag_equals(tag, "blockquote")) {
        node->padding[2] = 20;  /* Left indent */
        node->margin[0] = 16; node->margin[1] = 16;
        node->margin[2] = 40; node->margin[3] = 40;
        node->border_width = 3;
        node->border_color = 0xFF555555;
    }
    /* Table elements */
    else if (tag_equals(tag, "table")) {
        node->border_width = 1;
        node->border_color = 0xFF444444;
    }
    else if (tag_equals(tag, "th")) {
        node->is_bold = 1;
        node->text_align = 1;  /* Center */
        for (int i = 0; i < 4; i++) node->padding[i] = 6;
        node->border_width = 1;
        node->border_color = 0xFF444444;
        node->bg_color = 0xFF2A2A2A;
    }
    else if (tag_equals(tag, "td")) {
        for (int i = 0; i < 4; i++) node->padding[i] = 6;
        node->border_width = 1;
        node->border_color = 0xFF333333;
    }
    /* Form elements */
    else if (tag_equals(tag, "button")) {
        node->bg_color = 0xFF3A3A3A;
        node->fg_color = 0xFFFFFFFF;
        node->border_radius = 4;
        node->border_width = 1;
        node->border_color = 0xFF555555;
        for (int i = 0; i < 4; i++) node->padding[i] = 6;
    }
    else if (tag_equals(tag, "input")) {
        node->bg_color = 0xFF2A2A2A;
        node->fg_color = 0xFFFFFFFF;
        node->border_width = 1;
        node->border_color = 0xFF555555;
        node->border_radius = 3;
        for (int i = 0; i < 4; i++) node->padding[i] = 4;
    }
    else if (tag_equals(tag, "textarea")) {
        node->bg_color = 0xFF2A2A2A;
        node->fg_color = 0xFFFFFFFF;
        node->border_width = 1;
        node->border_color = 0xFF555555;
        for (int i = 0; i < 4; i++) node->padding[i] = 4;
        node->fixed_w = 300; node->fixed_h = 100;
    }
    else if (tag_equals(tag, "select")) {
        node->bg_color = 0xFF2A2A2A;
        node->fg_color = 0xFFFFFFFF;
        node->border_width = 1;
        node->border_color = 0xFF555555;
        for (int i = 0; i < 4; i++) node->padding[i] = 4;
    }
    else if (tag_equals(tag, "fieldset")) {
        node->border_width = 1;
        node->border_color = 0xFF555555;
        for (int i = 0; i < 4; i++) node->padding[i] = 10;
        node->margin[0] = 8; node->margin[1] = 8;
    }
    else if (tag_equals(tag, "legend")) {
        node->is_bold = 1;
        for (int i = 0; i < 4; i++) node->padding[i] = 4;
    }
    /* Media placeholders */
    else if (tag_equals(tag, "img")) {
        node->display = DISPLAY_INLINE;
    }
    else if (tag_equals(tag, "video") || tag_equals(tag, "audio")) {
        node->bg_color = 0xFF1A1A1A;
        node->border_width = 1;
        node->border_color = 0xFF333333;
    }
    else if (tag_equals(tag, "canvas")) {
        node->bg_color = 0xFF000000;
    }
    else if (tag_equals(tag, "iframe")) {
        node->bg_color = 0xFF1A1A1A;
        node->border_width = 1;
        node->border_color = 0xFF555555;
        node->fixed_w = 300; node->fixed_h = 150;
    }
    /* Progress/Meter */
    else if (tag_equals(tag, "progress")) {
        node->bg_color = 0xFF333333;
        node->fixed_w = 160; node->fixed_h = 16;
        node->border_radius = 8;
    }
    else if (tag_equals(tag, "meter")) {
        node->bg_color = 0xFF333333;
        node->fixed_w = 80; node->fixed_h = 16;
        node->border_radius = 8;
    }
    /* Details/Summary */
    else if (tag_equals(tag, "details")) {
        node->border_width = 1;
        node->border_color = 0xFF444444;
        for (int i = 0; i < 4; i++) node->padding[i] = 6;
    }
    else if (tag_equals(tag, "summary")) {
        node->is_bold = 1;
    }
    /* Center (deprecated) */
    else if (tag_equals(tag, "center")) {
        node->text_align = 1;
    }
    /* Dialog */
    else if (tag_equals(tag, "dialog")) {
        node->bg_color = 0xFF2D2D2D;
        node->border_width = 1;
        node->border_color = 0xFF555555;
        node->border_radius = 8;
        for (int i = 0; i < 4; i++) node->padding[i] = 16;
    }
    /* Address */
    else if (tag_equals(tag, "address")) {
        node->is_italic = 1;
    }
    /* Abbreviation - dotted underline */
    else if (tag_equals(tag, "abbr")) {
        node->is_underline = 1;  /* Simplified; real browsers use dotted underline */
    }
    /* Quotation */
    else if (tag_equals(tag, "q")) {
        /* Will be handled during rendering to add quotes */
    }

    /* Body default for dark theme */
    if (tag_equals(tag, "body")) {
        node->bg_color = 0xFF1A1A2E;  /* AurionOS dark background */
        node->fg_color = 0xFFE0E0E0;  /* Light gray text */
        for (int i = 0; i < 4; i++) node->padding[i] = 8;
    }
}

/* ATTRIBUTE PARSING */

/* Parse input type attribute */
static uint8_t parse_input_type(const char *type_str) {
    if (tag_equals(type_str, "text"))     return INPUT_TEXT;
    if (tag_equals(type_str, "password")) return INPUT_PASSWORD;
    if (tag_equals(type_str, "email"))    return INPUT_EMAIL;
    if (tag_equals(type_str, "number"))   return INPUT_NUMBER;
    if (tag_equals(type_str, "tel"))      return INPUT_TEL;
    if (tag_equals(type_str, "url"))      return INPUT_URL;
    if (tag_equals(type_str, "search"))   return INPUT_SEARCH;
    if (tag_equals(type_str, "checkbox")) return INPUT_CHECKBOX;
    if (tag_equals(type_str, "radio"))    return INPUT_RADIO;
    if (tag_equals(type_str, "submit"))   return INPUT_SUBMIT;
    if (tag_equals(type_str, "reset"))    return INPUT_RESET;
    if (tag_equals(type_str, "button"))   return INPUT_BUTTON;
    if (tag_equals(type_str, "hidden"))   return INPUT_HIDDEN;
    if (tag_equals(type_str, "file"))     return INPUT_FILE;
    if (tag_equals(type_str, "image"))    return INPUT_IMAGE;
    if (tag_equals(type_str, "color"))    return INPUT_COLOR;
    if (tag_equals(type_str, "date"))     return INPUT_DATE;
    if (tag_equals(type_str, "time"))     return INPUT_TIME;
    if (tag_equals(type_str, "range"))    return INPUT_RANGE;
    return INPUT_TEXT;
}

/* Parse inline CSS style property */
static void parse_style_property(DOMNode *node, const char *prop, const char *value) {
    if (tag_equals(prop, "background") || tag_equals(prop, "background-color")) {
        node->bg_color = blaze_parse_color(value);
    } else if (tag_equals(prop, "color")) {
        node->fg_color = blaze_parse_color(value);
    } else if (tag_equals(prop, "font-size")) {
        const char *v = value;
        node->font_size = parse_int(&v);
    } else if (tag_equals(prop, "font-weight")) {
        if (tag_equals(value, "bold") || tag_equals(value, "bolder")) node->is_bold = 1;
        else {
            const char *v = value;
            int w = parse_int(&v);
            if (w >= 700) node->is_bold = 1;
        }
    } else if (tag_equals(prop, "font-style")) {
        if (tag_equals(value, "italic") || tag_equals(value, "oblique")) node->is_italic = 1;
    } else if (tag_equals(prop, "text-decoration")) {
        if (blaze_str_starts_with(value, "underline")) node->is_underline = 1;
        else if (blaze_str_starts_with(value, "line-through")) node->is_strikethrough = 1;
        else if (blaze_str_starts_with(value, "none")) {
            node->is_underline = 0; node->is_strikethrough = 0;
        }
    } else if (tag_equals(prop, "text-align")) {
        if (tag_equals(value, "center")) node->text_align = 1;
        else if (tag_equals(value, "right")) node->text_align = 2;
        else if (tag_equals(value, "justify")) node->text_align = 3;
        else node->text_align = 0;
    } else if (tag_equals(prop, "padding")) {
        const char *v = value;
        int val = parse_int(&v);
        for (int i = 0; i < 4; i++) node->padding[i] = val;
    } else if (tag_equals(prop, "padding-top")) {
        const char *v = value; node->padding[0] = parse_int(&v);
    } else if (tag_equals(prop, "padding-bottom")) {
        const char *v = value; node->padding[1] = parse_int(&v);
    } else if (tag_equals(prop, "padding-left")) {
        const char *v = value; node->padding[2] = parse_int(&v);
    } else if (tag_equals(prop, "padding-right")) {
        const char *v = value; node->padding[3] = parse_int(&v);
    } else if (tag_equals(prop, "margin")) {
        const char *v = value;
        int val = parse_int(&v);
        for (int i = 0; i < 4; i++) node->margin[i] = val;
    } else if (tag_equals(prop, "margin-top")) {
        const char *v = value; node->margin[0] = parse_int(&v);
    } else if (tag_equals(prop, "margin-bottom")) {
        const char *v = value; node->margin[1] = parse_int(&v);
    } else if (tag_equals(prop, "margin-left")) {
        const char *v = value; node->margin[2] = parse_int(&v);
    } else if (tag_equals(prop, "margin-right")) {
        const char *v = value; node->margin[3] = parse_int(&v);
    } else if (tag_equals(prop, "border-radius")) {
        const char *v = value; node->border_radius = parse_int(&v);
    } else if (tag_equals(prop, "border-color")) {
        node->border_color = blaze_parse_color(value);
    } else if (tag_equals(prop, "border-width")) {
        const char *v = value; node->border_width = parse_int(&v);
    } else if (tag_equals(prop, "border")) {
        const char *v = value;
        /* Parse: width style color */
        node->border_width = parse_int(&v);
        while (*v == ' ' || *v == '\t') v++;
        /* Skip style word (solid, dashed, dotted, etc.) */
        while (*v && *v != ' ' && *v != ';') v++;
        while (*v == ' ' || *v == '\t') v++;
        /* Parse color */
        if (*v) {
            char col[32]; int ci = 0;
            while (*v && *v != ';' && *v != '"' && *v != '\'' && ci < 31) col[ci++] = *v++;
            col[ci] = 0;
            if (ci > 0) node->border_color = blaze_parse_color(col);
        }
    } else if (tag_equals(prop, "width")) {
        const char *v = value; node->fixed_w = parse_int(&v);
    } else if (tag_equals(prop, "height")) {
        const char *v = value; node->fixed_h = parse_int(&v);
    } else if (tag_equals(prop, "display")) {
        if (tag_equals(value, "none")) node->display = DISPLAY_NONE;
        else if (tag_equals(value, "block")) node->display = DISPLAY_BLOCK;
        else if (tag_equals(value, "inline")) node->display = DISPLAY_INLINE;
        else if (tag_equals(value, "inline-block")) node->display = DISPLAY_INLINE_BLOCK;
        else if (tag_equals(value, "flex")) node->display = DISPLAY_FLEX;
        else if (tag_equals(value, "grid")) node->display = DISPLAY_GRID;
        else if (tag_equals(value, "table")) node->display = DISPLAY_TABLE;
    } else if (tag_equals(prop, "visibility")) {
        node->visibility = tag_equals(value, "hidden") ? 0 : 1;
    } else if (tag_equals(prop, "opacity")) {
        /* Parse float as integer 0-255 */
        const char *v = value;
        int whole = 0;
        while (*v >= '0' && *v <= '9') { whole = whole * 10 + (*v - '0'); v++; }
        int frac = 0;
        if (*v == '.') {
            v++;
            int div = 10;
            while (*v >= '0' && *v <= '9') { frac += (*v - '0') * 255 / div; div *= 10; v++; }
        }
        node->opacity = (whole >= 1) ? 255 : (uint8_t)(whole * 255 + frac);
    } else if (tag_equals(prop, "overflow")) {
        node->overflow_hidden = tag_equals(value, "hidden") ? 1 : 0;
    } else if (tag_equals(prop, "z-index")) {
        const char *v = value; node->z_index = parse_int(&v);
    } else if (tag_equals(prop, "flex-direction")) {
        if (tag_equals(value, "row")) node->flex_direction = 0;
        else if (tag_equals(value, "column")) node->flex_direction = 1;
        else if (tag_equals(value, "row-reverse")) node->flex_direction = 2;
        else if (tag_equals(value, "column-reverse")) node->flex_direction = 3;
    } else if (tag_equals(prop, "justify-content")) {
        if (tag_equals(value, "flex-start")) node->justify_content = 0;
        else if (tag_equals(value, "center")) node->justify_content = 1;
        else if (tag_equals(value, "flex-end")) node->justify_content = 2;
        else if (tag_equals(value, "space-between")) node->justify_content = 3;
        else if (tag_equals(value, "space-around")) node->justify_content = 4;
        else if (tag_equals(value, "space-evenly")) node->justify_content = 5;
    } else if (tag_equals(prop, "align-items")) {
        if (tag_equals(value, "flex-start") || tag_equals(value, "start")) node->align_items = 0;
        else if (tag_equals(value, "center")) node->align_items = 1;
        else if (tag_equals(value, "flex-end") || tag_equals(value, "end")) node->align_items = 2;
        else if (tag_equals(value, "stretch")) node->align_items = 3;
    } else if (tag_equals(prop, "gap")) {
        const char *v = value; node->gap = parse_int(&v);
    } else if (tag_equals(prop, "flex-wrap")) {
        node->flex_wrap = tag_equals(value, "wrap") ? 1 : 0;
    } else if (tag_equals(prop, "position")) {
        if (tag_equals(value, "relative")) node->position = POS_RELATIVE;
        else if (tag_equals(value, "absolute")) node->position = POS_ABSOLUTE;
        else if (tag_equals(value, "fixed")) node->position = POS_FIXED;
        else if (tag_equals(value, "sticky")) node->position = POS_STICKY;
        else node->position = POS_STATIC;
    } else if (tag_equals(prop, "top")) {
        const char *v = value; node->pos_top = parse_int(&v);
    } else if (tag_equals(prop, "left")) {
        const char *v = value; node->pos_left = parse_int(&v);
    } else if (tag_equals(prop, "right")) {
        const char *v = value; node->pos_right = parse_int(&v);
    } else if (tag_equals(prop, "bottom")) {
        const char *v = value; node->pos_bottom = parse_int(&v);
    } else if (tag_equals(prop, "min-width")) {
        const char *v = value; node->min_w = parse_int(&v);
    } else if (tag_equals(prop, "max-width")) {
        const char *v = value; node->max_w = parse_int(&v);
    } else if (tag_equals(prop, "min-height")) {
        const char *v = value; node->min_h = parse_int(&v);
    } else if (tag_equals(prop, "max-height")) {
        const char *v = value; node->max_h = parse_int(&v);
    } else if (tag_equals(prop, "line-height")) {
        const char *v = value; node->line_height = parse_int(&v);
    } else if (tag_equals(prop, "letter-spacing")) {
        const char *v = value; node->letter_spacing = parse_int(&v);
    } else if (tag_equals(prop, "word-spacing")) {
        const char *v = value; node->word_spacing = parse_int(&v);
    } else if (tag_equals(prop, "text-indent")) {
        const char *v = value; node->text_indent = parse_int(&v);
    } else if (tag_equals(prop, "white-space")) {
        if (tag_equals(value, "nowrap")) node->white_space = WS_NOWRAP;
        else if (tag_equals(value, "pre")) node->white_space = WS_PRE;
        else if (tag_equals(value, "pre-wrap")) node->white_space = WS_PRE_WRAP;
        else node->white_space = WS_NORMAL;
    } else if (tag_equals(prop, "text-transform")) {
        if (tag_equals(value, "uppercase")) node->text_transform = TT_UPPERCASE;
        else if (tag_equals(value, "lowercase")) node->text_transform = TT_LOWERCASE;
        else if (tag_equals(value, "capitalize")) node->text_transform = TT_CAPITALIZE;
        else node->text_transform = TT_NONE;
    } else if (tag_equals(prop, "cursor")) {
        if (tag_equals(value, "pointer")) node->cursor_type = CURSOR_POINTER;
        else if (tag_equals(value, "text")) node->cursor_type = CURSOR_TEXT;
        else if (tag_equals(value, "move")) node->cursor_type = CURSOR_MOVE;
        else if (tag_equals(value, "not-allowed")) node->cursor_type = CURSOR_NOT_ALLOWED;
        else if (tag_equals(value, "crosshair")) node->cursor_type = CURSOR_CROSSHAIR;
        else node->cursor_type = CURSOR_DEFAULT;
    } else if (tag_equals(prop, "box-shadow")) {
        /* Simplified: just note that a shadow exists */
        node->has_shadow = 1;
    } else if (tag_equals(prop, "text-shadow")) {
        node->has_text_shadow = 1;
    } else if (tag_equals(prop, "transform")) {
        /* Store transform string for later */
        blaze_str_copy(node->transform, value, 64);
    } else if (tag_equals(prop, "transition")) {
        node->has_transition = 1;
    } else if (tag_equals(prop, "animation")) {
        node->has_animation = 1;
    } else if (tag_equals(prop, "outline")) {
        /* Parse similarly to border */
        const char *v = value;
        node->outline_width = parse_int(&v);
    } else if (tag_equals(prop, "outline-color")) {
        node->outline_color = blaze_parse_color(value);
    }
}

/* Parse the style attribute value (semicolon-separated properties) */
static void parse_inline_style(DOMNode *node, const char *style_str) {
    const char *s = style_str;
    while (*s) {
        s = skip_whitespace(s);
        if (*s == 0) break;

        /* Extract property name */
        char prop[64];
        int pi = 0;
        while (*s && *s != ':' && *s != ';' && pi < 63) {
            prop[pi++] = to_lower(*s);
            s++;
        }
        prop[pi] = 0;

        if (*s != ':') {
            /* Skip to next property */
            while (*s && *s != ';') s++;
            if (*s == ';') s++;
            continue;
        }
        s++; /* skip ':' */
        s = skip_whitespace(s);

        /* Extract value */
        char value[256];
        int vi = 0;
        while (*s && *s != ';' && *s != '"' && *s != '\'' && vi < 255) {
            value[vi++] = *s++;
        }
        /* Trim trailing whitespace */
        while (vi > 0 && (value[vi - 1] == ' ' || value[vi - 1] == '\t')) vi--;
        value[vi] = 0;

        if (pi > 0 && vi > 0) {
            parse_style_property(node, prop, value);
        }

        if (*s == ';') s++;
    }
}

/* Parse all attributes in a tag */
static void parse_attributes(DOMNode *node, const char *p) {
    while (*p && *p != '>') {
        p = skip_whitespace(p);
        if (*p == '>' || *p == '/' || *p == 0) break;

        /* Attribute name */
        char attr_name[64];
        int ai = 0;
        while (*p && *p != '=' && *p != ' ' && *p != '\t' && *p != '>' && *p != '/' && ai < 63) {
            attr_name[ai++] = to_lower(*p);
            p++;
        }
        attr_name[ai] = 0;

        p = skip_whitespace(p);

        /* Boolean attribute (no value) */
        if (*p != '=') {
            /* Handle boolean attributes */
            if (tag_equals(attr_name, "disabled")) node->is_disabled = 1;
            else if (tag_equals(attr_name, "checked")) node->is_checked = 1;
            else if (tag_equals(attr_name, "readonly")) node->is_readonly = 1;
            else if (tag_equals(attr_name, "hidden")) { node->is_hidden = 1; node->display = DISPLAY_NONE; }
            else if (tag_equals(attr_name, "required")) node->is_required = 1;
            else if (tag_equals(attr_name, "autofocus")) node->is_autofocus = 1;
            else if (tag_equals(attr_name, "multiple")) node->is_multiple = 1;
            else if (tag_equals(attr_name, "selected")) node->is_selected = 1;
            else if (tag_equals(attr_name, "autoplay")) node->is_autoplay = 1;
            else if (tag_equals(attr_name, "controls")) node->has_controls = 1;
            else if (tag_equals(attr_name, "loop")) node->is_loop = 1;
            else if (tag_equals(attr_name, "muted")) node->is_muted = 1;
            else if (tag_equals(attr_name, "defer")) node->is_defer = 1;
            else if (tag_equals(attr_name, "async")) node->is_async = 1;
            else if (tag_equals(attr_name, "open")) node->is_open = 1;
            else if (tag_equals(attr_name, "novalidate")) node->is_novalidate = 1;
            else if (tag_equals(attr_name, "draggable")) node->is_draggable = 1;
            else if (tag_equals(attr_name, "contenteditable")) node->is_contenteditable = 1;
            else if (tag_equals(attr_name, "spellcheck")) node->is_spellcheck = 1;
            continue;
        }

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
            while (*p && *p != ' ' && *p != '\t' && *p != '>' && vi < 255) attr_value[vi++] = *p++;
        }
        attr_value[vi] = 0;

        /* Apply common attributes */
        if (tag_equals(attr_name, "id")) {
            blaze_str_copy(node->id, attr_value, 64);
        } else if (tag_equals(attr_name, "class")) {
            blaze_str_copy(node->class_list, attr_value, 128);
        } else if (tag_equals(attr_name, "href")) {
            blaze_str_copy(node->href, attr_value, 256);
        } else if (tag_equals(attr_name, "src")) {
            blaze_str_copy(node->src, attr_value, 256);
        } else if (tag_equals(attr_name, "alt")) {
            blaze_str_copy(node->alt, attr_value, 64);
        } else if (tag_equals(attr_name, "title")) {
            blaze_str_copy(node->title_attr, attr_value, 128);
        } else if (tag_equals(attr_name, "name")) {
            blaze_str_copy(node->name, attr_value, 64);
        } else if (tag_equals(attr_name, "value")) {
            blaze_str_copy(node->value, attr_value, 256);
        } else if (tag_equals(attr_name, "placeholder")) {
            blaze_str_copy(node->placeholder, attr_value, 128);
        } else if (tag_equals(attr_name, "action")) {
            blaze_str_copy(node->action, attr_value, 256);
        } else if (tag_equals(attr_name, "method")) {
            blaze_str_copy(node->method, attr_value, 16);
        } else if (tag_equals(attr_name, "target")) {
            blaze_str_copy(node->target, attr_value, 32);
        } else if (tag_equals(attr_name, "rel")) {
            blaze_str_copy(node->rel, attr_value, 64);
        } else if (tag_equals(attr_name, "type")) {
            blaze_str_copy(node->type_attr, attr_value, 32);
            if (tag_equals(node->tag, "input")) {
                node->input_type = parse_input_type(attr_value);
                /* Style specific input types */
                if (node->input_type == INPUT_SUBMIT || node->input_type == INPUT_BUTTON || node->input_type == INPUT_RESET) {
                    node->bg_color = 0xFF3A3A3A;
                    node->border_radius = 4;
                }
                if (node->input_type == INPUT_HIDDEN) {
                    node->display = DISPLAY_NONE;
                }
                if (node->input_type == INPUT_CHECKBOX || node->input_type == INPUT_RADIO) {
                    node->fixed_w = 16; node->fixed_h = 16;
                }
                if (node->input_type == INPUT_COLOR) {
                    node->fixed_w = 40; node->fixed_h = 24;
                }
                if (node->input_type == INPUT_RANGE) {
                    node->fixed_w = 160; node->fixed_h = 20;
                }
            }
        } else if (tag_equals(attr_name, "width")) {
            const char *v = attr_value; node->fixed_w = parse_int(&v);
        } else if (tag_equals(attr_name, "height")) {
            const char *v = attr_value; node->fixed_h = parse_int(&v);
        } else if (tag_equals(attr_name, "colspan")) {
            const char *v = attr_value; node->colspan = parse_int(&v);
        } else if (tag_equals(attr_name, "rowspan")) {
            const char *v = attr_value; node->rowspan = parse_int(&v);
        } else if (tag_equals(attr_name, "tabindex")) {
            const char *v = attr_value; node->tabindex = parse_int(&v);
        } else if (tag_equals(attr_name, "maxlength")) {
            const char *v = attr_value; node->maxlength = parse_int(&v);
        } else if (tag_equals(attr_name, "min")) {
            blaze_str_copy(node->min_attr, attr_value, 32);
        } else if (tag_equals(attr_name, "max")) {
            blaze_str_copy(node->max_attr, attr_value, 32);
        } else if (tag_equals(attr_name, "step")) {
            blaze_str_copy(node->step_attr, attr_value, 32);
        } else if (tag_equals(attr_name, "pattern")) {
            blaze_str_copy(node->pattern, attr_value, 128);
        } else if (tag_equals(attr_name, "for")) {
            blaze_str_copy(node->for_attr, attr_value, 64);
        } else if (tag_equals(attr_name, "role")) {
            blaze_str_copy(node->role, attr_value, 32);
        } else if (tag_equals(attr_name, "lang")) {
            blaze_str_copy(node->lang, attr_value, 16);
        } else if (tag_equals(attr_name, "dir")) {
            blaze_str_copy(node->dir_attr, attr_value, 8);
        } else if (tag_equals(attr_name, "data-")) {
            /* Custom data attributes - store last one seen */
            blaze_str_copy(node->data_attr, attr_value, 128);
        }
        /* Event handlers */
        else if (tag_equals(attr_name, "onclick")) {
            blaze_str_copy(node->onclick_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onmouseover")) {
            blaze_str_copy(node->onmouseover_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onmouseout")) {
            blaze_str_copy(node->onmouseout_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onsubmit")) {
            blaze_str_copy(node->onsubmit_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onchange")) {
            blaze_str_copy(node->onchange_script, attr_value, 256);
        } else if (tag_equals(attr_name, "oninput")) {
            blaze_str_copy(node->oninput_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onkeydown")) {
            blaze_str_copy(node->onkeydown_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onkeyup")) {
            blaze_str_copy(node->onkeyup_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onload")) {
            blaze_str_copy(node->onload_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onfocus")) {
            blaze_str_copy(node->onfocus_script, attr_value, 256);
        } else if (tag_equals(attr_name, "onblur")) {
            blaze_str_copy(node->onblur_script, attr_value, 256);
        }
        /* Style attribute */
        else if (tag_equals(attr_name, "style")) {
            parse_inline_style(node, attr_value);
        }
        /* Font tag attributes (deprecated but supported) */
        else if (tag_equals(attr_name, "color") && tag_equals(node->tag, "font")) {
            node->fg_color = blaze_parse_color(attr_value);
        } else if (tag_equals(attr_name, "size") && tag_equals(node->tag, "font")) {
            const char *v = attr_value;
            int sz = parse_int(&v);
            /* HTML font size 1-7 mapping */
            int sizes[] = {8, 10, 12, 14, 18, 24, 36};
            if (sz >= 1 && sz <= 7) node->font_size = sizes[sz - 1];
        } else if (tag_equals(attr_name, "face") && tag_equals(node->tag, "font")) {
            blaze_str_copy(node->font_family, attr_value, 64);
        }
        /* ARIA attributes */
        else if (blaze_str_starts_with(attr_name, "aria-")) {
            /* Store for accessibility - simplified */
            blaze_str_copy(node->aria_label, attr_value, 128);
        }
    }
}

/* MAIN HTML PARSER */

/* Parse HTML string into DOM tree */
void blaze_parse_html(BlazeTab *tab, const char *html, uint32_t len) {
    tab->node_count = 0;
    tab->document = blaze_create_element(tab, "document");
    blaze_str_copy(tab->document->tag, "document", 32);
    tab->document->display = DISPLAY_BLOCK;

    /* Handle empty/null input */
    if (!html || len == 0) {
        DOMNode *body = blaze_create_element(tab, "body");
        if (body) {
            body->bg_color = 0xFF1A1A2E;
            blaze_append_child(tab->document, body);
            DOMNode *text = blaze_create_text(tab, "ERROR: No HTML content loaded");
            if (text) {
                text->fg_color = 0xFFFF4444;
                text->font_size = 16;
                blaze_append_child(body, text);
            }
        }
        return;
    }

    /* Parser stack for nesting */
    DOMNode *stack[64];
    int stack_top = 0;
    stack[stack_top++] = tab->document;

    /* Track list item numbering */
    int list_counter[16];
    int list_depth = 0;
    for (int i = 0; i < 16; i++) list_counter[i] = 0;

    const char *p = html;
    const char *end = html + len;
    bool in_pre = false;  /* Track <pre> context */

    while (p < end && tab->node_count < BLAZE_MAX_DOM_NODES - 2) {
        /* In non-pre mode, skip leading whitespace between tags */
        if (!in_pre) {
            const char *ws_start = p;
            p = skip_whitespace(p);
            if (p >= end) break;
            /* If we only had whitespace between tags, skip it */
            if (*p == '<' && ws_start != p) {
                /* Pure whitespace before a tag - skip */
            }
        }

        if (p >= end) break;

        if (*p == '<') {
            p++;
            if (p >= end) break;

            /* ---- HTML Comment ---- */
            if (p + 2 < end && p[0] == '!' && p[1] == '-' && p[2] == '-') {
                p += 3;
                const char *comment_start = p;
                while (p + 2 < end) {
                    if (p[0] == '-' && p[1] == '-' && p[2] == '>') {
                        /* Optionally store comment */
                        int clen = p - comment_start;
                        if (clen > 255) clen = 255;
                        char cbuf[256];
                        for (int i = 0; i < clen; i++) cbuf[i] = comment_start[i];
                        cbuf[clen] = 0;
                        /* Comment nodes are created but not displayed */
                        DOMNode *cnode = blaze_create_comment(tab, cbuf);
                        if (cnode && stack_top > 0) {
                            blaze_append_child(stack[stack_top - 1], cnode);
                        }
                        p += 3;
                        break;
                    }
                    p++;
                }
                continue;
            }

            /* ---- DOCTYPE and other <! declarations ---- */
            if (*p == '!') {
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            /* ---- Processing instruction <?...?> ---- */
            if (*p == '?') {
                while (p + 1 < end) {
                    if (*p == '?' && *(p + 1) == '>') { p += 2; break; }
                    p++;
                }
                continue;
            }

            /* ---- Closing tag ---- */
            if (*p == '/') {
                p++;
                char close_tag[32];
                int ct_len = 0;
                while (p < end && is_tag_char(*p) && ct_len < 31) {
                    close_tag[ct_len++] = to_lower(*p);
                    p++;
                }
                close_tag[ct_len] = 0;

                /* Track pre context */
                if (tag_equals(close_tag, "pre")) in_pre = false;

                /* Track list depth */
                if (tag_equals(close_tag, "ul") || tag_equals(close_tag, "ol") ||
                    tag_equals(close_tag, "menu") || tag_equals(close_tag, "dir")) {
                    if (list_depth > 0) list_depth--;
                }

                /* Pop matching element from stack */
                if (ct_len > 0 && stack_top > 1) {
                    /* Search stack for matching tag */
                    int found = -1;
                    for (int i = stack_top - 1; i >= 1; i--) {
                        if (tag_equals(stack[i]->tag, close_tag)) {
                            found = i;
                            break;
                        }
                    }
                    if (found > 0) {
                        stack_top = found;  /* Pop down to (and including) matching tag */
                    }
                }

                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            /* ---- Opening tag ---- */
            char tag[32];
            int tag_len = 0;
            while (p < end && is_tag_char(*p) && tag_len < 31) {
                tag[tag_len++] = to_lower(*p);
                p++;
            }
            tag[tag_len] = 0;

            if (tag_len == 0) {
                /* Malformed - skip */
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            /* ---- Handle raw text elements (script, style) ---- */
            if (tag_equals(tag, "script") || tag_equals(tag, "style")) {
                /* Find end of opening tag */
                const char *attr_start = p;
                while (p < end && *p != '>') p++;
                if (p < end) p++; /* skip '>' */

                const char *content_start = p;
                const char *content_end = content_start;

                /* Find matching closing tag </script> or </style> */
                char close_needle[12] = "</";
                int ni = 2;
                for (int i = 0; tag[i] && ni < 10; i++) close_needle[ni++] = tag[i];
                close_needle[ni] = 0;

                while (content_end < end) {
                    if (*content_end == '<' && content_end + 1 < end && content_end[1] == '/') {
                        const char *check = content_end + 2;
                        bool match = true;
                        for (int i = 0; tag[i]; i++) {
                            if (check >= end) { match = false; break; }
                            if (to_lower(*check) != tag[i]) { match = false; break; }
                            check++;
                        }
                        if (match && check < end && (*check == '>' || *check == ' ')) break;
                    }
                    content_end++;
                }

                if (tag_equals(tag, "script") && content_end > content_start) {
                    int script_len = content_end - content_start;
                    char *script_buf = (char *)kmalloc(script_len + 1);
                    if (script_buf) {
                        for (int i = 0; i < script_len; i++) script_buf[i] = content_start[i];
                        script_buf[script_len] = 0;
                        blaze_js_execute(tab, script_buf);
                        kfree(script_buf);
                    }
                } else if (tag_equals(tag, "style") && content_end > content_start) {
                    /* Parse CSS and apply to tab's stylesheet */
                    int css_len = content_end - content_start;
                    char *css_buf = (char *)kmalloc(css_len + 1);
                    if (css_buf) {
                        for (int i = 0; i < css_len; i++) css_buf[i] = content_start[i];
                        css_buf[css_len] = 0;
                        blaze_parse_css(tab, css_buf, css_len);
                        kfree(css_buf);
                    }
                }

                /* Skip past closing tag */
                p = content_end;
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            /* ---- Handle <title> ---- */
            if (tag_equals(tag, "title")) {
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                const char *title_start = p;
                while (p < end) {
                    if (*p == '<' && p + 1 < end && p[1] == '/') break;
                    p++;
                }
                int title_len = p - title_start;
                if (title_len >= BLAZE_MAX_TITLE_LEN) title_len = BLAZE_MAX_TITLE_LEN - 1;
                for (int i = 0; i < title_len; i++) tab->title[i] = title_start[i];
                tab->title[title_len] = 0;
                decode_entities_inplace(tab->title);
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            /* ---- Handle <head> content: skip meta/link but not title ---- */
            if (tag_equals(tag, "head")) {
                /* Create head node but mark as non-display */
                DOMNode *head_node = blaze_create_element(tab, tag);
                if (head_node) {
                    head_node->display = DISPLAY_NONE;
                    blaze_append_child(stack[stack_top - 1], head_node);
                    if (stack_top < 63) stack[stack_top++] = head_node;
                }
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            /* ---- Auto-close optional end tags ---- */
            if (stack_top > 1) {
                DOMNode *current = stack[stack_top - 1];
                if (current && tag_auto_closes(tag, current->tag)) {
                    stack_top--;
                }
            }

            /* ---- Create the element node ---- */
            DOMNode *node = blaze_create_element(tab, tag);
            if (!node) {
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            /* Apply default styles for this tag */
            apply_default_tag_styles(node, tag);

            /* Inherit from parent */
            DOMNode *parent = stack[stack_top - 1];
            if (parent) {
                /* Inherit text properties */
                if (node->fg_color == 0xFFFFFFFF && parent->fg_color != 0xFFFFFFFF) {
                    node->fg_color = parent->fg_color;
                }
                if (node->font_size == 14 && parent->font_size != 14) {
                    /* Only inherit if we didn't set a specific size */
                    const TagInfo *info = lookup_tag(tag);
                    if ((!info || info->category != TAG_CAT_HEADING)) {
                        node->font_size = parent->font_size;
                    }
                }
                if (parent->is_bold && !node->is_bold) {
                    /* Bold inheritance for inline elements */
                    const TagInfo *info = lookup_tag(tag);
                    if ((!info || !info->is_block)) node->is_bold = parent->is_bold;
                }
                if (parent->is_italic) {
                    const TagInfo *info = lookup_tag(tag);
                    if ((!info || !info->is_block)) node->is_italic = parent->is_italic;
                }
            }

            /* Parse attributes */
            parse_attributes(node, p);

            /* Track pre context */
            if (tag_equals(tag, "pre")) in_pre = true;

            /* Track list numbering */
            if (tag_equals(tag, "ul") || tag_equals(tag, "ol") ||
                tag_equals(tag, "menu") || tag_equals(tag, "dir")) {
                if (list_depth < 15) {
                    list_counter[list_depth] = 0;
                    list_depth++;
                }
            }
            if (tag_equals(tag, "li") && list_depth > 0) {
                list_counter[list_depth - 1]++;
                node->list_index = list_counter[list_depth - 1];
                /* Determine list type from parent */
                if (parent && tag_equals(parent->tag, "ol")) {
                    node->list_type = LIST_ORDERED;
                } else {
                    node->list_type = LIST_UNORDERED;
                }
            }

            /* Append to parent */
            blaze_append_child(parent, node);

            /* Check for self-closing: <tag/> or void element */
            bool self_close = is_self_closing(tag);
            /* Also check for <tag ... /> syntax */
            {
                const char *check = p;
                while (check < end && *check != '>') {
                    if (*check == '/') { self_close = true; break; }
                    check++;
                }
            }

            if (!self_close) {
                if (stack_top < 63) stack[stack_top++] = node;
            }

            /* Skip to end of tag */
            while (p < end && *p != '>') p++;
            if (p < end) p++;

        } else {
            /* ---- Text content ---- */
            char text[256];
            int ti = 0;

            if (in_pre) {
                /* In <pre>, preserve whitespace exactly */
                while (p < end && *p != '<' && ti < 255) {
                    text[ti++] = *p++;
                }
            } else {
                /* Normal mode: collapse whitespace */
                bool last_was_space = false;
                while (p < end && *p != '<' && ti < 255) {
                    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                        if (!last_was_space && ti > 0) {
                            text[ti++] = ' ';
                            last_was_space = true;
                        }
                        p++;
                    } else {
                        text[ti++] = *p++;
                        last_was_space = false;
                    }
                }
                /* Trim trailing space */
                if (ti > 0 && text[ti - 1] == ' ') ti--;
            }
            text[ti] = 0;

            /* Only add non-empty text */
            bool has_content = false;
            for (int i = 0; i < ti; i++) {
                if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
                    has_content = true;
                    break;
                }
            }

            if (has_content && stack_top > 0) {
                DOMNode *text_node = blaze_create_text(tab, text);
                if (text_node) {
                    DOMNode *parent = stack[stack_top - 1];
                    if (parent) {
                        text_node->fg_color = parent->fg_color;
                        text_node->font_size = parent->font_size;
                        text_node->is_bold = parent->is_bold;
                        text_node->is_italic = parent->is_italic;
                        text_node->is_underline = parent->is_underline;
                        text_node->is_strikethrough = parent->is_strikethrough;
                        text_node->text_transform = parent->text_transform;
                    }
                    blaze_append_child(parent, text_node);
                }
            }
        }
    }
}

/* DOM QUERY FUNCTIONS */

/* Find element by ID */
DOMNode *blaze_get_element_by_id(BlazeTab *tab, const char *id) {
    for (uint32_t i = 0; i < tab->node_count; i++) {
        if (tab->nodes[i].type == NODE_ELEMENT && tag_equals(tab->nodes[i].id, id)) {
            return &tab->nodes[i];
        }
    }
    return NULL;
}

/* Find elements by tag name - returns count, fills results array */
int blaze_get_elements_by_tag(BlazeTab *tab, const char *tag, DOMNode **results, int max_results) {
    int count = 0;
    for (uint32_t i = 0; i < tab->node_count && count < max_results; i++) {
        if (tab->nodes[i].type == NODE_ELEMENT && tag_equals(tab->nodes[i].tag, tag)) {
            results[count++] = &tab->nodes[i];
        }
    }
    return count;
}

/* Find elements by class name - returns count */
int blaze_get_elements_by_class(BlazeTab *tab, const char *classname, DOMNode **results, int max_results) {
    int count = 0;
    for (uint32_t i = 0; i < tab->node_count && count < max_results; i++) {
        if (tab->nodes[i].type != NODE_ELEMENT) continue;
        /* Check if class_list contains the target class */
        const char *cl = tab->nodes[i].class_list;
        while (*cl) {
            while (*cl == ' ') cl++;
            const char *start = cl;
            while (*cl && *cl != ' ') cl++;
            int clen = cl - start;
            /* Compare */
            const char *t = classname;
            int tlen = 0;
            while (t[tlen]) tlen++;
            if (clen == tlen) {
                bool match = true;
                for (int j = 0; j < clen; j++) {
                    if (start[j] != classname[j]) { match = false; break; }
                }
                if (match) { results[count++] = &tab->nodes[i]; break; }
            }
        }
    }
    return count;
}

/* Query selector (simplified - supports #id, .class, tag) */
DOMNode *blaze_query_selector(BlazeTab *tab, const char *selector) {
    if (!selector || !*selector) return NULL;

    if (selector[0] == '#') {
        return blaze_get_element_by_id(tab, selector + 1);
    } else if (selector[0] == '.') {
        DOMNode *result = NULL;
        blaze_get_elements_by_class(tab, selector + 1, &result, 1);
        return result;
    } else {
        DOMNode *result = NULL;
        blaze_get_elements_by_tag(tab, selector, &result, 1);
        return result;
    }
}

/* Get the text content of a node and its children */
int blaze_get_text_content(DOMNode *node, char *buffer, int max_len) {
    if (!node || max_len <= 0) return 0;

    int pos = 0;
    if (node->type == NODE_TEXT) {
        const char *t = node->text;
        while (*t && pos < max_len - 1) buffer[pos++] = *t++;
    } else if (node->type == NODE_ELEMENT) {
        DOMNode *child = node->first_child;
        while (child && pos < max_len - 1) {
            pos += blaze_get_text_content(child, buffer + pos, max_len - pos);
            child = child->next_sibling;
        }
    }
    buffer[pos] = 0;
    return pos;
}

/* Set inner HTML of an element (re-parses) */
void blaze_set_inner_html(BlazeTab *tab, DOMNode *node, const char *html) {
    if (!node || node->type != NODE_ELEMENT || !html) return;

    /* Remove existing children (simplified - just unlink) */
    node->first_child = NULL;
    node->last_child = NULL;
    node->child_count = 0;

    /* Create a temporary sub-parser context */
    /* For simplicity, parse inline and attach children */
    const char *p = html;
    int len = 0;
    while (p[len]) len++;

    DOMNode *stack[32];
    int stack_top = 0;
    stack[stack_top++] = node;

    const char *end = p + len;

    while (p < end && tab->node_count < BLAZE_MAX_DOM_NODES - 2) {
        p = skip_whitespace(p);
        if (p >= end) break;

        if (*p == '<') {
            p++;
            if (p >= end) break;
            if (*p == '/') {
                p++;
                while (p < end && is_tag_char(*p)) p++;
                if (stack_top > 1) stack_top--;
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }
            char tag[32]; int tl = 0;
            while (p < end && is_tag_char(*p) && tl < 31) {
                tag[tl++] = to_lower(*p); p++;
            }
            tag[tl] = 0;
            DOMNode *child = blaze_create_element(tab, tag);
            if (child) {
                apply_default_tag_styles(child, tag);
                parse_attributes(child, p);
                blaze_append_child(stack[stack_top - 1], child);
                if (!is_self_closing(tag) && stack_top < 31) stack[stack_top++] = child;
            }
            while (p < end && *p != '>') p++;
            if (p < end) p++;
        } else {
            char text[256]; int ti = 0;
            while (p < end && *p != '<' && ti < 255) text[ti++] = *p++;
            text[ti] = 0;
            bool has = false;
            for (int i = 0; i < ti; i++) {
                if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') { has = true; break; }
            }
            if (has) {
                DOMNode *tn = blaze_create_text(tab, text);
                if (tn) blaze_append_child(stack[stack_top - 1], tn);
            }
        }
    }
}