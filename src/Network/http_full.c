/*
 * Full HTTP/1.1 GET helper built on http_client (redirects, chunked, etc. in http_client.c).
*/

#include "../../include/http_client.h"

int http_full_get(const char *url, http_response_t *resp) {
    if (!url || !resp) return -1;
    return http_get(url, resp);
}
