/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_BISS_EVP_BLOCK_H
#define DVBIPITOOLS_LIB_CAS_BISS_EVP_BLOCK_H

#include <openssl/evp.h>

/* single EVP_CIPHER_CTX block op, no padding: out gets exactly len bytes. 0 ok, -1 failed. iv: NULL for ECB */
static inline int evp_cipher_block(const EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *iv, const unsigned char *in, unsigned char *out, int len, int enc) {
  EVP_CIPHER_CTX *ctx;
  int outlen, totlen;
  int rc = -1;
  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;

  if (enc ? EVP_EncryptInit_ex(ctx, cipher, NULL, key, iv) != 1 : EVP_DecryptInit_ex(ctx, cipher, NULL, key, iv) != 1)
    goto out;
  EVP_CIPHER_CTX_set_padding(ctx, 0);

  if (enc ? EVP_EncryptUpdate(ctx, out, &outlen, in, len) != 1 : EVP_DecryptUpdate(ctx, out, &outlen, in, len) != 1)
    goto out;
  if (outlen != len)
    goto out;
  totlen = outlen;
  if (enc ? EVP_EncryptFinal_ex(ctx, out + totlen, &outlen) != 1 : EVP_DecryptFinal_ex(ctx, out + totlen, &outlen) != 1)
    goto out;

  rc = 0;
out:
  EVP_CIPHER_CTX_free(ctx);
  return rc;
}

#endif
