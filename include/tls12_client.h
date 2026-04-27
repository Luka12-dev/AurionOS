#ifndef TLS12_CLIENT_H
#define TLS12_CLIENT_H

#include <stdint.h>

/* TLS 1.2 client (single-connection, RSA + AES-128-CBC-SHA256 cipher 0x003C).
 * ECDHE-based suites return TLS12_ERR_ECDHE (-2) until implemented.
*/

#define TLS12_ERR_ECDHE (-2)

void tls12_client_reset(void);
int tls12_client_handshake(int tcp_socket, const char *sni_hostname);
int tls12_client_send_app(const void *data, uint32_t len);
int tls12_client_recv_app(void *buf, uint32_t max_len);

#endif
