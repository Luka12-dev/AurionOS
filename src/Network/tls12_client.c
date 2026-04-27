/*
 * Minimal TLS 1.2 client — TLS_RSA_WITH_AES_128_CBC_SHA256 (0x003C) only.
 * ECDHE suites return TLS12_ERR_ECDHE from tls12_client_handshake().
 *
 * Crypto layout follows RFC 5246 / MAC-then-encrypt for CBC (mbedtls-compatible).
*/

#include "../../include/tls12_client.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern int tcp_send(int socket, const void *data, uint32_t len);
extern int tcp_receive(int socket, void *buffer, uint32_t max_len);
extern void netif_poll(void);
extern uint32_t get_ticks(void);

#define TLS_VERSION_BE 0x0303
#define CS_RSA_AES128_SHA256 0x003C

static void mem_cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void mem_set(uint8_t *d, uint8_t v, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) d[i] = v;
}

static int mem_cmp(const uint8_t *a, const uint8_t *b, uint32_t n) {
  for (uint32_t i = 0; i < n; i++)
    if (a[i] != b[i]) return a[i] - b[i];
  return 0;
}

/* --- PRNG (non-cryptographic; good enough for IVs / ClientHello.random) --- */
static uint32_t tls_rng_state = 0xC0FFEEu;

static void tls_rng_bytes(uint8_t *out, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    tls_rng_state = tls_rng_state * 1664525u + 1013904223u;
    out[i] = (uint8_t)(tls_rng_state ^ (tls_rng_state >> 16) ^ get_ticks());
  }
}

/* --- SHA-256 (compact, public-domain style) --- */
static void sha256_transform(uint32_t state[8], const uint8_t block[64]);

