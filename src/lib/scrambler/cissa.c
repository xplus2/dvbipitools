/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "lib/helper/secure_zero.h"

#include "cissa.h"

/* ETSI TS 103 127 clause 6.3.1.2, fixed IV for all CISSA v1 traffic */
static const unsigned char cissa_iv[16] = { 0x44, 0x56, 0x42, 0x54, 0x4d, 0x43, 0x50, 0x54, 0x41, 0x45, 0x53, 0x43, 0x49, 0x53, 0x53, 0x41};

struct cissa_key {
  EVP_CIPHER_CTX *ecb[2]; /* [0] decrypt, [1] encrypt, keyed once */
};

int cissa_available(void) { return 1; }

void cissa_key_free(cissa_key_t *k) {
  if (!k)
    return;
  EVP_CIPHER_CTX_free(k->ecb[0]);
  EVP_CIPHER_CTX_free(k->ecb[1]);
  secure_zero(k, sizeof *k);
  free(k);
}

cissa_key_t *cissa_key_new(const unsigned char cw[CISSA_CW_LEN]) {
  cissa_key_t *k = calloc(1, sizeof *k);
  if (!k)
    return NULL;
  for (int enc = 0; enc < 2; enc++) {
    k->ecb[enc] = EVP_CIPHER_CTX_new();
    if (!k->ecb[enc] || EVP_CipherInit_ex(k->ecb[enc], EVP_aes_128_ecb(), NULL, cw, NULL, enc) != 1) {
      cissa_key_free(k);
      return NULL;
    }
    EVP_CIPHER_CTX_set_padding(k->ecb[enc], 0);
  }
  return k;
}

static int ecb(EVP_CIPHER_CTX *c, unsigned char *out, const unsigned char *in, size_t len) {
  int outlen;
  return EVP_CipherUpdate(c, out, &outlen, in, (int)len) == 1 && (size_t)outlen == len ? 0 : -1;
}

static void xor16(unsigned char *d, const unsigned char *a, const unsigned char *b) {
  for (int i = 0; i < 16; i++) d[i] = a[i] ^ b[i];
}

/* CBC restarts per packet (TS 103 127 6.3, random access): block j of every packet goes through one ECB call */
static int encrypt_batch(const cissa_key_t *k, cissa_batch_entry_t *e, unsigned n) {
  unsigned char tmp[CISSA_BATCH_MAX * 16];
  const unsigned char *prev[CISSA_BATCH_MAX];
  size_t max_len = 0;
  for (unsigned i = 0; i < n; i++) {
    prev[i] = cissa_iv;
    if (e[i].len > max_len) max_len = e[i].len;
  }
  for (size_t off = 0; off < max_len; off += 16) {
    unsigned m = 0;
    for (unsigned i = 0; i < n; i++)
      if (off < e[i].len) xor16(tmp + 16 * m++, e[i].data + off, prev[i]);
    if (ecb(k->ecb[1], tmp, tmp, 16 * (size_t)m)) return -1;
    m = 0;
    for (unsigned i = 0; i < n; i++)
      if (off < e[i].len) {
        memcpy(e[i].data + off, tmp + 16 * m++, 16);
        prev[i] = e[i].data + off;
      }
  }
  return 0;
}

static int decrypt_batch(const cissa_key_t *k, cissa_batch_entry_t *e, unsigned n) {
  unsigned char tmp[CISSA_BATCH_MAX * 192];
  size_t total = 0;
  for (unsigned i = 0; i < n; i++) {
    memcpy(tmp + total, e[i].data, e[i].len);
    total += e[i].len;
  }
  if (ecb(k->ecb[0], tmp, tmp, total)) return -1;
  total = 0;
  for (unsigned i = 0; i < n; i++) {
    for (size_t off = e[i].len; off >= 16; off -= 16)
      xor16(e[i].data + off - 16, tmp + total + off - 16, off > 16 ? e[i].data + off - 32 : cissa_iv);
    total += e[i].len;
  }
  return 0;
}

int cissa_crypt_batch(cissa_key_t *k, int encrypt, cissa_batch_entry_t *e, unsigned n) {
  if (!k || n > CISSA_BATCH_MAX) return -1;
  for (unsigned i = 0; i < n; i++)
    if (e[i].len == 0 || e[i].len % 16 || e[i].len > 192) return -1;
  return encrypt ? encrypt_batch(k, e, n) : decrypt_batch(k, e, n);
}

int cissa_encrypt_block(cissa_key_t *k, unsigned char *data, size_t len) { return cissa_crypt_batch(k, 1, &(cissa_batch_entry_t){data, len}, 1); }

int cissa_decrypt_block(cissa_key_t *k, unsigned char *data, size_t len) { return cissa_crypt_batch(k, 0, &(cissa_batch_entry_t){data, len}, 1); }
