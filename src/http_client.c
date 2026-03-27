/*
 * HTTP Client Implementation for AurionOS
 * Built on top of the TCP/IP stack
*/

#include "../include/http_client.h"
#include "../include/network.h"
#include <stddef.h>

/* External functions from kernel */
extern void c_puts(const char *s);
extern void c_putc(char c);
extern uint32_t get_ticks(void);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

/* External TCP/IP functions */
extern int dns_resolve(const char *hostname);
extern int tcp_connect(uint32_t dest_ip, uint16_t dest_port);
extern int tcp_send(int socket, const void *data, uint32_t len);
extern int tcp_receive(int socket, void *buffer, uint32_t max_len);
extern int tcp_close(int socket);
extern void netif_poll(void);

/* String helper functions */
static int http_strlen(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    return len;
}

static int http_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *(unsigned char *)a - *(unsigned char *)b;
}

/* Case-insensitive string comparison for headers */
static int http_strcasecmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return (unsigned char)ca - (unsigned char)cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void http_strcpy(char *dest, const char *src)
{
    while ((*dest++ = *src++))
        ;
}

static void http_strncpy(char *dest, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i])
    {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* Check if string starts with prefix (case-insensitive) */
static int http_startswith_ci(const char *str, const char *prefix)
{
    while (*prefix)
    {
        char s = *str;
        char p = *prefix;
        if (s >= 'A' && s <= 'Z')
            s += 32;
        if (p >= 'A' && p <= 'Z')
            p += 32;
        if (s != p)
            return 0;
        str++;
        prefix++;
    }
    return 1;
}

/* Parse URL into components
 * Supports: http://host/path, http://host:port/path
 * Returns 0 on success, -1 on error
*/

int http_parse_url(const char *url, char *host, int host_max,
                   char *path, int path_max, uint16_t *port)
{
    if (!url || !host || !path || !port)
        return -1;

    /* Default port */
    *port = 80;

    /* Skip "http://" prefix */
    if (http_startswith_ci(url, "http://"))
    {
        url += 7;
    }
    else if (http_startswith_ci(url, "https://"))
    {
        c_puts("[HTTP] HTTPS not supported, use HTTP URLs only\n");
        return -1;
    }

    /* Find end of host (first '/' or ':' or end of string) */
    int host_len = 0;
    const char *p = url;
    while (*p && *p != '/' && *p != ':' && host_len < host_max - 1)
    {
        host[host_len++] = *p++;
    }
    host[host_len] = '\0';

    /* Check for port specification */
    if (*p == ':')
    {
        p++; /* Skip ':' */
        *port = 0;
        while (*p >= '0' && *p <= '9')
        {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }

    /* Path is everything after host (and optional port) */
    if (*p == '/')
    {
        http_strncpy(path, p, path_max);
    }
    else
    {
        /* No path specified, use root */
        path[0] = '/';
        path[1] = '\0';
    }

    return 0;
}

/* Build HTTP GET request */
static int http_build_request(const http_request_t *req, char *buffer, int max_len)
{
    int len = 0;

    /* Request line */
    len += http_strlen("GET ");
    if (len >= max_len)
        return -1;

    http_strcpy(buffer, "GET ");
    len = http_strlen(buffer);

    /* Path */
    int path_len = http_strlen(req->path);
    if (len + path_len >= max_len)
        return -1;
    http_strcpy(buffer + len, req->path);
    len += path_len;

    /* HTTP/1.1 version and Host header (required for HTTP/1.1) */
    const char *http_line = " HTTP/1.1\r\nHost: ";
    int line_len = http_strlen(http_line);
    if (len + line_len >= max_len)
        return -1;
    http_strcpy(buffer + len, http_line);
    len += line_len;

    /* Host */
    int host_len = http_strlen(req->host);
    if (len + host_len >= max_len)
        return -1;
    http_strcpy(buffer + len, req->host);
    len += host_len;

    /* Add User-Agent */
    const char *ua = "\r\nUser-Agent: AurionOS/1.0";
    int ua_len = http_strlen(ua);
    if (len + ua_len >= max_len)
        return -1;
    http_strcpy(buffer + len, ua);
    len += ua_len;

    /* Add Connection: close (simpler than keep-alive) */
    const char *conn = "\r\nConnection: close";
    int conn_len = http_strlen(conn);
    if (len + conn_len >= max_len)
        return -1;
    http_strcpy(buffer + len, conn);
    len += conn_len;

    /* Add custom headers */
    for (int i = 0; i < req->custom_header_count && i < HTTP_MAX_HEADERS; i++)
    {
        int hdr_len = http_strlen(req->custom_headers[i]);
        if (len + hdr_len + 2 >= max_len)
            break;
        buffer[len++] = '\r';
        buffer[len++] = '\n';
        http_strcpy(buffer + len, req->custom_headers[i]);
        len += hdr_len;
    }

    /* End headers */
    if (len + 4 >= max_len)
        return -1;
    buffer[len++] = '\r';
    buffer[len++] = '\n';
    buffer[len++] = '\r';
    buffer[len++] = '\n';
    buffer[len] = '\0';

    return len;
}

/* Find substring in string */
static const char *http_strstr(const char *haystack, const char *needle)
{
    while (*haystack)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n)
        {
            h++;
            n++;
        }
        if (*n == '\0')
            return haystack;
        haystack++;
    }
    return NULL;
}