static void sha256_raw(const uint8_t *data, uint32_t len, uint8_t out[32]) {
  uint32_t st[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  uint8_t buf[64];
  uint32_t i = 0;
  for (; i + 64 <= len; i += 64) sha256_transform(st, data + i);
  uint32_t rem = len - i;
  mem_set(buf, 0, 64);
  mem_cpy(buf, data + i, rem);
  buf[rem] = 0x80;
  uint64_t bitlen = (uint64_t)len * 8u;
  if (rem + 1 > 56) {
    sha256_transform(st, buf);
    mem_set(buf, 0, 64);
  }
  for (int j = 0; j < 8; j++) buf[63 - j] = (uint8_t)(bitlen >> (8 * j));
  sha256_transform(st, buf);
  for (int w = 0; w < 8; w++) {
    out[w * 4 + 0] = (uint8_t)(st[w] >> 24);
    out[w * 4 + 1] = (uint8_t)(st[w] >> 16);
    out[w * 4 + 2] = (uint8_t)(st[w] >> 8);
    out[w * 4 + 3] = (uint8_t)(st[w]);
  }
}

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(uint32_t st[8], const uint8_t block[64]) {
  static const uint32_t K[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t W[64], a, b, c, d, e, f, g, h, t1, t2;
  for (int i = 0; i < 16; i++)
    W[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
  for (int i = 16; i < 64; i++) W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];
  a = st[0];
  b = st[1];
  c = st[2];
  d = st[3];
  e = st[4];
  f = st[5];
  g = st[6];
  h = st[7];
  for (int i = 0; i < 64; i++) {
    t1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];
    t2 = EP0(a) + MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  st[0] += a;
  st[1] += b;
  st[2] += c;
  st[3] += d;
  st[4] += e;
  st[5] += f;
  st[6] += g;
  st[7] += h;
}

static void hmac_sha256(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len,
                        uint8_t mac[32]) {
  uint8_t kipad[64], kopad[64], tk[32];
  const uint8_t *k = key;
  uint32_t kl = key_len;
  if (kl > 64) {
    sha256_raw(key, kl, tk);
    k = tk;
    kl = 32;
  }
  mem_set(kipad, 0x36, 64);
  mem_set(kopad, 0x5c, 64);
  for (uint32_t i = 0; i < kl; i++) {
    kipad[i] ^= k[i];
    kopad[i] ^= k[i];
  }
  uint8_t inner[64 + 1024];
  if (64 + msg_len > sizeof(inner)) {
    mem_set(mac, 0, 32);
    return;
  }
  mem_cpy(inner, kipad, 64);
  mem_cpy(inner + 64, msg, msg_len);
  uint8_t ih[32];
  sha256_raw(inner, 64 + msg_len, ih);
  uint8_t outer[64 + 32];
  mem_cpy(outer, kopad, 64);
  mem_cpy(outer + 64, ih, 32);
  sha256_raw(outer, 96, mac);
}

/* TLS 1.2 PRF (SHA256) */
static void prf_tls12(const uint8_t *secret, uint32_t slen, const char *label, const uint8_t *seed,
                      uint32_t seed_len, uint8_t *out, uint32_t out_len) {
  uint8_t lab[128];
  uint32_t li = 0;
  while (label[li] && li + 1 < sizeof(lab)) lab[li] = (uint8_t)label[li], li++;
  uint8_t S[64 + 256];
  uint32_t lab_len = li;
  mem_cpy(S, lab, lab_len);
  mem_cpy(S + lab_len, seed, seed_len);
  uint32_t Slen = lab_len + seed_len;

  uint8_t A[32], tmp[32];
  hmac_sha256(secret, slen, S, Slen, A);
  uint32_t off = 0;
  while (off < out_len) {
    uint8_t ctx[32 + 256];
    mem_cpy(ctx, A, 32);
    mem_cpy(ctx + 32, S, Slen);
    hmac_sha256(secret, slen, ctx, 32 + Slen, tmp);
    uint32_t chunk = out_len - off;
    if (chunk > 32) chunk = 32;
    for (uint32_t i = 0; i < chunk; i++) out[off + i] = tmp[i];
    off += chunk;
    hmac_sha256(secret, slen, A, 32, A);
  }
}

/* --- AES-128 (ECB block + CBC) --- */
static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82,
    0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96,
    0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb,
    0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
    0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32,
    0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6,
    0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e,
    0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

static uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t p = 0, c;
  for (int i = 0; i < 8; i++) {
    if (b & 1) p ^= a;
    c = a & 0x80;
    a <<= 1;
    if (c) a ^= 0x1b;
    b >>= 1;
  }
  return p;
}

static void aes_sub_bytes(uint8_t s[16]) {
  for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
}
static void aes_shift_rows(uint8_t s[16]) {
  uint8_t t;
  t = s[1];
  s[1] = s[5];
  s[5] = s[9];
  s[9] = s[13];
  s[13] = t;
  t = s[2];
  s[2] = s[10];
  s[10] = t;
  t = s[6];
  s[6] = s[14];
  s[14] = t;
  t = s[15];
  s[15] = s[11];
  s[11] = s[7];
  s[7] = s[3];
  s[3] = t;
}
static void aes_mix_columns(uint8_t s[16]) {
  for (int c = 0; c < 4; c++) {
    int i = c * 4;
    uint8_t a0 = s[i], a1 = s[i + 1], a2 = s[i + 2], a3 = s[i + 3];
    s[i] = gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3;
    s[i + 1] = a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
    s[i + 2] = a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3);
    s[i + 3] = gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2);
  }
}
static void aes_add_round_key(uint8_t s[16], const uint8_t *rk) {
  for (int i = 0; i < 16; i++) s[i] ^= rk[i];
}

