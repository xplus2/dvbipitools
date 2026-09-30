/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
#endif
#include "lib/helper/log.h"
#include "lib/helper/secure_zero.h"

#include "cw_encryption.h"

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
static OSSL_PROVIDER *cwenc_legacy_provider;
static OSSL_PROVIDER *cwenc_default_provider;

static void cwenc_unload_providers(void) {
  if (cwenc_legacy_provider)
    OSSL_PROVIDER_unload(cwenc_legacy_provider);
  if (cwenc_default_provider)
    OSSL_PROVIDER_unload(cwenc_default_provider);
}
#endif

static void cwenc_ensure_legacy_provider(void) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  static int done = 0;
  if (!done) {
    cwenc_legacy_provider = OSSL_PROVIDER_load(NULL, "legacy");
    cwenc_default_provider = OSSL_PROVIDER_load(NULL, "default");
    atexit(cwenc_unload_providers);
    done = 1;
  }
#endif
}

static int cwenc_block_encrypt(const EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *in, size_t len, unsigned char *out) {
  EVP_CIPHER_CTX *ctx;
  int outlen = 0;
  int finlen = 0;
  int ret = -1;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return -1;
  if (EVP_EncryptInit_ex(ctx, cipher, NULL, key, NULL) != 1)
    goto done;
  if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1)
    goto done;
  if (EVP_EncryptUpdate(ctx, out, &outlen, in, (int)len) != 1)
    goto done;
  if (EVP_EncryptFinal_ex(ctx, out + outlen, &finlen) != 1)
    goto done;
  if ((size_t)(outlen + finlen) == len) ret = 0;

done:
  EVP_CIPHER_CTX_free(ctx);
  return ret;
}

int cwenc_des_ecb_encrypt(const unsigned char key[CWENC_DES_KEY_LEN], const unsigned char *pt, size_t pt_len, unsigned char *out) {
  if (pt_len == 0 || pt_len % CWENC_DES_KEY_LEN != 0) return -1;
  cwenc_ensure_legacy_provider();
  return cwenc_block_encrypt(EVP_des_ecb(), key, pt, pt_len, out);
}

int cwenc_aes_ecb_encrypt(int key_bits, const unsigned char *key, const unsigned char in[CWENC_AES_BLOCK_LEN], unsigned char out[CWENC_AES_BLOCK_LEN]) {
  const EVP_CIPHER *cipher = key_bits == 128 ? EVP_aes_128_ecb() : EVP_aes_256_ecb();
  return cwenc_block_encrypt(cipher, key, in, CWENC_AES_BLOCK_LEN, out);
}

int cwenc_aes_ctr_xcrypt(int key_bits, const unsigned char *key, const unsigned char iv[CWENC_AES_BLOCK_LEN], const unsigned char *in, unsigned char *out, size_t len) {
  const EVP_CIPHER *cipher = key_bits == 128 ? EVP_aes_128_ctr() : EVP_aes_256_ctr();
  EVP_CIPHER_CTX *ctx;
  int outlen = 0;
  int finlen = 0;
  int ret = -1;
  if (len == 0) return -1;
  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return -1;
  if (EVP_EncryptInit_ex(ctx, cipher, NULL, key, iv) != 1)
    goto done;
  if (EVP_EncryptUpdate(ctx, out, &outlen, in, (int)len) != 1)
    goto done;
  if (EVP_EncryptFinal_ex(ctx, out + outlen, &finlen) != 1)
    goto done;
  if ((size_t)(outlen + finlen) == len)
    ret = 0;

done:
  EVP_CIPHER_CTX_free(ctx);
  return ret;
}

static int cwenc_hmac_sha256(const unsigned char *key, size_t key_len, const unsigned char *data, size_t data_len, unsigned char out[CWENC_HMAC_SHA256_LEN]) {
  unsigned int outlen = 0;
  if (!HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, &outlen)) return -1;
  return outlen == CWENC_HMAC_SHA256_LEN ? 0 : -1;
}