/* Parse HTTP response headers */
static int http_parse_response(const char *raw, int raw_len, http_response_t *response)
{
    if (!raw || !response || raw_len < 16)
        return -1;

    /* Initialize response */
    response->status_code = 0;
    response->status_text[0] = '\0';
    response->header_count = 0;
    response->body_len = 0;
    response->content_length = 0;
    response->chunked = HTTP_FALSE;

    /* Parse status line: HTTP/1.1 200 OK */
    const char *p = raw;

    /* Skip "HTTP/1.x " */
    while (*p && *p != ' ')
        p++;
    if (*p != ' ')
        return -1;
    p++; /* Skip space */

    /* Parse status code */
    while (*p >= '0' && *p <= '9')
    {
        response->status_code = response->status_code * 10 + (*p - '0');
        p++;
    }

    /* Skip space */
    if (*p == ' ')
        p++;

    /* Parse status text until \r or \n */
    int i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < 63)
    {
        response->status_text[i++] = *p++;
    }
    response->status_text[i] = '\0';

    /* Skip to next line */
    if (*p == '\r')
        p++;
    if (*p == '\n')
        p++;

    /* Parse headers until empty line */
    while (*p && *p != '\r' && *p != '\n' && response->header_count < HTTP_MAX_HEADERS)
    {
        /* Read header line */
        int hdr_len = 0;
        const char *hdr_start = p;
        while (*p && *p != '\r' && *p != '\n' && hdr_len < HTTP_MAX_HEADER_LEN - 1)
        {
            response->headers[response->header_count][hdr_len++] = *p++;
        }
        response->headers[response->header_count][hdr_len] = '\0';
        response->header_count++;

        /* Check for Content-Length */
        if (http_startswith_ci(hdr_start, "Content-Length:"))
        {
            const char *val = hdr_start;
            while (*val && *val != ':')
                val++;
            if (*val == ':')
                val++;
            while (*val == ' ')
                val++;
            while (*val >= '0' && *val <= '9')
            {
                response->content_length = response->content_length * 10 + (*val - '0');
                val++;
            }
        }

        /* Check for Transfer-Encoding: chunked */
        if (http_startswith_ci(hdr_start, "Transfer-Encoding:") &&
            http_strstr(hdr_start, "chunked"))
        {
            response->chunked = HTTP_TRUE;
        }

        /* Skip line ending */
        if (*p == '\r')
            p++;
        if (*p == '\n')
            p++;
    }

    /* Skip empty line after headers */
    if (*p == '\r')
        p++;
    if (*p == '\n')
        p++;

    /* Body starts here */
    int body_offset = p - raw;
    int body_len = raw_len - body_offset;
    if (body_len > 0 && body_len < HTTP_MAX_BODY_LEN)
    {
        for (i = 0; i < body_len; i++)
        {
            response->body[i] = p[i];
        }
        response->body_len = body_len;
        response->body[body_len] = '\0';
    }

    return 0;
}

