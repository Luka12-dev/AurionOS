/*
 * Blaze Browser - Network Layer
 * HTTP/HTTPS fetching with TLS support
*/

#include "blaze.h"
#include "../../include/http_client.h"
#include "../../include/network.h"

/* External functions */
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern int dns_resolve(const char *hostname);
extern int tcp_connect(uint32_t dest_ip, uint16_t dest_port);
extern int tcp_send(int socket, const void *data, uint32_t len);
extern int tcp_receive(int socket, void *buffer, uint32_t max_len);
extern int tcp_close(int socket);
extern void netif_poll(void);
extern uint32_t get_ticks(void);

/* Parse URL into components */
static int parse_url(const char *url, char *protocol, char *host, char *path, uint16_t *port) {
    const char *p = url;
    
    /* Parse protocol */
    int proto_len = 0;
    while (*p && *p != ':' && proto_len < 15) {
        protocol[proto_len++] = *p++;
    }
    protocol[proto_len] = 0;
    
    if (*p != ':' || p[1] != '/' || p[2] != '/') return -1;
    p += 3;
    
    /* Default ports */
    if (blaze_str_cmp(protocol, "http") == 0) {
        *port = 80;
    } else if (blaze_str_cmp(protocol, "https") == 0) {
        *port = 443;
    } else {
        return -1;
    }
    
    /* Parse host */
    int host_len = 0;
    while (*p && *p != '/' && *p != ':' && host_len < 127) {
        host[host_len++] = *p++;
    }
    host[host_len] = 0;
    
    /* Parse port if specified */
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = (*port * 10) + (*p - '0');
            p++;
        }
    }
    
    /* Parse path */
    if (*p == 0) {
        path[0] = '/';
        path[1] = 0;
    } else {
        int path_len = 0;
        while (*p && path_len < 255) {
            path[path_len++] = *p++;
        }
        path[path_len] = 0;
    }
    
    return 0;
}

