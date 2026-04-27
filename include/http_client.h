/*
 * HTTP Client for AurionOS
 * Simple HTTP/1.1 client built on top of the TCP/IP stack
 * Supports GET requests with custom headers
*/

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "stdint.h"

/* Use int for boolean (matching OS conventions) */
#define HTTP_TRUE 1
#define HTTP_FALSE 0

/* Maximum sizes */
#define HTTP_MAX_URL_LEN 256
#define HTTP_MAX_HOST_LEN 128
#define HTTP_MAX_PATH_LEN 128
#define HTTP_MAX_HEADERS 16
#define HTTP_MAX_HEADER_LEN 256
#define HTTP_MAX_RESPONSE_LEN 65536 /* 64KB response buffer */
#define HTTP_MAX_BODY_LEN 32768     /* 32KB body buffer */

/* HTTP response structure */
typedef struct
{
    int status_code;      /* HTTP status code (200, 404, etc.) */
    char status_text[64]; /* Status text ("OK", "Not Found", etc.) */
    char headers[HTTP_MAX_HEADERS][HTTP_MAX_HEADER_LEN];
    int header_count;
    char body[HTTP_MAX_BODY_LEN];
    uint32_t body_len;
    uint32_t content_length; /* Content-Length header value */
    int chunked;             /* Transfer-Encoding: chunked (HTTP_TRUE/HTTP_FALSE) */
} http_response_t;

/* HTTP request configuration */
typedef struct
{
    char host[HTTP_MAX_HOST_LEN];
    char path[HTTP_MAX_PATH_LEN];
    uint16_t port; /* Default: 80 */
    char custom_headers[HTTP_MAX_HEADERS][HTTP_MAX_HEADER_LEN];
    int custom_header_count;
    uint32_t timeout_ms; /* Request timeout in milliseconds */
} http_request_t;

/* Parse a URL into host, path, and port */
int http_parse_url(const char *url, char *host, int host_max,
                   char *path, int path_max, uint16_t *port);

/* Perform an HTTP GET request */
int http_get(const char *url, http_response_t *response);

/* Full GET (alias for http_get; redirects and chunked bodies handled in http_get path) */
int http_full_get(const char *url, http_response_t *response);

/* Perform an HTTP GET request with custom configuration */
int http_get_ex(const http_request_t *request, http_response_t *response);

/* Get a specific header value from response (returns NULL if not found) */
const char *http_get_header(const http_response_t *response, const char *name);

/* Check if response indicates success (2xx status code) */
int http_is_success(const http_response_t *response);

/* Print response details for debugging */
void http_print_response(const http_response_t *response);

#endif /* HTTP_CLIENT_H */