/* Perform HTTP GET request */
int http_get(const char *url, http_response_t *response)
{
    http_request_t req;
    req.port = 80;
    req.custom_header_count = 0;
    req.timeout_ms = 30000; /* 30 second default timeout */

    if (http_parse_url(url, req.host, HTTP_MAX_HOST_LEN,
                       req.path, HTTP_MAX_PATH_LEN, &req.port) < 0)
    {
        c_puts("[HTTP] Failed to parse URL\n");
        return -1;
    }

    return http_get_ex(&req, response);
}

/* Perform HTTP GET request with custom configuration */
int http_get_ex(const http_request_t *request, http_response_t *response)
{
    if (!request || !response)
        return -1;

    char request_buf[1024];
    char response_buf[HTTP_MAX_RESPONSE_LEN];
    int response_len = 0;

    c_puts("[HTTP] Resolving host: ");
    c_puts(request->host);
    c_puts("...\n");

    /* Resolve hostname */
    int ip = dns_resolve(request->host);
    if (ip == 0)
    {
        c_puts("[HTTP] DNS resolution failed\n");
        return -1;
    }

    /* Show resolved IP */
    c_puts("[HTTP] Resolved to ");
    c_putc('0' + ((ip >> 24) & 0xFF) / 100 % 10);
    c_putc('0' + ((ip >> 24) & 0xFF) / 10 % 10);
    c_putc('0' + ((ip >> 24) & 0xFF) % 10);
    c_putc('.');
    c_putc('0' + ((ip >> 16) & 0xFF) / 100 % 10);
    c_putc('0' + ((ip >> 16) & 0xFF) / 10 % 10);
    c_putc('0' + ((ip >> 16) & 0xFF) % 10);
    c_putc('.');
    c_putc('0' + ((ip >> 8) & 0xFF) / 100 % 10);
    c_putc('0' + ((ip >> 8) & 0xFF) / 10 % 10);
    c_putc('0' + ((ip >> 8) & 0xFF) % 10);
    c_putc('.');
    c_putc('0' + (ip & 0xFF) / 100 % 10);
    c_putc('0' + (ip & 0xFF) / 10 % 10);
    c_putc('0' + (ip & 0xFF) % 10);
    c_puts("\n");

    /* Connect to server */
    c_puts("[HTTP] Connecting to server...\n");
    if (tcp_connect(ip, request->port) < 0)
    {
        c_puts("[HTTP] Connection failed\n");
        return -1;
    }

    c_puts("[HTTP] Connected! Sending request...\n");

    /* Build and send HTTP request */
    int req_len = http_build_request(request, request_buf, sizeof(request_buf));
    if (req_len < 0)
    {
        c_puts("[HTTP] Failed to build request\n");
        tcp_close(0);
        return -1;
    }

    c_puts("[HTTP] Request:\n");
    c_puts(request_buf);
    c_puts("\n---END REQUEST---\n");

    if (tcp_send(0, request_buf, req_len) < 0)
    {
        c_puts("[HTTP] Failed to send request\n");
        tcp_close(0);
        return -1;
    }

    c_puts("[HTTP] Request sent, receiving response...\n");

    /* Receive response */
    uint32_t start = get_ticks();
    while (response_len < HTTP_MAX_RESPONSE_LEN - 1)
    {
        int received = tcp_receive(0, response_buf + response_len,
                                   HTTP_MAX_RESPONSE_LEN - response_len - 1);
        if (received > 0)
        {
            response_len += received;
            response_buf[response_len] = '\0';

            /* Check if we have complete headers */
            if (http_strstr(response_buf, "\r\n\r\n"))
            {
                /* Check Content-Length to know when body is complete */
                /* For simplicity, we'll wait for connection close */
            }
        }

        /* Timeout check */
        if (get_ticks() - start > request->timeout_ms / 18)
        {
            c_puts("[HTTP] Response timeout\n");
            break;
        }

        /* Small delay to prevent busy loop */
        for (volatile int i = 0; i < 10000; i++)
            ;
    }

    c_puts("[HTTP] Received ");
    char num_buf[16];
    int n = response_len;
    int pos = 0;
    if (n == 0)
    {
        num_buf[pos++] = '0';
    }
    else
    {
        int digits = 0;
        int tmp = n;
        while (tmp > 0)
        {
            digits++;
            tmp /= 10;
        }
        pos = digits;
        num_buf[pos] = '\0';
        while (n > 0)
        {
            num_buf[--pos] = '0' + (n % 10);
            n /= 10;
        }
    }
    c_puts(num_buf);
    c_puts(" bytes\n");

    /* Close connection */
    tcp_close(0);

    /* Parse response */
    if (http_parse_response(response_buf, response_len, response) < 0)
    {
        c_puts("[HTTP] Failed to parse response\n");
        return -1;
    }

    c_puts("[HTTP] Status: ");
    c_putc('0' + (response->status_code / 100) % 10);
    c_putc('0' + (response->status_code / 10) % 10);
    c_putc('0' + response->status_code % 10);
    c_puts(" ");
    c_puts(response->status_text);
    c_puts("\n");

    return 0;
}

