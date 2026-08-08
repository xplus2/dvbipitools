/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "cissa.h"

/* ETSI TS 103 127 clause 6.3.1.2, fixed IV for all CISSA v1 traffic */
static const unsigned char cissa_iv[16] = { 0x44, 0x56, 0x42, 0x54, 0x4d, 0x43, 0x50, 0x54, 0x41, 0x45, 0x53, 0x43, 0x49, 0x53, 0x53, 0x41};

#define CISSA_DIR_NONE 0
#define CISSA_DIR_ENCRYPT 1
#define CISSA_DIR_DECRYPT 2

struct cissa_key {
  EVP_CIPHER_CTX *ctx;
  int dir; /* which direction ctx's key schedule is currently set up for */
  unsigned char cw[CISSA_CW_LEN];
};

cissa_key_t *cissa_key_new(const unsigned char cw[CISSA_CW_LEN]) {
  cissa_key_t *k = calloc(1, sizeof *k);
  if (!k)
    return NULL;
  k->ctx = EVP_CIPHER_CTX_new();
  if (!k->ctx) {
    free(k);
    return NULL;
  }
  k->dir = CISSA_DIR_NONE;
  memcpy(k->cw, cw, CISSA_CW_LEN);
  return k;
}

void cissa_key_free(cissa_key_t *k) {
  if (!k)
    return;
  EVP_CIPHER_CTX_free(k->ctx);
  free(k);
}

/* key schedule redone only on fresh ctx / direction switch, not per packet. IV still resets
   every call - CBC restarts per packet by design (TS 103 127 6.3, random access). */
int cissa_encrypt_block(cissa_key_t *k, unsigned char *data, size_t len) {
  int outlen, totlen;

  if (!k || len == 0 || len % 16 != 0)
    return -1;

  if (k->dir == CISSA_DIR_ENCRYPT) {
    if (EVP_EncryptInit_ex(k->ctx, NULL, NULL, NULL, cissa_iv) != 1)
      return -1;
  } else {
    if (EVP_EncryptInit_ex(k->ctx, EVP_aes_128_cbc(), NULL, k->cw, cissa_iv) != 1)
      return -1;
    EVP_CIPHER_CTX_set_padding(k->ctx, 0);
    k->dir = CISSA_DIR_ENCRYPT;
  }
  if (EVP_EncryptUpdate(k->ctx, data, &outlen, data, (int)len) != 1 || (size_t)outlen != len)
    return -1;
  totlen = outlen;
  if (EVP_EncryptFinal_ex(k->ctx, data + totlen, &outlen) != 1)
    return -1;
  return 0;
}

int cissa_decrypt_block(cissa_key_t *k, unsigned char *data, size_t len) {
  int outlen, totlen;

  if (!k || len == 0 || len % 16 != 0)
    return -1;

  if (k->dir == CISSA_DIR_DECRYPT) {
    if (EVP_DecryptInit_ex(k->ctx, NULL, NULL, NULL, cissa_iv) != 1)
      return -1;
  } else {
    if (EVP_DecryptInit_ex(k->ctx, EVP_aes_128_cbc(), NULL, k->cw, cissa_iv) != 1)
      return -1;
    EVP_CIPHER_CTX_set_padding(k->ctx, 0);
    k->dir = CISSA_DIR_DECRYPT;
  }
  if (EVP_DecryptUpdate(k->ctx, data, &outlen, data, (int)len) != 1 || (size_t)outlen != len)
    return -1;
  totlen = outlen;
  if (EVP_DecryptFinal_ex(k->ctx, data + totlen, &outlen) != 1)
    return -1;
  return 0;
}
