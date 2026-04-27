/*
 * HTTPS (TLS) - basic entry point for port 443 and future TLS 1.2 client.
 * Completes: DNS, TCP connect, teardown. Full handshake (RSA/AES/PRF/verify) is next.
*/

#include "../../include/network_https.h"
#include "../../include/tls12_client.h"
#include <stddef.h>
#include <stdint.h>

extern void c_puts(const char *s);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern uint32_t get_ticks(void);
extern int network_ensure_ready(void);
extern int dns_resolve(const char *hostname);
extern int tcp_connect(uint32_t dest_ip, uint16_t dest_port);
extern int tcp_close(int socket);
extern void netif_poll(void);

static int hdr_name_match(const char *line, int line_len, const char *name) {
    int i = 0;
    while (name[i] && i < line_len) {
        char a = line[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
        i++;
    }
    return name[i] == 0 && i < line_len && line[i] == ':';
}

static int find_http_header_end_hs(const char *buf, uint32_t total) {
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

static int http_header_text_len_hs(const char *buf, int header_end) {
    if (header_end < 2) return 0;
    if (header_end >= 4 && buf[header_end - 4] == '\r' && buf[header_end - 3] == '\n' &&
        buf[header_end - 2] == '\r' && buf[header_end - 1] == '\n')
        return header_end - 4;
    if (buf[header_end - 2] == '\n' && buf[header_end - 1] == '\n')
        return header_end - 2;
    return header_end;
}

static int parse_https_response(char *buf, uint32_t total, char **out_body, uint32_t *out_body_len) {
    if (!buf || total < 16) return -1;
    int header_end = find_http_header_end_hs(buf, total);
    if (header_end < 0) return -1;

    uint32_t off = 0;
    while (off < total && (buf[off] == ' ' || buf[off] == '\t' || buf[off] == '\r' || buf[off] == '\n'))
        off++;
    if (off + 12 > total) return -1;
    const char *b = buf + off;
    if (!((b[0] == 'h' || b[0] == 'H') && (b[1] == 't' || b[1] == 'T') && (b[2] == 't' || b[2] == 'T') &&
          (b[3] == 'p' || b[3] == 'P') && b[4] == '/'))
        return -1;

    int status = 0;
    const char *p = b;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') status = status * 10 + (*p++ - '0');
    if (status < 200 || status >= 300) return -1;

    int32_t content_length = -1;
    const char *scan = buf;
    const char *hend = buf + http_header_text_len_hs(buf, header_end);
    while (scan < hend) {
        const char *line = scan;
        while (scan < hend && *scan != '\r' && *scan != '\n') scan++;
        int ll = (int)(scan - line);
        while (scan < hend && (*scan == '\r' || *scan == '\n')) scan++;
        if (hdr_name_match(line, ll, "Content-Length")) {
            const char *v = line;
            while (v < line + ll && *v != ':') v++;
            if (v < line + ll) v++;
            while (v < line + ll && (*v == ' ' || *v == '\t')) v++;
            int32_t cl = 0;
            while (v < line + ll && *v >= '0' && *v <= '9') cl = cl * 10 + (*v++ - '0');
            content_length = cl;
        }
    }

    uint32_t body_have = total - (uint32_t)header_end;
    uint32_t body_len = body_have;
    if (content_length >= 0 && (uint32_t)content_length < body_have) body_len = (uint32_t)content_length;

    char *body = (char *)kmalloc(body_len + 1);
    if (!body) return -1;
    for (uint32_t i = 0; i < body_len; i++) body[i] = buf[header_end + i];
    body[body_len] = 0;
    *out_body = body;
    *out_body_len = body_len;
    return 0;
}

static int parse_https_url(const char *url, char *host, int host_max, char *path, int path_max,
                           uint16_t *port) {
    if (!url || !host || !path || !port) return -1;
    *port = 443;
    const char *p = url;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] != 'h' && p[0] != 'H') return -1;
    while (*p && *p != ':') p++;
    if (p[0] != ':' || p[1] != '/' || p[2] != '/') return -1;
    p += 3;

    int j = 0;
    while (*p && *p != '/' && *p != ':' && j < host_max - 1) host[j++] = *p++;
    host[j] = 0;

    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') *port = (uint16_t)(*port * 10 + (uint16_t)(*p++ - '0'));
    }

    int pi = 0;
    if (*p == '/') {
        while (*p && pi < path_max - 1) path[pi++] = *p++;
    } else {
        path[pi++] = '/';
    }
    path[pi] = 0;
    return 0;
}

int network_https_fetch(const char *url, char **out_content, uint32_t *out_len) {
    char host[128];
    char path[256];
    uint16_t port = 443;

    if (!url || !out_content || !out_len) return -1;
    *out_content = NULL;
    *out_len = 0;

    if (parse_https_url(url, host, 128, path, 256, &port) < 0) return -1;

    if (network_ensure_ready() != 0) return -1;

    uint32_t ip = dns_resolve(host);
    if (ip == 0) return -1;

    for (int k = 0; k < 12; k++) netif_poll();

    int sock = tcp_connect(ip, port);
    if (sock < 0) return -1;

    int th = tls12_client_handshake(sock, host);
    if (th != 0) {
        if (th == TLS12_ERR_ECDHE)
            c_puts("[HTTPS] Server requires ECDHE (not implemented yet).\n");
        tcp_close(sock);
        tls12_client_reset();
        return -1;
    }

    char request[1536];
    int rq = 0;
    const char *g = "GET ";
    while (*g) request[rq++] = *g++;
    for (int i = 0; path[i] && rq < (int)sizeof(request) - 200; i++) request[rq++] = path[i];
    const char *tail =
        " HTTP/1.1\r\n"
        "Host: ";
    while (*tail) request[rq++] = *tail++;
    for (int i = 0; host[i] && rq < (int)sizeof(request) - 120; i++) request[rq++] = host[i];
    const char *end =
        "\r\n"
        "User-Agent: Mozilla/5.0 (compatible; BlazeBrowser/1.0; AurionOS)\r\n"
        "Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n\r\n";
    while (*end && rq < (int)sizeof(request) - 2) request[rq++] = *end++;

    if (tls12_client_send_app(request, (uint32_t)rq) < 0) {
        tcp_close(sock);
        tls12_client_reset();
        return -1;
    }

    uint32_t cap = 786432;
    char *buf = (char *)kmalloc(cap);
    if (!buf) {
        tcp_close(sock);
        tls12_client_reset();
        return -1;
    }
    uint32_t total = 0;
    int idle_polls = 0;
    while (total + 4096 < cap) {
        for (int i = 0; i < 12; i++) netif_poll();
        int n = tls12_client_recv_app(buf + total, cap - total - 1);
        if (n > 0) {
            total += (uint32_t)n;
            buf[total] = 0;
            idle_polls = 0;
        } else {
            idle_polls++;
            if (idle_polls > 5) break;
        }
    }
    tcp_close(sock);
    tls12_client_reset();
    if (total == 0) {
        kfree(buf);
        return -1;
    }
    char *body = NULL;
    uint32_t body_len = 0;
    int pr = parse_https_response(buf, total, &body, &body_len);
    kfree(buf);
    if (pr < 0) return -1;
    *out_content = body;
    *out_len = body_len;
    return 0;
}
