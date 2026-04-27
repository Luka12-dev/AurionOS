/*
 * Blaze Browser - Network Layer
 * HTTP fetch: Content-Length, chunked transfer, redirects, relative Location URLs
 */

#include "blaze.h"
#include "../../include/network.h"
#include "../../include/network_https.h"

extern int network_ensure_ready(void);

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern int dns_resolve(const char *hostname);
extern int tcp_connect(uint32_t dest_ip, uint16_t dest_port);
extern int tcp_send(int socket, const void *data, uint32_t len);
extern int tcp_receive(int socket, void *buffer, uint32_t max_len);
extern int tcp_close(int socket);
extern void netif_poll(void);
extern uint32_t get_ticks(void);
extern uint16_t c_getkey_nonblock(void);

/* --- case helpers -------------------------------------------------------- */
static void str_lower(char *s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
}

static bool hdr_name_match(const char *line, int line_len, const char *name) {
    int i = 0;
    while (name[i] && i < line_len) {
        char a = line[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
        i++;
    }
    return name[i] == 0 && i < line_len && line[i] == ':';
}

static bool url_is_https_scheme(const char *url) {
    if (!url || !url[0]) return false;
    char c0 = url[0], c1 = url[1], c2 = url[2], c3 = url[3], c4 = url[4];
    if (c0 >= 'A' && c0 <= 'Z') c0 = (char)(c0 + 32);
    if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 32);
    if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 32);
    if (c3 >= 'A' && c3 <= 'Z') c3 = (char)(c3 + 32);
    if (c4 >= 'A' && c4 <= 'Z') c4 = (char)(c4 + 32);
    return c0 == 'h' && c1 == 't' && c2 == 't' && c3 == 'p' && c4 == 's' && url[5] == ':' &&
           url[6] == '/' && url[7] == '/';
}

static bool line_has_chunked(const char *s, int len) {
    for (int i = 0; i + 7 <= len; i++) {
        char c0 = s[i], c1 = s[i + 1];
        if (c0 >= 'A' && c0 <= 'Z') c0 += 32;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c0 == 'c' && c1 == 'h' && s[i + 2] == 'u' && s[i + 3] == 'n' &&
            s[i + 4] == 'k' && s[i + 5] == 'e' && s[i + 6] == 'd')
            return true;
    }
    return false;
}

static int parse_url(const char *url, char *protocol, char *host, char *path, uint16_t *port) {
    const char *p = url;

    int proto_len = 0;
    while (*p && *p != ':' && proto_len < 15) protocol[proto_len++] = *p++;
    protocol[proto_len] = 0;
    str_lower(protocol);

    if (*p != ':' || p[1] != '/' || p[2] != '/') return -1;
    p += 3;

    if (blaze_str_cmp(protocol, "http") == 0) *port = 80;
    else if (blaze_str_cmp(protocol, "https") == 0) *port = 443;
    else return -1;

    int host_len = 0;
    while (*p && *p != '/' && *p != ':' && host_len < 127) host[host_len++] = *p++;
    host[host_len] = 0;

    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') *port = (uint16_t)(*port * 10 + (*p++ - '0'));
    }

    if (*p == 0) {
        path[0] = '/';
        path[1] = 0;
    } else {
        int path_len = 0;
        while (*p && path_len < 255) path[path_len++] = *p++;
        path[path_len] = 0;
    }
    return 0;
}

