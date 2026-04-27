/*
 * HTTPS fetch over TCP port 443 (TLS 1.2 client, basic).
*/

#ifndef NETWORK_HTTPS_H
#define NETWORK_HTTPS_H

#include <stdint.h>

int network_https_fetch(const char *url, char **out_content, uint32_t *out_len);

#endif