/* Get a specific header value from response */
const char *http_get_header(const http_response_t *response, const char *name)
{
    if (!response || !name)
        return NULL;

    int name_len = http_strlen(name);
    for (int i = 0; i < response->header_count; i++)
    {
        if (http_startswith_ci(response->headers[i], name) &&
            response->headers[i][name_len] == ':')
        {
            const char *val = response->headers[i] + name_len + 1;
            while (*val == ' ')
                val++;
            return val;
        }
    }
    return NULL;
}

/* Check if response indicates success */
int http_is_success(const http_response_t *response)
{
    if (!response)
        return HTTP_FALSE;
    return (response->status_code >= 200 && response->status_code < 300) ? HTTP_TRUE : HTTP_FALSE;
}

/* Print response details for debugging */
void http_print_response(const http_response_t *response)
{
    if (!response)
    {
        c_puts("[HTTP] NULL response\n");
        return;
    }

    c_puts("\n=== HTTP Response ===\n");
    c_puts("Status: ");
    c_putc('0' + (response->status_code / 100) % 10);
    c_putc('0' + (response->status_code / 10) % 10);
    c_putc('0' + response->status_code % 10);
    c_puts(" ");
    c_puts(response->status_text);
    c_puts("\n\n");

    c_puts("Headers:\n");
    for (int i = 0; i < response->header_count; i++)
    {
        c_puts("  ");
        c_puts(response->headers[i]);
        c_puts("\n");
    }

    c_puts("\nBody (");
    char num_buf[16];
    uint32_t len = response->body_len;
    int pos = 0;
    if (len == 0)
    {
        num_buf[pos++] = '0';
    }
    else
    {
        int digits = 0;
        uint32_t tmp = len;
        while (tmp > 0)
        {
            digits++;
            tmp /= 10;
        }
        pos = digits;
        num_buf[pos] = '\0';
        while (len > 0)
        {
            num_buf[--pos] = '0' + (len % 10);
            len /= 10;
        }
    }
    c_puts(num_buf);
    c_puts(" bytes):\n");

    /* Print first 500 chars of body */
    int print_len = response->body_len;
    if (print_len > 500)
        print_len = 500;
    for (int i = 0; i < print_len; i++)
    {
        if (response->body[i] >= 32 || response->body[i] == '\n' || response->body[i] == '\r')
            c_putc(response->body[i]);
        else
            c_putc('.');
    }
    c_puts("\n=== End Response ===\n");
}