/* Build absolute URL from base URL and relative reference */
static void resolve_relative_url(const char *base, const char *rel, char *out, int max_out) {
    char prot[16], host[128], path[256];
    uint16_t port = 80;
    if (parse_url(base, prot, host, path, &port) < 0) {
        blaze_str_copy(out, rel, max_out);
        return;
    }

    if (rel[0] == '/' && rel[1] == '/') {
        int pos = 0;
        for (int i = 0; prot[i] && pos < max_out - 1; i++) out[pos++] = prot[i];
        out[pos++] = ':';
        out[pos++] = '/';
        out[pos++] = '/';
        for (int i = 2; rel[i] && pos < max_out - 1; i++) out[pos++] = rel[i];
        out[pos] = 0;
        return;
    }

    if (rel[0] == '/') {
        int pos = 0;
        for (int i = 0; prot[i] && pos < max_out - 4; i++) out[pos++] = prot[i];
        out[pos++] = ':'; out[pos++] = '/'; out[pos++] = '/';
        for (int i = 0; host[i] && pos < max_out - 1; i++) out[pos++] = host[i];
        for (int i = 0; rel[i] && pos < max_out - 1; i++) out[pos++] = rel[i];
        out[pos] = 0;
        return;
    }

    int plen = blaze_str_len(path);
    int last_slash = 0;
    for (int i = 0; i < plen; i++)
        if (path[i] == '/') last_slash = i;

    char dir[256];
    int dlen = (plen == 0) ? 1 : last_slash + 1;
    if (dlen > 255) dlen = 255;
    for (int i = 0; i < dlen; i++) dir[i] = path[i];
    dir[dlen] = 0;
    if (dlen == 0) { dir[0] = '/'; dir[1] = 0; dlen       = 1; }

    int pos = 0;
    for (int i = 0; prot[i] && pos < max_out - 4; i++) out[pos++] = prot[i];
    out[pos++] = ':'; out[pos++] = '/'; out[pos++] = '/';
    for (int i = 0; host[i] && pos < max_out - 1; i++) out[pos++] = host[i];
    for (int i = 0; dir[i] && pos < max_out - 1; i++) out[pos++] = dir[i];
    if (pos > 0 && out[pos - 1] != '/' && rel[0] != '/' && pos < max_out - 1)
        out[pos++] = '/';
    for (int i = 0; rel[i] && pos < max_out - 1; i++) out[pos++] = rel[i];
    out[pos] = 0;
}

/*
 * End of headers: MUST prefer \r\n\r\n first. Taking min(\r\n\r\n, \n\n) is wrong — the first
 * \n\n in the buffer is often inside the HTML body, which mis-parses headers and can hang the UI.
 */
static int find_http_header_end(const char *buf, uint32_t total) {
    for (uint32_t i = 0; i + 3 < total; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return (int)i + 4;
    }
    for (uint32_t i = 0; i + 1 < total; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n')
            return (int)i + 2;
    }
    return -1;
}

/* Byte length of header block excluding the blank line before the body. */
static int http_header_text_len(const char *buf, int header_end) {
    if (header_end < 2) return 0;
    if (header_end >= 4 && buf[header_end - 4] == '\r' && buf[header_end - 3] == '\n' &&
        buf[header_end - 2] == '\r' && buf[header_end - 1] == '\n')
        return header_end - 4;
    if (buf[header_end - 2] == '\n' && buf[header_end - 1] == '\n')
        return header_end - 2;
    return header_end;
}

static bool line_starts_with_http_version(const char *b) {
    return (b[0] == 'h' || b[0] == 'H') && (b[1] == 't' || b[1] == 'T') && (b[2] == 't' || b[2] == 'T') &&
           (b[3] == 'p' || b[3] == 'P') && b[4] == '/';
}

/* Parse status + headers (non-destructive). */
static void parse_http_headers(const char *buf, uint32_t total, int *status, int32_t *content_length,
                               bool *chunked, int *header_end) {
    *status = 0;
    *content_length = -1;
    *chunked = false;
    *header_end = 0;

    if (total < 12) return;

    uint32_t off = 0;
    while (off < total && (buf[off] == ' ' || buf[off] == '\t' || buf[off] == '\r' || buf[off] == '\n'))
        off++;
    if (off + 12 > total) return;
    const char *b = buf + off;
    if (!line_starts_with_http_version(b)) return;

    const char *p = b;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') *status = *status * 10 + (*p++ - '0');

    *header_end = find_http_header_end(buf, total);
    if (*header_end <= 0) return;
    if ((uint32_t)*header_end > total) *header_end = (int)total;

    int htext = http_header_text_len(buf, *header_end);
    if (htext < 0) htext = 0;
    if ((uint32_t)htext > total) htext = (int)total;
    const char *scan = buf;
    const char *hdr_end = buf + htext;

    while (scan < hdr_end) {
        const char *line = scan;
        while (scan < hdr_end && *scan != '\r' && *scan != '\n') scan++;
        int ll = (int)(scan - line);
        while (scan < hdr_end && (*scan == '\r' || *scan == '\n')) scan++;

        if (hdr_name_match(line, ll, "Content-Length")) {
            const char *v = line;
            while (v < line + ll && *v != ':') v++;
            if (v < line + ll && *v == ':') {
                v++;
                while (v < line + ll && (*v == ' ' || *v == '\t')) v++;
                uint32_t cl = 0;
                while (v < line + ll && *v >= '0' && *v <= '9')
                    cl = cl * 10u + (uint32_t)(*v++ - '0');
                *content_length = (int32_t)cl;
            }
        }
        if (hdr_name_match(line, ll, "Transfer-Encoding") && line_has_chunked(line, ll))
            *chunked = true;
    }
}

