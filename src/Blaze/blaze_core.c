/*
 * Blaze Browser - Core Implementation
 * Main browser logic, tab management, and UI
*/

#include "blaze.h"
#include "../window_manager.h"

/* External functions from AurionOS */
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern uint32_t get_ticks(void);
extern int load_file_content(const char *filename, char *buffer, int max_len);
extern int save_file_content(const char *filename, const char *data, int len);

/* Forward declarations */
static int fast_atoi(const char **s);
int blaze_get_page_height(BlazeState *state);

/* Utility Functions */
void blaze_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int blaze_str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int blaze_str_len(const char *s) {
    int l = 0;
    while (s[l]) l++;
    return l;
}

bool blaze_str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        str++; prefix++;
    }
    return true;
}

const char* blaze_str_strstr(const char *haystack, const char *needle) {
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

void blaze_log(BlazeState *state, const char *msg) {
    int len = blaze_str_len(msg);
    if (state->console_len + len < 4095) {
        for (int i = 0; i < len; i++) {
            state->console_log[state->console_len++] = msg[i];
        }
        state->console_log[state->console_len] = 0;
    }
}

/* Helper: Convert string to integer for rgba parsing */
static int fast_atoi(const char **s) {
    int res = 0;
    while (**s >= '0' && **s <= '9') {
        res = res * 10 + (**s - '0');
        (*s)++;
    }
    return res;
}

/* Find node at coordinates */
DOMNode* blaze_find_node_at(BlazeTab *tab, int x, int y) {
    for (int i = tab->node_count - 1; i >= 0; i--) {
        DOMNode *node = &tab->nodes[i];
        if (x >= node->x && x < node->x + node->w &&
            y >= node->y && y < node->y + node->h) {
            return node;
        }
    }
    return NULL;
}

/* Parse hex color #RRGGBB, named colors, or rgb/rgba */
uint32_t blaze_parse_color(const char *color_str) {
    if (!color_str || !color_str[0]) return 0xFF000000;
    
    /* Skip leading whitespace */
    while (*color_str == ' ' || *color_str == '\t') color_str++;

    /* Hex color */
    if (color_str[0] == '#') {
        const char *p = color_str + 1;
        int len = 0;
        while (p[len] && ((p[len] >= '0' && p[len] <= '9') || (p[len] >= 'a' && p[len] <= 'f') || (p[len] >= 'A' && p[len] <= 'F'))) len++;

        uint32_t r = 0, g = 0, b = 0;
        if (len == 3) {
            char c = p[0];
            r = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10);
            r = (r << 4) | r;
            c = p[1];
            g = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10);
            g = (g << 4) | g;
            c = p[2];
            b = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10);
            b = (b << 4) | b;
        } else if (len >= 6) {
            for (int i = 0; i < 6; i++) {
                char c = p[i];
                int val = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10);
                if (i < 2) r = (r << 4) | val;
                else if (i < 4) g = (g << 4) | val;
                else b = (b << 4) | val;
            }
        }
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    
    /* rgb(r, g, b) or rgba(r, g, b, a) */
    if (blaze_str_starts_with(color_str, "rgb")) {
        const char *p = color_str + 3;
        bool has_alpha = false;
        if (*p == 'a') { p++; has_alpha = true; }
        
        while (*p && *p != '(') p++;
        if (*p == '(') p++;
        
        while (*p == ' ') p++;
        int r = fast_atoi(&p);
        while (*p && *p != ',') p++; if (*p == ',') p++;
        while (*p == ' ') p++;
        int g = fast_atoi(&p);
        while (*p && *p != ',') p++; if (*p == ',') p++;
        while (*p == ' ') p++;
        int b = fast_atoi(&p);
        
        uint32_t a = 0xFF;
        if (has_alpha) {
            while (*p && *p != ',') p++; if (*p == ',') p++;
            while (*p == ' ') p++;
            /* Parse float alpha (0.0 to 1.0) */
            if (*p == '0') {
                p++;
                if (*p == '.') {
                    p++;
                    int frac = fast_atoi(&p);
                    a = (frac * 255) / 10; /* Simplified: 0.1 -> 10% */
                } else a = 0;
            } else if (*p == '1') {
                a = 255;
            }
        }
        
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    /* Named colors */
    if (blaze_str_cmp(color_str, "white") == 0) return 0xFFFFFFFF;
    if (blaze_str_cmp(color_str, "black") == 0) return 0xFF000000;
    if (blaze_str_cmp(color_str, "red") == 0) return 0xFFFF0000;
    if (blaze_str_cmp(color_str, "green") == 0) return 0xFF00FF00;
    if (blaze_str_cmp(color_str, "blue") == 0) return 0xFF0000FF;
    if (blaze_str_cmp(color_str, "yellow") == 0) return 0xFFFFFF00;
    if (blaze_str_cmp(color_str, "cyan") == 0) return 0xFF00FFFF;
    if (blaze_str_cmp(color_str, "magenta") == 0) return 0xFFFF00FF;
    if (blaze_str_cmp(color_str, "gray") == 0) return 0xFF808080;
    
    return 0xFF000000;
}