int cwenc_hkdf_sha256(const unsigned char *ikm, size_t ikm_len, const unsigned char *info, size_t info_len, unsigned char out[CWENC_HMAC_SHA256_LEN]) {
  unsigned char zero_salt[CWENC_HMAC_SHA256_LEN];
  unsigned char prk[CWENC_HMAC_SHA256_LEN];
  unsigned char t1[CWENC_HMAC_SHA256_LEN + 64];
  int ret;

  if (info_len + 1 > sizeof t1) return -1;
  memset(zero_salt, 0, sizeof zero_salt);
  if (cwenc_hmac_sha256(zero_salt, sizeof zero_salt, ikm, ikm_len, prk) != 0)
    return -1;
  memcpy(t1, info, info_len);
  t1[info_len] = 0x01;
  ret = cwenc_hmac_sha256(prk, sizeof prk, t1, info_len + 1, out);
  secure_zero(prk, sizeof prk);
  return ret;
}

int cwenc_config_validate(const cwenc_config_t *cfg, int cw_len) {
  if (cfg->algo == CWENC_ALGO_OFF) return 0;
  if (cfg->algo != CWENC_ALGO_DES56 && cfg->aes_mode == CWENC_AES_MODE_ECB && cw_len == 8) {
    log_line("cw_encryption: aes_mode=ecb is invalid with cw_len=8 (AES has no 8-byte block mode)");
    return -1;
  }
  if (!cfg->key_list_a_loaded && !cfg->key_list_b_loaded && cfg->fixed_key_len == 0) {
    log_line("cw_encryption: no fixed_key configured and no key list loaded");
    return -1;
  }
  return 0;
}

int cwenc_encrypt_cw(const cwenc_config_t *cfg, const cwenc_selection_t *sel, int cw_len, unsigned short ecm_channel_id, unsigned short ecm_stream_id, unsigned short combo_cp_number, unsigned char *cw) {
  unsigned char info[26];
  unsigned char derived[CWENC_HMAC_SHA256_LEN];
  int key_bits;
  int rc;
  unsigned char out_buf[CWENC_AES_BLOCK_LEN];

  if (cfg->algo == CWENC_ALGO_DES56) {
    unsigned char des_key[CWENC_DES_KEY_LEN];
    int nblocks = cw_len / CWENC_DES_KEY_LEN;
    cwenc_des56_expand(sel->key_material, des_key);
    for (int b = 0; b < nblocks; b++) {
      unsigned char block_out[CWENC_DES_KEY_LEN];
      if (cwenc_des_ecb_encrypt(des_key, cw + b * CWENC_DES_KEY_LEN, CWENC_DES_KEY_LEN, block_out) != 0) {
        secure_zero(des_key, sizeof des_key);
        return -1;
      }
      memcpy(cw + b * CWENC_DES_KEY_LEN, block_out, CWENC_DES_KEY_LEN);
    }
    secure_zero(des_key, sizeof des_key);
    return 0;
  }

  memcpy(info, "annexd-cwenc-ctx-v01", 20);
  info[20] = (unsigned char)(ecm_channel_id >> 8);
  info[21] = (unsigned char)ecm_channel_id;
  info[22] = (unsigned char)(ecm_stream_id >> 8);
  info[23] = (unsigned char)ecm_stream_id;
  info[24] = (unsigned char)(combo_cp_number >> 8);
  info[25] = (unsigned char)combo_cp_number;

  if (cwenc_hkdf_sha256(sel->key_material, sel->key_material_len, info, sizeof info, derived) != 0)
    return -1;

  key_bits = cfg->algo == CWENC_ALGO_AES128 ? 128 : 256;
  if (cfg->aes_mode == CWENC_AES_MODE_STREAM) {
    unsigned char zero_iv[CWENC_AES_BLOCK_LEN] = {0};
    rc = cwenc_aes_ctr_xcrypt(key_bits, derived, zero_iv, cw, out_buf, (size_t)cw_len);
    if (rc == 0)
      memcpy(cw, out_buf, (size_t)cw_len);
  } else {
    rc = cwenc_aes_ecb_encrypt(key_bits, derived, cw, out_buf);
    if (rc == 0) memcpy(cw, out_buf, CWENC_AES_BLOCK_LEN);
  }
  secure_zero(derived, sizeof derived);
  return rc;
}