/* Decode chunked body at src[0..src_len) into new buffer */
static int decode_chunked_body(const char *src, uint32_t src_len, char **out_body, uint32_t *out_len) {
    const char *p = src;
    const char *end = src + src_len;
    char *acc = (char *)kmalloc(src_len + 64);
    if (!acc) return -1;
    uint32_t acc_len = 0;

    while (p < end) {
        /* chunk size line */
        uint32_t chunk_sz = 0;
        bool saw_hex = false;
        while (p < end && *p != '\r' && *p != '\n') {
            char c = *p++;
            if (c == ';') {
                while (p < end && *p != '\r' && *p != '\n') p++;
                break;
            }
            if (c >= '0' && c <= '9') {
                chunk_sz = chunk_sz * 16u + (uint32_t)(c - '0');
                saw_hex = true;
            } else if (c >= 'a' && c <= 'f') {
                chunk_sz = chunk_sz * 16u + (uint32_t)(10 + c - 'a');
                saw_hex = true;
            } else if (c >= 'A' && c <= 'F') {
                chunk_sz = chunk_sz * 16u + (uint32_t)(10 + c - 'A');
                saw_hex = true;
            } else if (c == '\t' || c == ' ')
                continue;
        }
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;

        if (!saw_hex && chunk_sz == 0 && p >= end) break;

        if (chunk_sz == 0) {
            /* trailers optional — skip final CRLF */
            if (p < end && *p == '\r') p++;
            if (p < end && *p == '\n') p++;
            break;
        }

        if ((uint32_t)(end - p) < chunk_sz + 2) {
            kfree(acc);
            return -1;
        }
        for (uint32_t i = 0; i < chunk_sz; i++) acc[acc_len++] = *p++;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    acc[acc_len] = 0;
    *out_body = acc;
    *out_len = acc_len;
    return 0;
}

/*
 * HTTP/1.1 GET — read full entity body using Content-Length, chunked, or until close.
 * Returns HTTP status; on 200 sets *out_content / *out_len. On redirect fills out_location.
 */
static int http_get_simple(const char *host, const char *path, uint16_t port,
                           char **out_content, uint32_t *out_len, char *out_location) {
    uint32_t ip = dns_resolve(host);
    if (ip == 0) return -1;

    for (int k = 0; k < 12; k++) netif_poll();

    int sock = tcp_connect(ip, port);
    if (sock < 0) return -1;

    char request[1536];
    int req_len = 0;
    const char *get = "GET ";
    while (*get) request[req_len++] = *get++;
    for (int i = 0; path[i] && req_len < (int)sizeof(request) - 200; i++) request[req_len++] = path[i];
    const char *tail =
        " HTTP/1.1\r\n"
        "Host: ";
    while (*tail) request[req_len++] = *tail++;
    for (int i = 0; host[i] && req_len < (int)sizeof(request) - 120; i++) request[req_len++] = host[i];
    const char *mid =
        "\r\n"
        "User-Agent: Mozilla/5.0 (compatible; BlazeBrowser/1.0; AurionOS)\r\n"
        "Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n\r\n";
    while (*mid && req_len < (int)sizeof(request) - 2) request[req_len++] = *mid++;

    tcp_send(sock, request, req_len);

    uint32_t buf_cap = 786432; /* 768 KiB */
    char *buf = (char *)kmalloc(buf_cap);
    if (!buf) {
        tcp_close(sock);
        return -1;
    }

    uint32_t total = 0;
    int idle_polls = 0;
    const int idle_limit = 5;
    bool had_data = false;
    int safety_iters = 0;
    const int safety_max = 4096; /* cap outer loop even if parser state is wrong */

    while (total + 4096 < buf_cap && safety_iters < safety_max) {
        safety_iters++;
        for (int i = 0; i < 12; i++) netif_poll();

        uint16_t k = c_getkey_nonblock();
        if ((k & 0xFF) == 27) {
            kfree(buf);
            tcp_close(sock);
            return -1;
        }

        int n = tcp_receive(sock, buf + total, buf_cap - total - 1);
        if (n > 0) {
            total += (uint32_t)n;
            buf[total] = 0;
            idle_polls = 0;
            had_data = true;
        } else {
            idle_polls++;
            if (had_data && idle_polls > idle_limit) break;
            if (!had_data && idle_polls > idle_limit) break;
        }

        int status = 0, hdr_end = 0;
        int32_t content_length = -1;
        bool chunked = false;
        parse_http_headers(buf, total, &status, &content_length, &chunked, &hdr_end);

        if (hdr_end > 0 && status >= 301 && status <= 308 && out_location) {
            const char *hscan = buf;
            const char *hend = buf + http_header_text_len(buf, hdr_end);
            out_location[0] = 0;
            while (hscan < hend) {
                const char *ln = hscan;
                while (hscan < hend && *hscan != '\r' && *hscan != '\n') hscan++;
                int llen = (int)(hscan - ln);
                while (hscan < hend && (*hscan == '\r' || *hscan == '\n')) hscan++;
                if (hdr_name_match(llen > 0 ? ln : "", llen, "Location")) {
                    const char *v = ln;
                    while (v < ln + llen && *v != ':') v++;
                    if (v < ln + llen && *v == ':') {
                        v++;
                        while (v < ln + llen && (*v == ' ' || *v == '\t')) v++;
                        int li = 0;
                        while (li < BLAZE_MAX_URL_LEN - 1 && v < ln + llen &&
                               *v != '\r' && *v != '\n') {
                            out_location[li++] = *v++;
                        }
                        out_location[li] = 0;
                    }
                    break;
                }
            }
            kfree(buf);
            tcp_close(sock);
            return status;
        }

        if (hdr_end > 0 && status == 200) {
            uint32_t have_body = total - (uint32_t)hdr_end;
            if (!chunked && content_length >= 0) {
                if (have_body >= (uint32_t)content_length) break;
            } else if (chunked) {
                /* Heuristic: find 0\r\n\r\n terminator */
                const char *body = buf + hdr_end;
                const char *z = body;
                bool ok = false;
                while (z + 5 <= buf + total) {
                    if (z[0] == '0' && z[1] == '\r' && z[2] == '\n' && z[3] == '\r' &&
                        z[4] == '\n') {
                        ok = true;
                        break;
                    }
                    z++;
                }
                if (ok) break;
            } else {
                /* no CL, not chunked: read until idle timeout (already in loop) */
                if (n <= 0 && had_data && idle_polls > 2) break;
            }
        }

        for (volatile int w = 0; w < 8000; w++) {}
    }

    tcp_close(sock);

    if (total == 0) {
        kfree(buf);
        return -1;
    }

    buf[total] = 0;

    int status = 0, hdr_end = 0;
    int32_t content_length = -1;
    bool chunked = false;
    parse_http_headers(buf, total, &status, &content_length, &chunked, &hdr_end);

    if (hdr_end <= 0) {
        kfree(buf);
        return -1;
    }

    if (status < 200 || status >= 300) {
        kfree(buf);
        return status;
    }

    char *body_src = buf + hdr_end;
    uint32_t body_avail = total - (uint32_t)hdr_end;
    char *final_body = NULL;
    uint32_t final_len = 0;

    if (chunked) {
        if (decode_chunked_body(body_src, body_avail, &final_body, &final_len) < 0) {
            kfree(buf);
            return -1;
        }
        kfree(buf);
    } else if (content_length >= 0) {
        uint32_t want = (uint32_t)content_length;
        if (want > body_avail) want = body_avail;
        final_body = (char *)kmalloc(want + 1);
        if (!final_body) {
            kfree(buf);
            return -1;
        }
        for (uint32_t i = 0; i < want; i++) final_body[i] = body_src[i];
        final_body[want] = 0;
        final_len = want;
        kfree(buf);
    } else {
        final_body = (char *)kmalloc(body_avail + 1);
        if (!final_body) {
            kfree(buf);
            return -1;
        }
        for (uint32_t i = 0; i < body_avail; i++) final_body[i] = body_src[i];
        final_body[body_avail] = 0;
        final_len = body_avail;
        kfree(buf);
    }

    *out_content = final_body;
    *out_len = final_len;
    return status;
}

static int blaze_fetch_recursive(const char *url, char **out_content, uint32_t *out_len, int depth) {
    if (depth > 8) return -1;

    if (network_ensure_ready() != 0)
        return -1;

    char protocol[16], host[128], path[256];
    uint16_t port = 80;

    if (parse_url(url, protocol, host, path, &port) < 0) return -1;

    /* Prefer real HTTPS when TLS client completes; else try plain HTTP on port 80 */
    if (blaze_str_cmp(protocol, "https") == 0) {
        if (network_https_fetch(url, out_content, out_len) == 0) return 0;
        char down[BLAZE_MAX_URL_LEN];
        blaze_str_copy(down, "http://", BLAZE_MAX_URL_LEN);
        blaze_str_copy(down + 7, host, BLAZE_MAX_URL_LEN - 7);
        int pl2 = blaze_str_len(down);
        for (int i = 0; path[i] && pl2 < BLAZE_MAX_URL_LEN - 1; i++) down[pl2++] = path[i];
        down[pl2] = 0;
        return blaze_fetch_recursive(down, out_content, out_len, depth + 1);
    }

    char next_url[BLAZE_MAX_URL_LEN];
    next_url[0] = 0;

    int status = http_get_simple(host, path, port, out_content, out_len, next_url);

    if (status >= 301 && status <= 308 && next_url[0]) {
        char follow[BLAZE_MAX_URL_LEN];

        if (next_url[0] == '/') {
            int pos = 0;
            for (int i = 0; protocol[i] && pos < BLAZE_MAX_URL_LEN - 4; i++) follow[pos++] = protocol[i];
            follow[pos++] = ':';
            follow[pos++] = '/';
            follow[pos++] = '/';
            for (int i = 0; host[i] && pos < BLAZE_MAX_URL_LEN - 1; i++) follow[pos++] = host[i];
            for (int i = 0; next_url[i] && pos < BLAZE_MAX_URL_LEN - 1; i++) follow[pos++] = next_url[i];
            follow[pos] = 0;
        } else if (url_is_https_scheme(next_url)) {
            /* Follow TLS URL (TLS client); do not rewrite to http — that loops on HTTP→HTTPS sites */
            blaze_str_copy(follow, next_url, BLAZE_MAX_URL_LEN);
        } else if (blaze_str_starts_with(next_url, "http://") || blaze_str_starts_with(next_url, "HTTP://")) {
            blaze_str_copy(follow, next_url, BLAZE_MAX_URL_LEN);
        } else {
            resolve_relative_url(url, next_url, follow, BLAZE_MAX_URL_LEN);
        }

        return blaze_fetch_recursive(follow, out_content, out_len, depth + 1);
    }

    /* Plain HTTP failed: many hosts only speak HTTPS on 443 */
    if (status == -1) {
        char https_url[BLAZE_MAX_URL_LEN];
        int pos = 0;
        const char *pre = "https://";
        while (*pre && pos < BLAZE_MAX_URL_LEN - 1) https_url[pos++] = *pre++;
        for (int i = 0; host[i] && pos < BLAZE_MAX_URL_LEN - 1; i++) https_url[pos++] = host[i];
        if (port != 80) {
            https_url[pos++] = ':';
            uint16_t po = port;
            char tmp[8];
            int n = 0;
            while (po > 0 && n < 7) {
                tmp[n++] = (char)('0' + (po % 10));
                po = (uint16_t)(po / 10);
            }
            while (n > 0 && pos < BLAZE_MAX_URL_LEN - 1) https_url[pos++] = tmp[--n];
        }
        for (int i = 0; path[i] && pos < BLAZE_MAX_URL_LEN - 1; i++) https_url[pos++] = path[i];
        https_url[pos] = 0;
        if (network_https_fetch(https_url, out_content, out_len) == 0) return 0;
    }

    return status == 200 ? 0 : -1;
}

int blaze_fetch(const char *url, char **out_content, uint32_t *out_len) {
    return blaze_fetch_recursive(url, out_content, out_len, 0);
}
