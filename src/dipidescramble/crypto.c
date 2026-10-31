/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include <openssl/hmac.h>

#include "lib/cas/des.h"
#include "lib/demux/crc32.h"
#include "lib/helper/secure_zero.h"

#include "crypto.h"

static const EVP_CIPHER *ecm_evp_cipher(crypto_ecm_cipher_t cipher, int cbc) {
  switch (cipher) {
    case CRYPTO_ECM_AES128:
      return cbc ? EVP_aes_128_cbc() : EVP_aes_128_ecb();
    case CRYPTO_ECM_AES256:
      return cbc ? EVP_aes_256_cbc() : EVP_aes_256_ecb();
    case CRYPTO_ECM_DES_EDE:
    case CRYPTO_ECM_DES_EDE3:
      break;
  }
  return NULL;
}

/* key: k1|k2 for EDE, k1|k2|k3 for EDE3. ct/out may alias */
static int ecm_des3_decrypt(crypto_ecm_cipher_t cipher, int cbc, const unsigned char *key, const unsigned char *iv, const unsigned char *ct, size_t ct_len, unsigned char *out) {
  des_key_t k1;
  des_key_t k2;
  des_key_t k3;
  unsigned char prev[DES_BLOCK_LEN];
  if (ct_len == 0 || ct_len % DES_BLOCK_LEN != 0) return -1;
  des_set_key(&k1, key);
  des_set_key(&k2, key + DES_BLOCK_LEN);
  des_set_key(&k3, cipher == CRYPTO_ECM_DES_EDE3 ? key + 2 * DES_BLOCK_LEN : key);
  if (cbc) memcpy(prev, iv, DES_BLOCK_LEN);
  for (size_t off = 0; off < ct_len; off += DES_BLOCK_LEN) {
    unsigned char next[DES_BLOCK_LEN];
    memcpy(next, ct + off, DES_BLOCK_LEN);
    des3_decrypt(&k1, &k2, &k3, next, out + off);
    if (cbc) {
      for (size_t i = 0; i < DES_BLOCK_LEN; i++) out[off + i] ^= prev[i];
      memcpy(prev, next, DES_BLOCK_LEN);
    }
  }
  secure_zero(&k1, sizeof k1);
  secure_zero(&k2, sizeof k2);
  secure_zero(&k3, sizeof k3);
  return 0;
}

int crypto_ecm_block_decrypt(crypto_ecm_cipher_t cipher, int cbc, const unsigned char *key,
                              const unsigned char *iv, const unsigned char *ct, size_t ct_len, unsigned char *out) {
  const EVP_CIPHER *evp = ecm_evp_cipher(cipher, cbc);
  EVP_CIPHER_CTX *ctx;
  int len = 0, finlen = 0, ret = -1;
  if (cipher == CRYPTO_ECM_DES_EDE || cipher == CRYPTO_ECM_DES_EDE3) return ecm_des3_decrypt(cipher, cbc, key, iv, ct, ct_len, out);
  if (!evp || ct_len == 0) return -1;
  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return -1;
  if (EVP_DecryptInit_ex(ctx, evp, NULL, key, iv) != 1) goto done;
  if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) goto done;
  if (EVP_DecryptUpdate(ctx, out, &len, ct, (int)ct_len) != 1) goto done;
  if (EVP_DecryptFinal_ex(ctx, out + len, &finlen) != 1) goto done;
  if ((size_t)(len + finlen) == ct_len) ret = 0;

done:
  EVP_CIPHER_CTX_free(ctx);
  return ret;
}

int crypto_ecm_gcm_decrypt(int key_bits, const unsigned char *key, const unsigned char nonce[CRYPTO_GCM_NONCE_LEN],
                            const unsigned char *aad, size_t aad_len, const unsigned char *ct, size_t ct_len,
                            const unsigned char tag[CRYPTO_GCM_TAG_LEN], unsigned char *out) {
  const EVP_CIPHER *evp = key_bits == 128 ? EVP_aes_128_gcm() : EVP_aes_256_gcm();
  EVP_CIPHER_CTX *ctx;
  int discard = 0, outlen = 0, finlen = 0, ret = -1;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return -1;
  if (EVP_DecryptInit_ex(ctx, evp, NULL, NULL, NULL) != 1) goto done;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CRYPTO_GCM_NONCE_LEN, NULL) != 1) goto done;
  if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto done;
  if (aad && aad_len && EVP_DecryptUpdate(ctx, NULL, &discard, aad, (int)aad_len) != 1) goto done;
  if (ct_len && EVP_DecryptUpdate(ctx, out, &outlen, ct, (int)ct_len) != 1) goto done;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, CRYPTO_GCM_TAG_LEN, (void *)tag) != 1) goto done;
  if (EVP_DecryptFinal_ex(ctx, out + outlen, &finlen) != 1) goto done;
  if ((size_t)outlen == ct_len) ret = 0;

done:
  EVP_CIPHER_CTX_free(ctx);
  return ret;
}

int crypto_hmac_sha256(const unsigned char key[CRYPTO_KEY_LEN], const unsigned char *data, size_t len, unsigned char out[CRYPTO_HMAC_SHA256_LEN]) {
  unsigned int outlen = 0;
  if (!HMAC(EVP_sha256(), key, CRYPTO_KEY_LEN, data, len, out, &outlen)) return -1;
  if (outlen != CRYPTO_HMAC_SHA256_LEN) return -1;
  return 0;
}

int crypto_hkdf_sha256(const unsigned char key[CRYPTO_KEY_LEN], const char *info, unsigned char out[CRYPTO_HMAC_SHA256_LEN]) {
  unsigned char zero_salt[CRYPTO_HMAC_SHA256_LEN];
  unsigned char prk[CRYPTO_HMAC_SHA256_LEN];
  unsigned char t1_input[64]; /* info || counter(1B), info stays well under this */
  size_t infolen = strlen(info);
  int ret;

  if (infolen + 1 > sizeof t1_input) return -1;
  memset(zero_salt, 0, sizeof zero_salt);
  if (crypto_hmac_sha256(zero_salt, key, CRYPTO_KEY_LEN, prk) != 0) return -1;
  memcpy(t1_input, info, infolen);
  t1_input[infolen] = 0x01;
  ret = crypto_hmac_sha256(prk, t1_input, infolen + 1, out); /* expand, single block */
  secure_zero(prk, sizeof prk);
  return ret;
}

uint32_t crypto_crc32(int castagnoli, const unsigned char *data, size_t len) {
  return crc32_zlib(castagnoli, data, len);
}
