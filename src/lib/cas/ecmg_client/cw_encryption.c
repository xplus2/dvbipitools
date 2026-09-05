/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
#endif
#include "lib/helper/log.h"
#include "lib/helper/secure_zero.h"

#include "cw_encryption.h"

void cwenc_des56_expand(const unsigned char in7[7], unsigned char out8[CWENC_DES_KEY_LEN]) {
  uint64_t bits = 0;
  for (int i = 0; i < 7; i++) bits = (bits << 8) | in7[i];
  for (int g = 0; g < 8; g++) {
    int shift = 56 - 7 * (g + 1);
    unsigned char byte = (unsigned char)(((bits >> shift) & 0x7F) << 1);
    int ones = 0;
    for (int b = 0; b < 8; b++) if (byte & (1 << b)) ones++;
    out8[g] = (ones % 2 == 0) ? (byte | 1) : byte;
  }
}

static void cwenc_ensure_legacy_provider(void) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  static int done = 0;
  if (!done) {
    OSSL_PROVIDER_load(NULL, "legacy");
    OSSL_PROVIDER_load(NULL, "default");
    done = 1;
  }
#endif
}

static int cwenc_block_encrypt(const EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *in, size_t len, unsigned char *out) {
  EVP_CIPHER_CTX *ctx;
  int outlen = 0, finlen = 0, ret = -1;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;
  if (EVP_EncryptInit_ex(ctx, cipher, NULL, key, NULL) != 1)
    goto done;
  if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1)
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
  int outlen = 0, finlen = 0, ret = -1;
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

