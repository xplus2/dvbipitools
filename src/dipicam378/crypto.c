/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include <openssl/pem.h>

#include "lib/secure_zero.h"

#include "crypto.h"

int device_key_load(const char *path, EVP_PKEY **out) {
  FILE *f = fopen(path, "r");
  EVP_PKEY *pkey;
  if (!f)
    return -1;
  pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
  fclose(f);
  if (!pkey)
    return -1;
  *out = pkey;
  return 0;
}

int device_emm_u_decrypt(EVP_PKEY *ek, const unsigned char *ct, size_t ct_len, unsigned char bk_out[CRYPTO_KEY_LEN]) {
  EVP_PKEY_CTX *ctx;
  unsigned char buf[512];
  size_t outlen = sizeof buf;
  int ret = -1;

  ctx = EVP_PKEY_CTX_new(ek, NULL);
  if (!ctx)
    return -1;
  if (EVP_PKEY_decrypt_init(ctx) <= 0)
    goto done;
  if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0)
    goto done;
  if (EVP_PKEY_decrypt(ctx, buf, &outlen, ct, ct_len) <= 0)
    goto done;
  if (outlen != CRYPTO_KEY_LEN)
    goto done;
  memcpy(bk_out, buf, CRYPTO_KEY_LEN);
  ret = 0;
done:
  EVP_PKEY_CTX_free(ctx);
  return ret;
}

int device_emm_g_decrypt(const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char in[CRYPTO_EMM_G_LEN], unsigned char sk_out[CRYPTO_KEY_LEN]) {
  const unsigned char *nonce = in;
  const unsigned char *ciphertext = in + CRYPTO_GCM_NONCE_LEN;
  const unsigned char *tag = in + CRYPTO_GCM_NONCE_LEN + CRYPTO_KEY_LEN;
  EVP_CIPHER_CTX *ctx;
  int ok = 1, len = 0;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;

  ok &= EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1;
  ok &= EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CRYPTO_GCM_NONCE_LEN, NULL) == 1;
  ok &= EVP_DecryptInit_ex(ctx, NULL, NULL, bk, nonce) == 1;
  ok &= EVP_DecryptUpdate(ctx, sk_out, &len, ciphertext, CRYPTO_KEY_LEN) == 1;
  ok &= EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, CRYPTO_GCM_TAG_LEN, (void *)tag) == 1;
  ok &= EVP_DecryptFinal_ex(ctx, sk_out + len, &len) == 1;

  EVP_CIPHER_CTX_free(ctx);
  if (!ok) {
    secure_zero(sk_out, CRYPTO_KEY_LEN);
    return -1;
  }
  return 0;
}

int device_ecm_decrypt(const unsigned char sk[CRYPTO_KEY_LEN], const unsigned char enc[CRYPTO_CW_ENC_LEN], int cw_len, unsigned char *cw_out) {
  EVP_CIPHER_CTX *ctx;
  unsigned char block[CRYPTO_CW_ENC_LEN];
  int ok = 1, len = 0;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;

  ok &= EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, sk, NULL) == 1;
  ok &= EVP_CIPHER_CTX_set_padding(ctx, 0) == 1;
  ok &= EVP_DecryptUpdate(ctx, block, &len, enc, CRYPTO_CW_ENC_LEN) == 1;

  EVP_CIPHER_CTX_free(ctx);
  if (!ok || len != CRYPTO_CW_ENC_LEN) {
    secure_zero(block, sizeof block);
    return -1;
  }
  memcpy(cw_out, block, (size_t)cw_len);
  secure_zero(block, sizeof block);
  return 0;
}