static void aes_key_expand(const uint8_t key[16], uint8_t round_keys[11][16]) {
  uint8_t w[176];
  mem_cpy(w, key, 16);
  for (int i = 4; i < 44; i++) {
    uint32_t k = ((uint32_t)w[(i - 1) * 4] << 24) | ((uint32_t)w[(i - 1) * 4 + 1] << 16) |
                 ((uint32_t)w[(i - 1) * 4 + 2] << 8) | (uint32_t)w[(i - 1) * 4 + 3];
    if (i % 4 == 0) {
      uint32_t kb = (k << 8) | (k >> 24);
      uint8_t b[4] = {(uint8_t)(kb >> 24), (uint8_t)(kb >> 16), (uint8_t)(kb >> 8), (uint8_t)kb};
      for (int j = 0; j < 4; j++) b[j] = sbox[b[j]];
      k = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
      static const uint32_t rcon[10] = {0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
                                       0x20000000, 0x40000000, 0x80000000, 0x1b000000, 0x36000000};
      k ^= rcon[i / 4 - 1];
    }
    uint32_t prev = ((uint32_t)w[(i - 4) * 4] << 24) | ((uint32_t)w[(i - 4) * 4 + 1] << 16) |
                    ((uint32_t)w[(i - 4) * 4 + 2] << 8) | (uint32_t)w[(i - 4) * 4 + 3];
    k ^= prev;
    w[i * 4] = (uint8_t)(k >> 24);
    w[i * 4 + 1] = (uint8_t)(k >> 16);
    w[i * 4 + 2] = (uint8_t)(k >> 8);
    w[i * 4 + 3] = (uint8_t)k;
  }
  for (int r = 0; r < 11; r++) mem_cpy(round_keys[r], w + r * 16, 16);
}

static void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  uint8_t rk[11][16];
  aes_key_expand(key, rk);
  uint8_t s[16];
  mem_cpy(s, in, 16);
  aes_add_round_key(s, rk[0]);
  for (int r = 1; r < 10; r++) {
    aes_sub_bytes(s);
    aes_shift_rows(s);
    aes_mix_columns(s);
    aes_add_round_key(s, rk[r]);
  }
  aes_sub_bytes(s);
  aes_shift_rows(s);
  aes_add_round_key(s, rk[10]);
  mem_cpy(out, s, 16);
}

static void aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, uint32_t len) {
  uint8_t prev[16];
  mem_cpy(prev, iv, 16);
  for (uint32_t i = 0; i < len; i += 16) {
    uint8_t blk[16];
    for (int j = 0; j < 16; j++) blk[j] = data[i + j] ^ prev[j];
    aes128_encrypt_block(key, blk, data + i);
    mem_cpy(prev, data + i, 16);
  }
}

static const uint8_t isbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb, 0x7c, 0xe3,
    0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32,
    0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9,
    0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15,
    0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84, 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05,
    0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13,
    0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1,
    0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b,
    0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4, 0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07,
    0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, 0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
    0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, 0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb,
    0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61, 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63,
    0x55, 0x21, 0x0c, 0x7d};

static void aes_inv_shift_rows(uint8_t s[16]) {
  uint8_t t;
  t = s[13];
  s[13] = s[9];
  s[9] = s[5];
  s[5] = s[1];
  s[1] = t;
  t = s[2];
  s[2] = s[10];
  s[10] = t;
  t = s[6];
  s[6] = s[14];
  s[14] = t;
  t = s[3];
  s[3] = s[7];
  s[7] = s[11];
  s[11] = s[15];
  s[15] = t;
}

static void aes_inv_sub_bytes(uint8_t s[16]) {
  for (int i = 0; i < 16; i++) s[i] = isbox[s[i]];
}