static int cwenc_hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int cwenc_hex_decode(const char *hex, unsigned char *out, size_t out_len) {
  if (strlen(hex) != out_len * 2) return -1;
  for (size_t i = 0; i < out_len; i++) {
    int hi = cwenc_hex_nibble(hex[2 * i]), lo = cwenc_hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

static int cwenc_load_key_list_file(const char *path, unsigned char *out) {
  FILE *f = fopen(path, "rb");
  size_t n;
  int extra;
  if (!f) {
    log_line("cw_encryption: cannot open key list '%s'", path);
    return -1;
  }
  n = fread(out, 1, CWENC_KEY_LIST_LEN, f);
  extra = fgetc(f);
  fclose(f);
  if (n != CWENC_KEY_LIST_LEN || extra != EOF) {
    log_line("cw_encryption: key list '%s' must be exactly %d bytes", path, CWENC_KEY_LIST_LEN);
    return -1;
  }
  return 0;
}

int cwenc_config_init(cwenc_config_t *cfg, const char *algorithm, const char *aes_mode, const char *fixed_key_hex, const char *key_list_a_path, const char *key_list_b_path) {
  size_t want_len;

  memset(cfg, 0, sizeof *cfg);
  if (!algorithm || !algorithm[0]) {
    cfg->algo = CWENC_ALGO_OFF;
    return 0;
  }

  if (strcmp(algorithm, "des56") == 0) {
    cfg->algo = CWENC_ALGO_DES56;
    want_len = 7;
  } else if (strcmp(algorithm, "aes128") == 0) {
    cfg->algo = CWENC_ALGO_AES128;
    want_len = 16;
  } else if (strcmp(algorithm, "aes256") == 0) {
    cfg->algo = CWENC_ALGO_AES256;
    want_len = 32;
  } else {
    log_line("cw_encryption: invalid algorithm '%s' (des56|aes128|aes256)", algorithm);
    return -1;
  }

  if (!aes_mode || !aes_mode[0] || strcmp(aes_mode, "stream") == 0) {
    cfg->aes_mode = CWENC_AES_MODE_STREAM;
  } else if (strcmp(aes_mode, "ecb") == 0) {
    cfg->aes_mode = CWENC_AES_MODE_ECB;
  } else {
    log_line("cw_encryption: invalid aes_mode '%s' (stream|ecb)", aes_mode);
    return -1;
  }

  if (fixed_key_hex && fixed_key_hex[0]) {
    if (cwenc_hex_decode(fixed_key_hex, cfg->fixed_key, want_len) != 0) {
      log_line("cw_encryption: fixed_key must be %zu hex chars for %s", want_len * 2, algorithm);
      return -1;
    }
    cfg->fixed_key_len = want_len;
  } else if (cfg->algo == CWENC_ALGO_DES56) {
    static const unsigned char annex_d_fixed_key[7] = {0x4D, 0xA1, 0x9F, 0xF0, 0xAF, 0x6B, 0x8F};
    memcpy(cfg->fixed_key, annex_d_fixed_key, 7);
    cfg->fixed_key_len = 7;
  }

  if (key_list_a_path && key_list_a_path[0]) {
    if (cwenc_load_key_list_file(key_list_a_path, cfg->key_list_a) != 0) return -1;
    cfg->key_list_a_loaded = 1;
  }
  if (key_list_b_path && key_list_b_path[0]) {
    if (cwenc_load_key_list_file(key_list_b_path, cfg->key_list_b) != 0) return -1;
    cfg->key_list_b_loaded = 1;
  }
  return 0;
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

void cwenc_ctx_init(cwenc_ctx_t *ctx, const cwenc_config_t *cfg) {
  ctx->cfg = *cfg;
  ctx->next_ptr = 0;
  ctx->next_list_sel = 0;
}

int cwenc_select_next(cwenc_ctx_t *ctx, cwenc_selection_t *out) {
  const cwenc_config_t *cfg = &ctx->cfg;
  if (cfg->algo == CWENC_ALGO_OFF) return -1;
  out->algorithm_type = cfg->algo == CWENC_ALGO_DES56 ? 0 : cfg->algo == CWENC_ALGO_AES128 ? 1 : 2;
  if (cfg->key_list_a_loaded || cfg->key_list_b_loaded) {
    int use_b = cfg->key_list_b_loaded && (ctx->next_list_sel || !cfg->key_list_a_loaded);
    const unsigned char *list = use_b ? cfg->key_list_b : cfg->key_list_a;
    out->fixed_key_mode = 1;
    out->key_list_sel = use_b ? 1 : 0;
    out->cw_key_ptr = (int)ctx->next_ptr;
    memcpy(out->key_material, list + ctx->next_ptr, 7);
    out->key_material_len = 7;

    ctx->next_ptr += CWENC_PTR_STEP;
    if (ctx->next_ptr > CWENC_PTR_MAX) {
      ctx->next_ptr = 0;
      if (cfg->key_list_a_loaded && cfg->key_list_b_loaded)
        ctx->next_list_sel = !ctx->next_list_sel;
    }
    return 0;
  }
  if (cfg->fixed_key_len == 0) return -1;
  out->fixed_key_mode = 0;
  out->key_list_sel = 0;
  out->cw_key_ptr = 0;
  memcpy(out->key_material, cfg->fixed_key, cfg->fixed_key_len);
  out->key_material_len = cfg->fixed_key_len;
  return 0;
}

unsigned short cwenc_pack_param(const cwenc_selection_t *sel) {
  unsigned short v = 0;
  v |= (unsigned short)((sel->key_list_sel & 1) << 15);
  v |= (unsigned short)((sel->fixed_key_mode & 1) << 14);
  v |= (unsigned short)((sel->algorithm_type & 0x7) << 11);
  v |= (unsigned short)(sel->cw_key_ptr & 0x7FF);
  return v;
}

int cwenc_encrypt_cw(const cwenc_config_t *cfg, const cwenc_selection_t *sel, int cw_len, unsigned short ecm_channel_id, unsigned short ecm_stream_id, unsigned short combo_cp_number, unsigned char *cw) {
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

  unsigned char info[26];
  memcpy(info, "annexd-cwenc-ctx-v01", 20);
  info[20] = (unsigned char)(ecm_channel_id >> 8);
  info[21] = (unsigned char)ecm_channel_id;
  info[22] = (unsigned char)(ecm_stream_id >> 8);
  info[23] = (unsigned char)ecm_stream_id;
  info[24] = (unsigned char)(combo_cp_number >> 8);
  info[25] = (unsigned char)combo_cp_number;

  unsigned char derived[CWENC_HMAC_SHA256_LEN];
  if (cwenc_hkdf_sha256(sel->key_material, sel->key_material_len, info, sizeof info, derived) != 0)
    return -1;

  int key_bits = cfg->algo == CWENC_ALGO_AES128 ? 128 : 256;
  int rc;
  unsigned char out_buf[CWENC_AES_BLOCK_LEN];
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