/* Initialize browser state */
void blaze_init(BlazeState *state) {
    for (int i = 0; i < (int)sizeof(BlazeState); i++) {
        ((char *)state)[i] = 0;
    }
    
    state->tab_count = 1;
    state->active_tab = 0;
    state->address_bar_focused = false;
    state->console_open = false;
    state->console_scroll_y = 0;
    
    /* Initialize first tab */
    BlazeTab *tab = &state->tabs[0];
    blaze_str_copy(tab->url, "/Documents/index.html", BLAZE_MAX_URL_LEN);
    blaze_str_copy(tab->title, "New Tab", BLAZE_MAX_TITLE_LEN);
    tab->loading = false;
    tab->scroll_y = 0;
    tab->node_count = 0;
    tab->rule_count = 0;
    tab->html_content = 0;
    tab->render_buffer = 0;
    tab->needs_reflow = true;
    
    /* Create document root */
    tab->document = &tab->nodes[tab->node_count++];
    tab->document->type = NODE_DOCUMENT;
    blaze_str_copy(tab->document->tag, "document", 32);
    
    blaze_log(state, "[Blaze] Browser initialized\n");
}

/* Create new tab */
int blaze_new_tab(BlazeState *state) {
    if (state->tab_count >= BLAZE_MAX_TABS) return -1;
    
    int idx = state->tab_count++;
    BlazeTab *tab = &state->tabs[idx];
    
    blaze_str_copy(tab->url, "about:blank", BLAZE_MAX_URL_LEN);
    blaze_str_copy(tab->title, "New Tab", BLAZE_MAX_TITLE_LEN);
    tab->loading = false;
    tab->scroll_y = 0;
    tab->node_count = 0;
    tab->rule_count = 0;
    tab->html_content = 0;
    tab->render_buffer = 0;
    tab->needs_reflow = true;
    
    /* Create document root */
    tab->document = &tab->nodes[0];
    tab->node_count = 1;
    tab->document->type = NODE_DOCUMENT;
    blaze_str_copy(tab->document->tag, "document", 32);
    tab->html_len = 0;
    tab->rule_count = 0;
    tab->scroll_y = 0;
    tab->loading = false;
    tab->html_content = 0;
    tab->render_buffer = 0;
    tab->needs_reflow = true;
    
    state->active_tab = idx;
    return idx;
}

/* Close tab */
void blaze_close_tab(BlazeState *state, int idx) {
    if (idx < 0 || idx >= state->tab_count) return;
    if (state->tab_count == 1) return; /* Keep at least one tab */
    
    BlazeTab *tab = &state->tabs[idx];
    if (tab->html_content) kfree(tab->html_content);
    if (tab->render_buffer) kfree(tab->render_buffer);
    
    /* Shift tabs down */
    for (int i = idx; i < state->tab_count - 1; i++) {
        state->tabs[i] = state->tabs[i + 1];
    }
    state->tab_count--;
    
    if (state->active_tab >= state->tab_count) {
        state->active_tab = state->tab_count - 1;
    }
}