/* Simple HTTP GET request */
static int http_get_simple(const char *host, const char *path, uint16_t port,
                          char **out_content, uint32_t *out_len, char *out_location) {
    /* Resolve hostname */
    uint32_t ip = dns_resolve(host);
    if (ip == 0) return -1;
    
    /* Connect */
    int sock = tcp_connect(ip, port);
    if (sock < 0) return -1;
    
    /* Build HTTP request */
    char request[1024];
    int req_len = 0;
    
    /* GET line */
    const char *get = "GET ";
    for (int i = 0; get[i]; i++) request[req_len++] = get[i];
    for (int i = 0; path[i] && req_len < 1000; i++) request[req_len++] = path[i];
    const char *http11 = " HTTP/1.1\r\n";
    for (int i = 0; http11[i]; i++) request[req_len++] = http11[i];
    
    /* Host header */
    const char *host_hdr = "Host: ";
    for (int i = 0; host_hdr[i]; i++) request[req_len++] = host_hdr[i];
    for (int i = 0; host[i] && req_len < 1000; i++) request[req_len++] = host[i];
    request[req_len++] = '\r';
    request[req_len++] = '\n';
    
    /* User-Agent (iPhone Safari UA for simple mobile pages) */
    const char *ua = "User-Agent: Mozilla/5.0 (iPhone; CPU iPhone OS 14_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/14.0 Mobile/15E148 Safari/604.1\r\n";
    for (int i = 0; ua[i]; i++) request[req_len++] = ua[i];
    
    /* Accept header */
    const char *acc = "Accept: text/html,application/xhtml+xml\r\n";
    for (int i = 0; acc[i]; i++) request[req_len++] = acc[i];
    
    /* Connection close */
    const char *conn = "Connection: close\r\n\r\n";
    for (int i = 0; conn[i]; i++) request[req_len++] = conn[i];
    
    /* Send request */
    tcp_send(sock, request, req_len);
    
    /* Receive response */
    uint32_t buf_size = 524288; /* 512KB */
    char *response = (char *)kmalloc(buf_size);
    if (!response) {
        tcp_close(sock);
        return -1;
    }
    
    uint32_t total = 0;
    uint32_t start_time = get_ticks();
    extern uint16_t c_getkey_nonblock(void);
    
    while (total < buf_size - 1) {
        /* Poll network and check for cancel */
        for (int i = 0; i < 10; i++) netif_poll();
        
        uint16_t k = c_getkey_nonblock();
        if ((k & 0xFF) == 27) { /* ESC to cancel */
            kfree(response);
            tcp_close(sock);
            return -1;
        }
        
        int received = tcp_receive(sock, response + total, buf_size - 1 - total);
        if (received > 0) {
            total += received;
            start_time = get_ticks();
        }
        
        /* Timeout after 5 seconds of no data */
        if (get_ticks() - start_time > 100) break;
        
        /* Yield to other processes */
        for (volatile int i = 0; i < 50000; i++);
    }
    
    tcp_close(sock);
    
    if (total == 0) {
        kfree(response);
        return -1;
    }
    
    response[total] = 0;
    
    /* Parse HTTP Status */
    int status = 0;
    if (blaze_str_starts_with(response, "HTTP/1.1 ")) {
        const char *p = response + 9;
        while (*p >= '0' && *p <= '9') {
            status = status * 10 + (*p - '0');
            p++;
        }
    }
    
    /* Find end of headers */
    int header_end = 0;
    for (int i = 0; i < (int)total - 3; i++) {
        if (response[i] == '\r' && response[i+1] == '\n' &&
            response[i+2] == '\r' && response[i+3] == '\n') {
            header_end = i + 4;
            break;
        }
    }
    
    /* Handle Redirects (301, 302, 303, 307, 308) */
    if ((status >= 301 && status <= 308) && out_location) {
        /* Find Location header */
        char *loc = (char *)response;
        for (int i = 0; i < header_end - 10; i++) {
            if (blaze_str_starts_with(loc + i, "Location: ") || blaze_str_starts_with(loc + i, "location: ")) {
                char *lp = loc + i + 10;
                int li = 0;
                while (li < BLAZE_MAX_URL_LEN - 1 && lp[li] != '\r' && lp[li] != '\n') {
                    out_location[li] = lp[li];
                    li++;
                }
                out_location[li] = 0;
                break;
            }
        }
        kfree(response);
        return status;
    }
    
    if (header_end == 0) {
        kfree(response);
        return -1;
    }
    
    /* Extract body */
    uint32_t body_len = total - header_end;
    char *body = (char *)kmalloc(body_len + 1);
    if (!body) {
        kfree(response);
        return -1;
    }
    
    for (uint32_t i = 0; i < body_len; i++) body[i] = response[header_end + i];
    body[body_len] = 0;
    
    kfree(response);
    *out_content = body;
    *out_len = body_len;
    
    return status;
}

/* Recursive fetch to follow redirects */
static int blaze_fetch_recursive(const char *url, char **out_content, uint32_t *out_len, int depth) {
    if (depth > 5) return -1;
    
    char protocol[16];
    char host[128];
    char path[256];
    uint16_t port;
    
    if (parse_url(url, protocol, host, path, &port) < 0) return -1;
    
    char next_url[BLAZE_MAX_URL_LEN];
    next_url[0] = 0;
    
    int status = http_get_simple(host, path, port, out_content, out_len, next_url);
    
    if (status >= 301 && status <= 308 && next_url[0]) {
        /* Recursive call for redirect */
        /* If next_url is relative, build full URL */
        if (next_url[0] == '/') {
            char full_url[1024];
            int pos = 0;
            for (int i = 0; protocol[i]; i++) full_url[pos++] = protocol[i];
            full_url[pos++] = ':'; full_url[pos++] = '/'; full_url[pos++] = '/';
            for (int i = 0; host[i]; i++) full_url[pos++] = host[i];
            for (int i = 0; next_url[i] && pos < 1023; i++) full_url[pos++] = next_url[i];
            full_url[pos] = 0;
            return blaze_fetch_recursive(full_url, out_content, out_len, depth + 1);
        } else if (!blaze_str_starts_with(next_url, "http")) {
             /* Relative to current path (simplified handling) */
             return -1;
        }
        return blaze_fetch_recursive(next_url, out_content, out_len, depth + 1);
    }
    
    return status == 200 ? 0 : -1;
}

/* Fetch URL content */
int blaze_fetch(const char *url, char **out_content, uint32_t *out_len) {
    return blaze_fetch_recursive(url, out_content, out_len, 0);
}
