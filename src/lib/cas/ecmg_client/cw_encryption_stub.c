/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/log.h"
#include "cw_encryption.h"

int cwenc_aes_ecb_encrypt(int key_bits, const unsigned char *key, const unsigned char in[CWENC_AES_BLOCK_LEN], unsigned char out[CWENC_AES_BLOCK_LEN]) {
  (void)key_bits;
  (void)key;
  (void)in;
  (void)out;
  return -1;
}

int cwenc_aes_ctr_xcrypt(int key_bits, const unsigned char *key, const unsigned char iv[CWENC_AES_BLOCK_LEN], const unsigned char *in, unsigned char *out, size_t len) {
  (void)key_bits;
  (void)key;
  (void)iv;
  (void)in;
  (void)out;
  (void)len;
  return -1;
}

int cwenc_hkdf_sha256(const unsigned char *ikm, size_t ikm_len, const unsigned char *info, size_t info_len, unsigned char out[CWENC_HMAC_SHA256_LEN]) {
  (void)ikm;
  (void)ikm_len;
  (void)info;
  (void)info_len;
  (void)out;
  return -1;
}

int cwenc_config_validate(const cwenc_config_t *cfg, int cw_len) {
  (void)cw_len;
  if (cfg->algo == CWENC_ALGO_OFF) return 0;
  if (cfg->algo == CWENC_ALGO_DES56) return cwenc_config_validate_key(cfg);
  log_code("LA300131", "cw_encryption: this build has no OpenSSL, AES CW_encryption unavailable");
  return -1;
}

int cwenc_encrypt_cw(const cwenc_config_t *cfg, const cwenc_selection_t *sel, int cw_len, unsigned short ecm_channel_id, unsigned short ecm_stream_id, unsigned short combo_cp_number, unsigned char *cw) {
  if (cfg->algo == CWENC_ALGO_DES56) return cwenc_encrypt_des56(sel, cw_len, cw);
  (void)ecm_channel_id;
  (void)ecm_stream_id;
  (void)combo_cp_number;
  return -1;
}