/* Address bar: treat host-like input (example.com) as http:// — avoid /Documents/example.com */
static char blaze_lc(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static bool blaze_ext_is_known_file_suffix(const char *e, int elen) {
    const char *exts[] = {
        "html", "htm", "xhtml", "css", "js", "mjs", "json", "xml",
        "txt", "md", "pdf", "png", "jpg", "jpeg", "gif", "svg", "ico",
        "bmp", "webp", "wasm", "map",
    };
    for (unsigned k = 0; k < sizeof(exts) / sizeof(exts[0]); k++) {
        const char *lit = exts[k];
        int i = 0;
        for (; lit[i]; i++) {
            if (i >= elen || blaze_lc(e[i]) != lit[i]) break;
        }
        if (lit[i] == 0 && i == elen) return true;
    }
    return false;
}

static bool blaze_slice_eq_ci(const char *s, int n, const char *lit) {
    for (int i = 0; i < n; i++) {
        if (!lit[i] || blaze_lc(s[i]) != lit[i]) return false;
    }
    return lit[n] == 0;
}

static bool blaze_host_ipv4_like(const char *h, int n) {
    if (n < 3) return false;
    int dots = 0;
    for (int i = 0; i < n; i++) {
        char c = h[i];
        if (c >= '0' && c <= '9') continue;
        if (c == '.') { dots++; continue; }
        return false;
    }
    return dots >= 1;
}

/* True if input should fetch over HTTP without an explicit scheme (e.g. example.com, 1.2.3.4/path) */
static bool blaze_looks_like_web_hostname(const char *url) {
    if (!url || !url[0] || url[0] == '.' || url[0] == '/') return false;

    const char *p = url;
    const char *auth_end = p;
    while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
        auth_end++;

    int auth_len = (int)(auth_end - p);
    if (auth_len <= 0) return false;

    int host_len = auth_len;
    for (int i = 0; i < auth_len; i++) {
        if (p[i] == ':') {
            host_len = i;
            break;
        }
    }
    if (host_len <= 0) return false;

    if (host_len == 9 && blaze_slice_eq_ci(p, 9, "localhost")) return true;

    if (blaze_host_ipv4_like(p, host_len)) return true;

    /* If there is no path, reject obvious file names (index.html, notes.txt) */
    if (*auth_end == 0 || *auth_end == '?' || *auth_end == '#') {
        const char *last = NULL;
        for (int i = 0; i < host_len; i++) {
            if (p[i] == '.') last = p + i;
        }
        if (last && last > p) {
            int el = (int)((p + host_len) - (last + 1));
            if (el >= 2 && el <= 5 && blaze_ext_is_known_file_suffix(last + 1, el))
                return false;
        }
    }

    for (int i = 0; i < host_len; i++) {
        if (p[i] == '.') return true;
    }
    return false;
}

/* Normalize address bar / navigation input */
static void blaze_canonicalize_user_url(char *out, int cap, const char *url) {
    if (!url || !url[0]) {
        blaze_str_copy(out, "/Documents/index.html", cap);
        return;
    }
    
    /* If the user only typed the protocol, go home */
    if (blaze_str_cmp(url, "http://") == 0 || blaze_str_cmp(url, "https://") == 0 || 
        blaze_str_cmp(url, "file://") == 0) {
        blaze_str_copy(out, "/Documents/index.html", cap);
        return;
    }

    if (blaze_str_starts_with(url, "http://") || blaze_str_starts_with(url, "https://") ||
        blaze_str_starts_with(url, "file://") || url[0] == '/') {
        blaze_str_copy(out, url, cap);
        return;
    }
    if (blaze_str_starts_with(url, "//")) {
        blaze_str_copy(out, "http:", cap);
        int l = blaze_str_len(out);
        blaze_str_copy(out + l, url, cap - l);
        return;
    }
    if (blaze_looks_like_web_hostname(url)) {
        blaze_str_copy(out, "http://", cap);
        int l = blaze_str_len(out);
        blaze_str_copy(out + l, url, cap - l);
        return;
    }
    blaze_str_copy(out, url, cap);
}

/* Navigate to URL */
void blaze_navigate(BlazeState *state, const char *url) {
    BlazeTab *tab = &state->tabs[state->active_tab];
    
    char resolved[BLAZE_MAX_URL_LEN];
    blaze_canonicalize_user_url(resolved, BLAZE_MAX_URL_LEN, url);

    /* Free old content EARLY to reclaim memory for the next allocation */
    if (tab->html_content) {
        kfree(tab->html_content);
        tab->html_content = 0;
    }
    
    blaze_str_copy(tab->url, resolved, BLAZE_MAX_URL_LEN);
    tab->loading = true;
    tab->scroll_y = 0;
    tab->node_count = 1; /* Reset to document root only */
    tab->rule_count = 0;
    tab->needs_reflow = true;
    
    /* Add to history */
    if (state->history_count < BLAZE_MAX_HISTORY) {
        blaze_str_copy(state->history[state->history_count], resolved, BLAZE_MAX_URL_LEN);
        state->history_count++;
        state->history_pos = state->history_count;
    }
    
    blaze_log(state, "[Blaze] Navigating to: ");
    blaze_log(state, resolved);
    blaze_log(state, "\n");
    
    /* Check if this is a local file path (starts with C:\ or file://) */
    bool is_local = false;
    const char *file_path = resolved;
    char absolute_path[BLAZE_MAX_URL_LEN];
    
    if (blaze_str_starts_with(resolved, "file://")) {
        is_local = true;
        file_path = resolved + 7; /* Skip "file://" */
    } else if (resolved[0] == '/') {
        is_local = true;
        file_path = resolved;
    } else if (resolved[0] && !blaze_str_starts_with(resolved, "http")) {
        /* Relative path: assume relative to /Documents/ for now */
        is_local = true;
        blaze_str_copy(absolute_path, "/Documents/", BLAZE_MAX_URL_LEN);
        int plen = blaze_str_len(absolute_path);
        blaze_str_copy(absolute_path + plen, resolved, BLAZE_MAX_URL_LEN - plen);
        file_path = absolute_path;
    }
    
    char *content = 0;
    uint32_t content_len = 0;
    int result = -1;
    
    if (is_local) {
        /* Load from local filesystem */
        blaze_log(state, "[Blaze] Loading local file: ");
        blaze_log(state, file_path);
        blaze_log(state, "\n");
        
        /* Allocate buffer for file content (8KB is plenty for index/about etc.) */
        blaze_log(state, "[Blaze] Allocating 8KB for file_buf...\n");
        char *file_buf = (char *)kmalloc(8192); 
        if (file_buf) {
            blaze_log(state, "[Blaze] Allocated at 0xOK\n");
            int bytes_read = load_file_content(file_path, file_buf, 8191);
            
            if (bytes_read > 0) {
                content = file_buf;
                content_len = bytes_read;
                result = 0;
                blaze_log(state, "[Blaze] File loaded: ");
                /* Print bytes_read */
                char numbuf[16];
                int nbpos = 0;
                int n = bytes_read;
                if (n == 0) { numbuf[nbpos++] = '0'; }
                else { while (n > 0) { numbuf[nbpos++] = '0' + (n % 10); n /= 10; } }
                while (nbpos > 0) { blaze_log(state, (char[]){numbuf[--nbpos], 0}); }
                blaze_log(state, " bytes\n");
            } else {
                if (blaze_str_cmp(file_path, "/Documents/index.html") == 0) {
                    blaze_log(state, "[Blaze] Using embedded fallback for index.html\n");
                    const char *fallback = "<html><title>Aurion OS</title><body style='background:#000; color:#fff; text-align:center; padding-top:100;'>"
                                          "<h1 style='font-size:80; color:#4ade80;'>AURION OS</h1>"
                                          "<p style='color:#888; font-size:24;'>Premium Web Experience v1.1</p>"
                                          "<div style='margin-top:50;'><a href='/Documents/index.html' style='color:#4ade80; text-decoration:none;'>RETRY LOADING</a></div>"
                                          "</body></html>";
                    int fl = blaze_str_len(fallback);
                    for (int i = 0; i < fl && i < 8191; i++) file_buf[i] = fallback[i];
                    file_buf[fl < 8191 ? fl : 8191] = 0;
                    content = file_buf;
                    content_len = fl;
                    result = 0;
                } else {
                    kfree(file_buf);
                    blaze_log(state, "[Blaze] load_file_content returned 0 or -1\n");
                }
            }
        } else {
            blaze_log(state, "[Blaze] kmalloc failed\n");
        }
    } else {
        /* Fetch from network */
        result = blaze_fetch(resolved, &content, &content_len);
    }
    
    /* Debug: print what we got */
    if (result == 0 && content) {
        blaze_log(state, "[Blaze] Parsing HTML...\n");
    }
    
    if (result == 0 && content) {
        tab->html_content = content;
        tab->html_len = content_len;
        
        /* Debug: Show first 100 chars of HTML */
        blaze_log(state, "[Blaze] HTML content (first 100 chars): ");
        for (uint32_t i = 0; i < content_len && i < 100; i++) {
            char c = content[i];
            if (c >= 32 && c < 127) {
                blaze_log(state, (char[]){c, 0});
            } else {
                blaze_log(state, ".");
            }
        }
        blaze_log(state, "\n");
        
        /* Parse HTML */
        blaze_log(state, "[Blaze] Calling parse_html...\n");
        blaze_parse_html(tab, content, content_len);
        blaze_log(state, "[Blaze] parse_html done, nodes=");
        char nbuf[16]; int npos = 0; int n = tab->node_count;
        if (n == 0) { nbuf[npos++] = '0'; }
        else { while (n > 0) { nbuf[npos++] = '0' + (n % 10); n /= 10; } }
        while (npos > 0) { blaze_log(state, (char[]){nbuf[--npos], 0}); }
        blaze_log(state, "\n");
        
        /* Apply default styles */
        blaze_log(state, "[Blaze] Calling apply_styles...\n");
        blaze_apply_styles(tab);
        blaze_log(state, "[Blaze] apply_styles done\n");
        
        /* Layout */
        blaze_log(state, "[Blaze] Calling layout...\n");
        blaze_layout(tab, BLAZE_VIEWPORT_W, BLAZE_VIEWPORT_H);
        blaze_log(state, "[Blaze] layout done\n");
        
        tab->loading = false;
        tab->needs_reflow = false;
        
        blaze_log(state, "[Blaze] Page loaded successfully\n");
    } else {
        blaze_log(state, "[Blaze] Failed to load page\n");
        tab->loading = false;
        
        /* Show error page */
        blaze_str_copy(tab->title, "Error Loading Page", BLAZE_MAX_TITLE_LEN);
        const char *error_html = 
            "<html><title>Page Load Error</title>"
            "<body style='background:#000;color:#0f0;padding:40px;font-family:monospace;'>"
            "<h1 style='color:#f87171;'>Network error</h1>"
            "<p>Blaze Browser could not reach the requested host.</p><br>"
            "<p style='color:#60a5fa;'><strong>Troubleshooting suggestions:</strong></p>"
            "<ul style='color:#94a3b8; line-height:1.8;'>"
            "<li><strong>Network Interface</strong>: AurionOS supports VirtIO-Net, RTL8139, or NE2000. VMware default Intel e1000 / vmxnet3 is not supported — change the adapter to <strong>RTL8139</strong> (legacy) in VM Settings → Network → Advanced.</li>"
            "<li><strong>DNS Resolution</strong>: If you see 'Unknown Host' in the console, try <strong>http://146.190.62.39</strong> (httpforever) by IP.</li>"
            "<li><strong>HTTPS/TLS</strong>: Some sites require TLS 1.3. Blaze supports HTTP and TLS 1.2 (RSA-AES).</li>"
            "<li><strong>QEMU</strong>: Use <code>-device virtio-net-pci -netdev user,id=n0</code>. <strong>VMware</strong> does not use QEMU's 10.0.2.x network; DHCP must succeed with a supported NIC.</li>"
            "</ul><br>"
            "<p>URL: <span style='color:#fbbf24;'>";
        
        /* Build error page */
        int err_len = blaze_str_len(error_html) + blaze_str_len(resolved) + 500;
        char *err_page = (char *)kmalloc(err_len);
        if (err_page) {
            int pos = 0;
            for (int i = 0; error_html[i]; i++) err_page[pos++] = error_html[i];
            for (int i = 0; resolved[i]; i++) err_page[pos++] = resolved[i];
            const char *end = "</span></p><br><br>"
                              "<a href='/Documents/index.html' style='color:#34d399; text-decoration:none;'>[ Return Home ]</a>"
                              "</body></html>";
            for (int i = 0; end[i]; i++) err_page[pos++] = end[i];
            err_page[pos] = 0;
            
            if (tab->html_content) kfree(tab->html_content);
            tab->html_content = err_page;
            tab->html_len = pos;
            
            blaze_parse_html(tab, err_page, pos);
            blaze_apply_styles(tab);
            blaze_layout(tab, BLAZE_VIEWPORT_W, BLAZE_VIEWPORT_H);
        }
    }
}

/* Go back in history */
void blaze_go_back(BlazeState *state) {
    if (state->history_pos > 1) {
        state->history_pos--;
        blaze_navigate(state, state->history[state->history_pos - 1]);
    }
}

/* Go forward in history */
void blaze_go_forward(BlazeState *state) {
    if (state->history_pos < state->history_count) {
        state->history_pos++;
        blaze_navigate(state, state->history[state->history_pos - 1]);
    }
}

/* Reload current page */
void blaze_reload(BlazeState *state) {
    BlazeTab *tab = &state->tabs[state->active_tab];
    blaze_navigate(state, tab->url);
}

/* Add bookmark */
void blaze_add_bookmark(BlazeState *state) {
    if (state->bookmark_count >= BLAZE_MAX_BOOKMARKS) return;
    
    BlazeTab *tab = &state->tabs[state->active_tab];
    blaze_str_copy(state->bookmarks[state->bookmark_count], tab->url, BLAZE_MAX_URL_LEN);
    blaze_str_copy(state->bookmark_titles[state->bookmark_count], tab->title, BLAZE_MAX_TITLE_LEN);
    state->bookmark_count++;
    
    /* Save bookmarks to disk */
    char buf[8192];
    int pos = 0;
    for (int i = 0; i < state->bookmark_count; i++) {
        for (int j = 0; state->bookmarks[i][j] && pos < 8190; j++) {
            buf[pos++] = state->bookmarks[i][j];
        }
        buf[pos++] = '\n';
    }
    buf[pos] = 0;
    save_file_content("/System/bookmarks.txt", buf, pos);
}

/* Load bookmarks from disk */
void blaze_load_bookmarks(BlazeState *state) {
    char buf[8192];
    int len = load_file_content("/System/bookmarks.txt", buf, 8191);
    if (len <= 0) return;
    
    buf[len] = 0;
    state->bookmark_count = 0;
    
    int line_start = 0;
    for (int i = 0; i <= len && state->bookmark_count < BLAZE_MAX_BOOKMARKS; i++) {
        if (buf[i] == '\n' || buf[i] == 0) {
            int line_len = i - line_start;
            if (line_len > 0 && line_len < BLAZE_MAX_URL_LEN) {
                for (int j = 0; j < line_len; j++) {
                    state->bookmarks[state->bookmark_count][j] = buf[line_start + j];
                }
                state->bookmarks[state->bookmark_count][line_len] = 0;
                state->bookmark_count++;
            }
            line_start = i + 1;
        }
    }
}

/* Handle keyboard input */
void blaze_handle_key(BlazeState *state, uint16_t key) {
    uint8_t ascii = key & 0xFF;
    uint8_t scan = (key >> 8) & 0xFF;
    
    BlazeTab *tab = &state->tabs[state->active_tab];
    
    /* Address bar input */
    if (state->address_bar_focused) {
        if (ascii == 27) { /* ESC */
            state->address_bar_focused = false;
            return;
        }
        
        if (ascii == 13) { /* Enter */
            state->address_bar_focused = false;
            blaze_navigate(state, state->address_bar);
            return;
        }
        
        if (ascii == 8 || scan == 0x0E) { /* Backspace */
            if (state->address_bar_cursor > 0) {
                state->address_bar_cursor--;
                state->address_bar[state->address_bar_cursor] = 0;
            }
            return;
        }
        
        if (ascii >= 32 && ascii < 127) {
            if (state->address_bar_cursor < BLAZE_MAX_URL_LEN - 1) {
                state->address_bar[state->address_bar_cursor++] = (char)ascii;
                state->address_bar[state->address_bar_cursor] = 0;
            }
        }
        return;
    }
    
    /* Global shortcuts */
    /* Ctrl+L - Focus address bar */
    if (ascii == 12) { /* Ctrl+L */
        state->address_bar_focused = true;
        state->address_bar[0] = 0;
        state->address_bar_cursor = 0;
        return;
    }
    
    /* Ctrl+T - New tab */
    if (ascii == 20) { /* Ctrl+T */
        blaze_new_tab(state);
        return;
    }
    
    /* Ctrl+W - Close tab */
    if (ascii == 23) { /* Ctrl+W */
        blaze_close_tab(state, state->active_tab);
        return;
    }
    
    /* Ctrl+R - Reload */
    if (ascii == 18) { /* Ctrl+R */
        blaze_reload(state);
        return;
    }
    
    /* F12 - Toggle console */
    if (scan == 0x58) { /* F12 */
        state->console_open = !state->console_open;
        return;
    }
    
    /* Arrow keys for scrolling */
    if (scan == 0x48) { /* Up */
        if (tab->scroll_y > 0) tab->scroll_y -= 40;
        return;
    }
    if (scan == 0x50) { /* Down */
        tab->scroll_y += 40;
        /* Clamping happens in blaze_paint every frame */
        return;
    }
    
    /* Page Up/Down */
    if (scan == 0x49) { /* Page Up */
        if (state->console_open) {
            if (state->console_scroll_y > 0) state->console_scroll_y -= 5;
            if (state->console_scroll_y < 0) state->console_scroll_y = 0;
        } else {
            if (tab->scroll_y > 0) tab->scroll_y -= 400;
            if (tab->scroll_y < 0) tab->scroll_y = 0;
        }
        return;
    }
    if (scan == 0x51) { /* Page Down */
        if (state->console_open) {
            state->console_scroll_y += 5;
        } else {
            int page_h = blaze_get_page_height(state);
            int viewport_h = BLAZE_VIEWPORT_H - 120;
            int max_scroll = (page_h > viewport_h) ? (page_h - viewport_h) : 0;
            
            tab->scroll_y += 400;
            if (tab->scroll_y > max_scroll) tab->scroll_y = max_scroll;
            if (tab->scroll_y < 0) tab->scroll_y = 0;
        }
        return;
    }
}

/* Find body and return its ACTUAL content height.
 * We skip container elements (body, html, div, section, nav, etc.) because
 * blaze_layout.c pads body->h to viewport height. Only leaf/content nodes
 * (text, headings, paragraphs, links, spans, etc.) give us the real bottom. */
int blaze_get_page_height(BlazeState *state) {
    BlazeTab *tab = &state->tabs[state->active_tab];
    if (!tab->document) return 0;

    int max_bottom = 0;
    for (int i = 0; i < tab->node_count; i++) {
        DOMNode *node = &tab->nodes[i];
        if (node->w <= 0 || node->h <= 0) continue;

        /* Skip containers that get padded by layout - they inflate the height */
        if (node->type == NODE_DOCUMENT) continue;
        if (node->type == NODE_ELEMENT) {
            const char *t = node->tag;
            if (blaze_str_cmp(t, "body") == 0 ||
                blaze_str_cmp(t, "html") == 0 ||
                blaze_str_cmp(t, "div") == 0 ||
                blaze_str_cmp(t, "section") == 0 ||
                blaze_str_cmp(t, "nav") == 0 ||
                blaze_str_cmp(t, "header") == 0 ||
                blaze_str_cmp(t, "footer") == 0 ||
                blaze_str_cmp(t, "main") == 0 ||
                blaze_str_cmp(t, "article") == 0) {
                continue;
            }
        }

        int bottom = node->y + node->h;
        if (bottom > max_bottom) max_bottom = bottom;
    }

    return max_bottom;
}

/* Handle mouse input */
void blaze_handle_mouse(BlazeState *state, int mx, int my, bool click) {
    (void)mx; (void)my; (void)click;
    /* TODO: Implement link clicking, scrolling, etc */
}

void blaze_handle_input(BlazeState *state, uint16_t key, int mx, int my, bool click) {
    if (key != 0) {
        blaze_handle_key(state, key);
    }
    if (click) {
        blaze_handle_mouse(state, mx, my, click);
    }
}

/* Render browser UI and content */
void blaze_render(BlazeState *state, void *window) {
    Window *win = (Window *)window;
    BlazeTab *tab = &state->tabs[state->active_tab];
    
    int cw = wm_client_w(win);
    int ch = wm_client_h(win);
    
    /* Toolbar background */
    wm_fill_rect(win, 0, 0, cw, 40, 0xFF000000);
    
    /* Back button */
    wm_fill_rect(win, 8, 8, 24, 24, 0xFF3A3A54);
    wm_draw_string(win, 14, 14, "<", 0xFFE0E0F0, 0xFF3A3A54);
    
    /* Forward button */
    wm_fill_rect(win, 36, 8, 24, 24, 0xFF3A3A54);
    wm_draw_string(win, 42, 14, ">", 0xFFE0E0F0, 0xFF3A3A54);
    
    /* Reload button */
    wm_fill_rect(win, 64, 8, 24, 24, 0xFF3A3A54);
    wm_draw_string(win, 70, 14, "R", 0xFFE0E0F0, 0xFF3A3A54);
    
    /* Address bar */
    int addr_x = 96;
    int addr_w = cw - 230;
    uint32_t addr_bg = state->address_bar_focused ? 0xFFFFFFFF : 0xFF3A3A54;
    uint32_t addr_fg = state->address_bar_focused ? 0xFF000000 : 0xFFE0E0F0;
    
    wm_fill_rect(win, addr_x, 8, addr_w, 24, addr_bg);
    
    const char *addr_text = state->address_bar_focused ? state->address_bar : tab->url;
    wm_draw_string(win, addr_x + 8, 14, addr_text, addr_fg, addr_bg);
    
    /* Cursor in address bar */
    if (state->address_bar_focused && (get_ticks() % 120) < 60) {
        int cx = addr_x + 8 + state->address_bar_cursor * 8;
        wm_fill_rect(win, cx, 10, 2, 20, 0xFF000000);
    }
    
    /* Console button */
    int console_btn_x = cw - 120;
    wm_fill_rect(win, console_btn_x, 8, 70, 24, state->console_open ? 0xFF6366F1 : 0xFF3A3A54);
    wm_draw_string(win, console_btn_x + 8, 14, "CONSOLE", 0xFFE0E0F0, state->console_open ? 0xFF6366F1 : 0xFF3A3A54);

    /* Bookmark button */
    wm_fill_rect(win, cw - 36, 8, 24, 24, 0xFF3A3A54);
    wm_draw_string(win, cw - 30, 14, "*", 0xFFFFD700, 0xFF3A3A54);
    
    /* Tab bar */
    int tab_y = 40;
    int tab_h = 28;
    for (int i = 0; i < state->tab_count; i++) {
        int tab_x = i * 160;
        bool active = (i == state->active_tab);
        uint32_t tab_bg = active ? 0xFF1A1A28 : 0xFF2A2A42;
        
        wm_fill_rect(win, tab_x, tab_y, 158, tab_h, tab_bg);
        wm_fill_rect(win, tab_x + 158, tab_y, 2, tab_h, 0xFF0A0A14);
        
        /* Tab title */
        char title[20];
        int title_len = blaze_str_len(state->tabs[i].title);
        if (title_len > 18) title_len = 18;
        for (int j = 0; j < title_len; j++) title[j] = state->tabs[i].title[j];
        title[title_len] = 0;
        
        wm_draw_string(win, tab_x + 8, tab_y + 10, title, 0xFFE0E0F0, tab_bg);
        
        /* Close button */
        wm_fill_rect(win, tab_x + 134, tab_y + 6, 16, 16, 0xFF3A3A54);
        wm_draw_string(win, tab_x + 138, tab_y + 9, "x", 0xFFE0E0F0, 0xFF3A3A54);
    }
    
    /* New Tab button */
    int new_tab_x = state->tab_count * 160;
    if (new_tab_x < cw - 40) {
        wm_fill_rect(win, new_tab_x, tab_y, 32, tab_h, 0xFF3A3A54);
        wm_draw_string(win, new_tab_x + 12, tab_y + 10, "+", 0xFFE0E0F0, 0xFF3A3A54);
    }
    
    /* Content area */
    int content_y = tab_y + tab_h;
    int content_h = ch - content_y;
    
    if (state->console_open) {
        content_h = content_h * 2 / 3;
    }
    
    /* Render page content */
    if (tab->loading) {
        wm_fill_rect(win, 0, content_y, cw, content_h, 0xFF000000);
        wm_draw_string(win, 20, content_y + 20, "Loading...", 0xFFFFFFFF, 0xFF000000);
    } else {
        blaze_paint(tab, window, tab->scroll_y, content_h);
    }
    
    /* Developer console */
    if (state->console_open) {
        int console_y = content_y + content_h;
        int console_h = ch - console_y;
        
        wm_fill_rect(win, 0, console_y, cw, console_h, 0xFF1A1A28);
        wm_fill_rect(win, 0, console_y, cw, 1, 0xFF6366F1);
        
        wm_draw_string(win, 8, console_y + 8, "Developer Console", 0xFFE0E0F0, 0xFF1A1A28);
        
        /* Console log */
        int log_y = console_y + 24;
        int log_line = 0;
        int col = 0;
        int lines_skipped = 0;
        for (int i = 0; i < state->console_len && log_line < 25; i++) {
            if (state->console_log[i] == '\n') {
                if (lines_skipped < state->console_scroll_y) lines_skipped++;
                else { log_line++; log_y += 12; }
                col = 0;
            } else {
                if (lines_skipped >= state->console_scroll_y) {
                    char c = state->console_log[i];
                    wm_draw_char(win, 8 + col * 8, log_y, (uint8_t)c, 0xFFE0E0F0, 0xFF1A1A28);
                    col++;
                    if (col >= 100) { col = 0; log_line++; log_y += 12; }
                } else {
                    col++;
                    if (col >= 100) { col = 0; lines_skipped++; }
                }
            }
        }
    }
}
