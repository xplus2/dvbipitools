/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_ECMG_CLIENT_CW_ENCRYPTION_H
#define DVBIPITOOLS_LIB_CAS_ECMG_CLIENT_CW_ENCRYPTION_H

#include <stddef.h>

#define CWENC_HMAC_SHA256_LEN 32
#define CWENC_AES_BLOCK_LEN 16
#define CWENC_DES_KEY_LEN 8

void cwenc_des56_expand(const unsigned char in7[7], unsigned char out8[CWENC_DES_KEY_LEN]);
int cwenc_des_ecb_encrypt(const unsigned char key[CWENC_DES_KEY_LEN], const unsigned char *pt, size_t pt_len, unsigned char *out);
int cwenc_aes_ecb_encrypt(int key_bits, const unsigned char *key, const unsigned char in[CWENC_AES_BLOCK_LEN], unsigned char out[CWENC_AES_BLOCK_LEN]);
int cwenc_aes_ctr_xcrypt(int key_bits, const unsigned char *key, const unsigned char iv[CWENC_AES_BLOCK_LEN], const unsigned char *in, unsigned char *out, size_t len);
int cwenc_hkdf_sha256(const unsigned char *ikm, size_t ikm_len, const unsigned char *info, size_t info_len, unsigned char out[CWENC_HMAC_SHA256_LEN]);

#define CWENC_KEY_LIST_LEN 2048
#define CWENC_KEY_MATERIAL_MAX 32
#define CWENC_PTR_MAX 2037
#define CWENC_PTR_STEP 7

typedef enum {
  CWENC_ALGO_OFF = 0,
  CWENC_ALGO_DES56,
  CWENC_ALGO_AES128,
  CWENC_ALGO_AES256,
} cwenc_algo_t;

typedef enum {
  CWENC_AES_MODE_STREAM = 0,
  CWENC_AES_MODE_ECB,
} cwenc_aes_mode_t;

typedef struct {
  cwenc_algo_t algo;
  cwenc_aes_mode_t aes_mode;
  unsigned char fixed_key[CWENC_KEY_MATERIAL_MAX];
  size_t fixed_key_len;
  unsigned char key_list_a[CWENC_KEY_LIST_LEN];
  int key_list_a_loaded;
  unsigned char key_list_b[CWENC_KEY_LIST_LEN];
  int key_list_b_loaded;
} cwenc_config_t;

typedef struct {
  cwenc_config_t cfg;
  unsigned next_ptr;
  int next_list_sel;
} cwenc_ctx_t;

typedef struct {
  int key_list_sel;
  int fixed_key_mode;
  int algorithm_type;
  int cw_key_ptr;
  unsigned char key_material[CWENC_KEY_MATERIAL_MAX];
  size_t key_material_len;
} cwenc_selection_t;

int cwenc_config_init(cwenc_config_t *cfg, const char *algorithm, const char *aes_mode, const char *fixed_key_hex, const char *key_list_a_path, const char *key_list_b_path);
int cwenc_config_validate(const cwenc_config_t *cfg, int cw_len);
void cwenc_ctx_init(cwenc_ctx_t *ctx, const cwenc_config_t *cfg);
int cwenc_select_next(cwenc_ctx_t *ctx, cwenc_selection_t *out);
int cwenc_encrypt_cw(const cwenc_config_t *cfg, const cwenc_selection_t *sel, int cw_len, unsigned short ecm_channel_id, unsigned short ecm_stream_id, unsigned short combo_cp_number, unsigned char *cw);
unsigned short cwenc_pack_param(const cwenc_selection_t *sel);

#endif