static void aes_inv_mix_columns(uint8_t s[16]) {
  for (int c = 0; c < 4; c++) {
    int i = c * 4;
    uint8_t a0 = s[i], a1 = s[i + 1], a2 = s[i + 2], a3 = s[i + 3];
    s[i] = (uint8_t)(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
    s[i + 1] = (uint8_t)(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
    s[i + 2] = (uint8_t)(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
    s[i + 3] = (uint8_t)(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
  }
}

static void aes128_decrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  uint8_t rk[11][16];
  aes_key_expand(key, rk);
  uint8_t s[16];
  mem_cpy(s, in, 16);
  aes_add_round_key(s, rk[10]);
  for (int r = 9; r >= 1; r--) {
    aes_inv_shift_rows(s);
    aes_inv_sub_bytes(s);
    aes_add_round_key(s, rk[r]);
    aes_inv_mix_columns(s);
  }
  aes_inv_shift_rows(s);
  aes_inv_sub_bytes(s);
  aes_add_round_key(s, rk[0]);
  mem_cpy(out, s, 16);
}

static void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, uint32_t len) {
  uint8_t prev[16], cur[16];
  mem_cpy(prev, iv, 16);
  for (uint32_t i = 0; i < len; i += 16) {
    mem_cpy(cur, data + i, 16);
    uint8_t dec[16];
    aes128_decrypt_block(key, cur, dec);
    for (int j = 0; j < 16; j++) data[i + j] = (uint8_t)(dec[j] ^ prev[j]);
    mem_cpy(prev, cur, 16);
  }
}

/* --- Big-endian 256-byte integer helpers (RSA-2048) --- */
static int bn_cmp256(const uint8_t *a, const uint8_t *b) {
  for (int i = 0; i < 256; i++) {
    if (a[i] != b[i]) return (int)a[i] - (int)b[i];
  }
  return 0;
}

static void bn_sub256(const uint8_t *a, const uint8_t *b, uint8_t *out) {
  int16_t borrow = 0;
  for (int i = 255; i >= 0; i--) {
    int16_t v = (int16_t)a[i] - (int16_t)b[i] - borrow;
    if (v < 0) {
      v += 256;
      borrow = 1;
    } else
      borrow = 0;
    out[i] = (uint8_t)v;
  }
}

static void bn_mul256_be(const uint8_t *a, const uint8_t *b, uint8_t *out512) {
  mem_set(out512, 0, 512);
  for (int i = 0; i < 256; i++) {
    for (int j = 0; j < 256; j++) {
      uint32_t prod = (uint32_t)a[255 - i] * (uint32_t)b[255 - j];
      int k = i + j;
      if (k >= 512) continue;
      int p = 511 - k;
      uint32_t c = prod;
      while (c && p >= 0) {
        c += out512[p];
        out512[p] = (uint8_t)(c & 0xFF);
        c >>= 8;
        p--;
      }
    }
  }
}

static void bn_shl1_or_bit(uint8_t *r, int bit) {
  uint16_t c = 0;
  for (int i = 255; i >= 0; i--) {
    uint16_t v = ((uint16_t)r[i] << 1) | c;
    r[i] = (uint8_t)(v & 0xFF);
    c = v >> 8;
  }
  if (bit) r[255] ^= 1u;
}

static void bn_mod_product512(const uint8_t *prod512, const uint8_t *n256, uint8_t *out256) {
  uint8_t r[256];
  mem_set(r, 0, 256);
  for (int bit = 0; bit < 4096; bit++) {
    int bi = bit / 8;
    int bb = 7 - (bit % 8);
    int b = (prod512[bi] >> bb) & 1;
    bn_shl1_or_bit(r, b);
    while (bn_cmp256(r, n256) >= 0) {
      uint8_t tmp[256];
      bn_sub256(r, n256, tmp);
      mem_cpy(r, tmp, 256);
    }
  }
  mem_cpy(out256, r, 256);
}

static void bn_mod_mul(const uint8_t *a, const uint8_t *b, const uint8_t *n256, uint8_t *out256) {
  uint8_t p512[512];
  bn_mul256_be(a, b, p512);
  bn_mod_product512(p512, n256, out256);
}

static void rsa_pub_enc(const uint8_t *m256, const uint8_t *n256, uint8_t *out256) {
  uint8_t x[256];
  mem_cpy(x, m256, 256);
  for (int i = 0; i < 16; i++) bn_mod_mul(x, x, n256, x);
  bn_mod_mul(x, m256, n256, out256);
}

static int pkcs1_v15_encrypt(const uint8_t *msg, uint32_t msg_len, uint8_t *out256, const uint8_t *n256) {
  if (msg_len + 11 > 256) return -1;
  uint8_t m[256];
  m[0] = 0;
  m[1] = 2;
  uint32_t ps_len = 256 - 3 - msg_len;
  tls_rng_bytes(m + 2, ps_len);
  for (uint32_t i = 0; i < ps_len; i++)
    if (m[2 + i] == 0) m[2 + i] = 0x42;
  m[2 + ps_len] = 0;
  mem_cpy(m + 3 + ps_len, msg, msg_len);
  rsa_pub_enc(m, n256, out256);
  return 0;
}

static int der_find_rsa_modulus(const uint8_t *der, uint32_t dlen, uint8_t *n256, uint32_t *exp_out) {
  *exp_out = 65537;
  for (uint32_t i = 0; i + 4 < dlen; i++) {
    if (der[i] == 0x02 && der[i + 1] == 0x82 && der[i + 2] == 0x01 && der[i + 3] == 0x00) {
      mem_cpy(n256, der + i + 4, 256);
      for (uint32_t j = i + 4 + 256; j + 5 < dlen; j++) {
        if (der[j] == 0x02 && der[j + 1] == 0x03 && der[j + 2] == 0x01 && der[j + 3] == 0x00 && der[j + 4] == 0x01) {
          *exp_out = 65537;
          return 0;
        }
      }
      return 0;
    }
  }
  return -1;
}

/* --- TLS connection state --- */
static uint8_t tls_rx[16384];
static uint32_t tls_rx_len;
static uint8_t client_random[32], server_random[32];
static uint8_t master_secret[48];
static uint8_t client_w_mac[32], server_w_mac[32];
static uint8_t client_w_key[16], server_w_key[16];
static uint8_t client_w_iv[16], server_w_iv[16];
static uint64_t client_seq, server_seq;
static int tls_enc_ready;
static uint8_t transcript[8192];
static uint32_t transcript_len;

static void tr_add(const uint8_t *p, uint32_t n) {
  if (transcript_len + n > sizeof(transcript)) return;
  mem_cpy(transcript + transcript_len, p, n);
  transcript_len += n;
}

static void tls_rx_consume(uint32_t n) {
  if (n >= tls_rx_len) {
    tls_rx_len = 0;
    return;
  }
  memmove(tls_rx, tls_rx + n, tls_rx_len - n);
  tls_rx_len -= n;
}

static int tcp_pull(void) {
  for (int iter = 0; iter < 5; iter++) {
    for (int i = 0; i < 25; i++) netif_poll();
    int n = tcp_receive(0, tls_rx + tls_rx_len, (uint32_t)(sizeof(tls_rx) - tls_rx_len));
    if (n > 0) tls_rx_len += (uint32_t)n;
    if (tls_rx_len > 0) return 0;
  }
  return -1;
}

static int tls_need(uint32_t n) {
  while (tls_rx_len < n) {
    if (tcp_pull() < 0) return -1;
  }
  return 0;
}

static void be_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static void be_u24(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)((v >> 16) & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)(v & 0xFF);
}

static void be_u64(uint8_t *p, uint64_t v) {
  for (int i = 7; i >= 0; i--) {
    p[7 - i] = (uint8_t)((v >> (i * 8)) & 0xFF);
  }
}

static int tls_send_record(uint8_t type, const uint8_t *data, uint32_t len) {
  uint8_t rec[5 + 16384];
  if (len + 5 > sizeof(rec)) return -1;
  rec[0] = type;
  be_u16(rec + 1, TLS_VERSION_BE);
  be_u16(rec + 3, (uint16_t)len);
  mem_cpy(rec + 5, data, len);
  return tcp_send(0, rec, 5 + len);
}

static int tls_send_handshake(uint8_t msg_type, const uint8_t *body, uint32_t body_len) {
  uint8_t hs[4 + 16384];
  hs[0] = msg_type;
  be_u24(hs + 1, body_len);
  mem_cpy(hs + 4, body, body_len);
  tr_add(hs, 4 + body_len);
  return tls_send_record(22, hs, 4 + body_len);
}

void tls12_client_reset(void) {
  tls_rx_len = 0;
  tls_enc_ready = 0;
  transcript_len = 0;
  client_seq = 0;
  server_seq = 0;
  mem_set(master_secret, 0, sizeof(master_secret));
}

static int tls_encrypt_payload(uint8_t type, const uint8_t *plain, uint32_t plen, uint8_t *out, uint32_t *out_len) {
  uint8_t seqb[8];
  be_u64(seqb, client_seq);
  uint8_t ad[13];
  mem_cpy(ad, seqb, 8);
  ad[8] = type;
  be_u16(ad + 9, TLS_VERSION_BE);
  be_u16(ad + 11, (uint16_t)plen);
  uint8_t mac[32];
  uint8_t hmsg[13 + 16384];
  mem_cpy(hmsg, ad, 13);
  mem_cpy(hmsg + 13, plain, plen);
  hmac_sha256(client_w_mac, 32, hmsg, 13 + plen, mac);
  uint32_t tot = plen + 32;
  uint32_t pad = 16 - (tot % 16);
  if (pad == 0) pad = 16;
  uint32_t enc_len = tot + pad;
  uint8_t work[4096];
  if (enc_len > sizeof(work)) return -1;
  mem_cpy(work, plain, plen);
  mem_cpy(work + plen, mac, 32);
  for (uint32_t i = 0; i < pad; i++) work[tot + i] = (uint8_t)(pad - 1);
  uint8_t iv[16];
  tls_rng_bytes(iv, 16);
  aes128_cbc_encrypt(client_w_key, iv, work, enc_len);
  mem_cpy(out, iv, 16);
  mem_cpy(out + 16, work, enc_len);
  *out_len = 16 + enc_len;
  client_seq++;
  return 0;
}

static int tls_send_encrypted(uint8_t type, const uint8_t *plain, uint32_t plen) {
  uint8_t buf[16384];
  uint32_t el = 0;
  if (tls_encrypt_payload(type, plain, plen, buf, &el) < 0) return -1;
  return tls_send_record(type, buf, el);
}

static int tls_decrypt_app(uint8_t *out, uint32_t max_out, uint32_t *got) {
  if (tls_need(5) < 0) return -1;
  uint8_t *p = tls_rx;
  uint8_t rtype = p[0];
  if (rtype != 23 && rtype != 22 && rtype != 21) return -1;
  uint16_t reclen = ((uint16_t)p[3] << 8) | p[4];
  if (tls_need(5 + reclen) < 0) return -1;
  p = tls_rx;
  uint8_t *pay = p + 5;
  uint16_t rl = reclen;
  tls_rx_consume(5 + reclen);
  if (rl < 16) return -1;
  uint8_t iv[16];
  mem_cpy(iv, pay, 16);
  uint32_t clen = rl - 16;
  uint8_t *ct = pay + 16;
  uint8_t *tmp = out;
  if (clen > max_out + 64) return -1;
  mem_cpy(tmp, ct, clen);
  aes128_cbc_decrypt(server_w_key, iv, tmp, clen);
  if (clen < 32 + 1) return -1;
  uint8_t padv = tmp[clen - 1];
  uint32_t dlen = clen - 32 - padv - 1;
  if (dlen > max_out) return -1;
  uint8_t seqb[8];
  be_u64(seqb, server_seq);
  uint8_t ad[13];
  mem_cpy(ad, seqb, 8);
  ad[8] = rtype;
  be_u16(ad + 9, TLS_VERSION_BE);
  be_u16(ad + 11, (uint16_t)dlen);
  uint8_t hm[13 + 16384];
  mem_cpy(hm, ad, 13);
  mem_cpy(hm + 13, tmp, dlen);
  uint8_t mac2[32];
  hmac_sha256(server_w_mac, 32, hm, 13 + dlen, mac2);
  if (mem_cmp(mac2, tmp + dlen, 32) != 0) return -1;
  mem_cpy(out, tmp, dlen);
  *got = dlen;
  server_seq++;
  return 0;
}

int tls12_client_send_app(const void *data, uint32_t len) {
  if (!tls_enc_ready) return -1;
  return tls_send_encrypted(23, (const uint8_t *)data, len);
}

int tls12_client_recv_app(void *buf, uint32_t max_len) {
  if (!tls_enc_ready) return -1;
  uint32_t got = 0;
  if (tls_decrypt_app((uint8_t *)buf, max_len, &got) < 0) return -1;
  return (int)got;
}

int tls12_client_handshake(int tcp_socket, const char *sni_hostname) {
  (void)tcp_socket;
  tls12_client_reset();
  tls_rng_bytes(client_random, 32);

  uint8_t ch_body[512];
  uint32_t cb = 0;
  be_u16(ch_body + cb, TLS_VERSION_BE);
  cb += 2;
  mem_cpy(ch_body + cb, client_random, 32);
  cb += 32;
  ch_body[cb++] = 0;
  be_u16(ch_body + cb, 2);
  cb += 2;
  be_u16(ch_body + cb, CS_RSA_AES128_SHA256);
  cb += 2;
  ch_body[cb++] = 1;
  ch_body[cb++] = 0;
  uint16_t sni_len = 0;
  while (sni_hostname[sni_len] && sni_len < 200) sni_len++;
  /* extensions: SNI — total ext bytes = 4 + (5 + sni_len) */
  uint16_t ext_data_len = (uint16_t)(5 + sni_len);
  uint16_t all_ext_len = (uint16_t)(4 + ext_data_len);
  be_u16(ch_body + cb, all_ext_len);
  cb += 2;
  be_u16(ch_body + cb, 0);
  cb += 2;
  be_u16(ch_body + cb, ext_data_len);
  cb += 2;
  be_u16(ch_body + cb, (uint16_t)(3 + sni_len));
  cb += 2;
  ch_body[cb++] = 0;
  be_u16(ch_body + cb, sni_len);
  cb += 2;
  for (uint16_t i = 0; i < sni_len; i++) ch_body[cb++] = (uint8_t)sni_hostname[i];

  if (tls_send_handshake(1, ch_body, cb) < 0) return -1;

  uint8_t srv_rand[32];
  uint8_t n256[256];
  uint32_t pubexp = 65537;
  int got_sh = 0, got_cert = 0, got_done = 0;
  uint8_t cert_der[4096];
  uint32_t cert_len = 0;

  while (!got_done) {
    if (tls_need(5) < 0) return -1;
    uint8_t typ = tls_rx[0];
    uint16_t rlen = ((uint16_t)tls_rx[3] << 8) | tls_rx[4];
    if (tls_need(5 + rlen) < 0) return -1;
    uint8_t *frag = tls_rx + 5;
    if (typ == 22) {
      uint32_t off = 0;
      while (off + 4 <= rlen) {
        uint8_t ht = frag[off];
        uint32_t hlen = ((uint32_t)frag[off + 1] << 16) | ((uint32_t)frag[off + 2] << 8) | frag[off + 3];
        if (off + 4 + hlen > rlen) break;
        uint8_t *hbody = frag + off + 4;
        if (ht == 2) {
          mem_cpy(server_random, hbody + 2, 32);
          uint8_t sidlen = hbody[34];
          uint32_t cso = 35u + (uint32_t)sidlen;
          if (cso + 1 >= hlen) break;
          uint16_t cs = ((uint16_t)hbody[cso] << 8) | hbody[cso + 1];
          if (cs == 0xC02F || cs == 0xC030 || (cs >> 8) == 0xC0) {
            tls_rx_consume(5 + rlen);
            return TLS12_ERR_ECDHE;
          }
          if (cs != CS_RSA_AES128_SHA256) {
            tls_rx_consume(5 + rlen);
            return -1;
          }
          mem_cpy(srv_rand, server_random, 32);
          got_sh = 1;
          tr_add(frag + off, 4 + hlen);
        } else if (ht == 11) {
          uint32_t chain_len = ((uint32_t)hbody[0] << 16) | ((uint32_t)hbody[1] << 8) | hbody[2];
          uint32_t c0len = ((uint32_t)hbody[3] << 16) | ((uint32_t)hbody[4] << 8) | hbody[5];
          (void)chain_len;
          if (c0len > sizeof(cert_der)) {
            tls_rx_consume(5 + rlen);
            return -1;
          }
          mem_cpy(cert_der, hbody + 6, c0len);
          cert_len = c0len;
          if (der_find_rsa_modulus(cert_der, cert_len, n256, &pubexp) < 0) {
            tls_rx_consume(5 + rlen);
            return -1;
          }
          got_cert = 1;
          tr_add(frag + off, 4 + hlen);
        } else if (ht == 14) {
          got_done = 1;
          tr_add(frag + off, 4 + hlen);
        }
        off += 4 + hlen;
      }
    }
    tls_rx_consume(5 + rlen);
  }

  if (!got_sh || !got_cert) return -1;

  uint8_t pre[48];
  pre[0] = 0x03;
  pre[1] = 0x03;
  tls_rng_bytes(pre + 2, 46);
  uint8_t enc_pre[256];
  if (pkcs1_v15_encrypt(pre, 48, enc_pre, n256) < 0) return -1;
  uint8_t cke[2 + 256];
  be_u16(cke, 256);
  mem_cpy(cke + 2, enc_pre, 256);
  {
    uint8_t hs[4 + 300];
    hs[0] = 16;
    be_u24(hs + 1, 258);
    mem_cpy(hs + 4, cke, 258);
    tr_add(hs, 4 + 258);
    if (tls_send_record(22, hs, 4 + 258) < 0) return -1;
  }

  uint8_t ms_seed[64];
  mem_cpy(ms_seed, client_random, 32);
  mem_cpy(ms_seed + 32, server_random, 32);
  prf_tls12(pre, 48, "master secret", ms_seed, 64, master_secret, 48);
  uint8_t seed_exp[64];
  mem_cpy(seed_exp, server_random, 32);
  mem_cpy(seed_exp + 32, client_random, 32);
  uint8_t keyblk[128];
  prf_tls12(master_secret, 48, "key expansion", seed_exp, 64, keyblk, 128);
  mem_cpy(client_w_mac, keyblk, 32);
  mem_cpy(server_w_mac, keyblk + 32, 32);
  mem_cpy(client_w_key, keyblk + 64, 16);
  mem_cpy(server_w_key, keyblk + 80, 16);
  mem_cpy(client_w_iv, keyblk + 96, 16);
  mem_cpy(server_w_iv, keyblk + 112, 16);

  uint8_t verify_hash[32];
  sha256_raw(transcript, transcript_len, verify_hash);
  uint8_t fin_seed[32];
  mem_cpy(fin_seed, verify_hash, 32);
  uint8_t vd[12];
  prf_tls12(master_secret, 48, "client finished", fin_seed, 32, vd, 12);

  uint8_t ccs[1] = {1};
  if (tls_send_record(20, ccs, 1) < 0) return -1;

  uint8_t fin_hs[4 + 12];
  fin_hs[0] = 20;
  be_u24(fin_hs + 1, 12);
  mem_cpy(fin_hs + 4, vd, 12);
  uint8_t encfin[256];
  uint32_t efl = 0;
  if (tls_encrypt_payload(22, fin_hs, 16, encfin, &efl) < 0) return -1;
  if (tls_send_record(22, encfin, efl) < 0) return -1;

  if (tls_need(6) < 0) return -1;
  if (tls_rx[0] != 20 || (((uint16_t)tls_rx[3] << 8) | tls_rx[4]) != 1) return -1;
  tls_rx_consume(6);

  if (tls_need(5) < 0) return -1;
  if (tls_rx[0] != 22) return -1;
  uint16_t srlen = ((uint16_t)tls_rx[3] << 8) | tls_rx[4];
  if (tls_need(5 + srlen) < 0) return -1;
  {
    uint8_t *pay = tls_rx + 5;
    uint8_t riv[16];
    mem_cpy(riv, pay, 16);
    uint32_t scl = srlen - 16;
    uint8_t stmp[512];
    if (scl > sizeof(stmp)) return -1;
    mem_cpy(stmp, pay + 16, scl);
    aes128_cbc_decrypt(server_w_key, riv, stmp, scl);
    uint8_t padv = stmp[scl - 1];
    uint32_t dlen = scl - 32 - padv - 1;
    (void)dlen;
    uint8_t seqb[8];
    be_u64(seqb, server_seq);
    uint8_t ad[13];
    mem_cpy(ad, seqb, 8);
    ad[8] = 22;
    be_u16(ad + 9, TLS_VERSION_BE);
    be_u16(ad + 11, (uint16_t)dlen);
    uint8_t hm[13 + 64];
    mem_cpy(hm, ad, 13);
    mem_cpy(hm + 13, stmp, dlen);
    uint8_t mac2[32];
    hmac_sha256(server_w_mac, 32, hm, 13 + dlen, mac2);
    if (mem_cmp(mac2, stmp + dlen, 32) != 0) return -1;
    tls_rx_consume(5 + srlen);
    server_seq++;
  }

  tls_enc_ready = 1;
  (void)pubexp;
  (void)srv_rand;
  return 0;
}
