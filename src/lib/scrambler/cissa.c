/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <openssl/evp.h>

#include "cissa.h"

/* ETSI TS 103 127 clause 6.3.1.2, fixed IV for all CISSA v1 traffic */
static const unsigned char cissa_iv[16] = { 0x44, 0x56, 0x42, 0x54, 0x4d, 0x43, 0x50, 0x54, 0x41, 0x45, 0x53, 0x43, 0x49, 0x53, 0x53, 0x41};

int cissa_encrypt_block(const unsigned char key[CISSA_CW_LEN], unsigned char *data, size_t len) {
  EVP_CIPHER_CTX *ctx;
  int outlen, totlen;
  int ok = 0;

  if (len == 0 || len % 16 != 0)
    return -1;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;

  if (EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, cissa_iv) != 1)
    goto done;
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  if (EVP_EncryptUpdate(ctx, data, &outlen, data, (int)len) != 1 || (size_t)outlen != len)
    goto done;
  totlen = outlen;
  if (EVP_EncryptFinal_ex(ctx, data + totlen, &outlen) != 1)
    goto done;
  ok = 1;

done:
  EVP_CIPHER_CTX_free(ctx);
  return ok ? 0 : -1;
}
