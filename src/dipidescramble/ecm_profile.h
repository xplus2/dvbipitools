/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_ECM_PROFILE_H
#define DIPIDESCRAMBLE_ECM_PROFILE_H

#include <stddef.h>

#include "crypto.h"

#define ECM_PROFILE_HEADER_MAX 4
#define ECM_PROFILE_HEADER_LEN_MAX 16
#define ECM_PROFILE_ID_MAX 16
#define ECM_PROFILE_INFO_MAX 64
#define ECM_PROFILE_TOKENS_MAX 16
#define ECM_PROFILE_CW_MAX 8 /* cw_count upper bound */
#define ECM_PROFILE_SPEC_MAX 2048 /* max --ecm-profile <spec> string length */
#define ECM_PROFILE_WIRE_MAX 512 /* max plausible ECM payload size under a profile, scratch buffer sizing */

typedef enum {
  ECM_CIPHER_AES256_ECB = 0, /* default, legacy path equivalent */
  ECM_CIPHER_AES128_ECB,
  ECM_CIPHER_AES128_CBC,
  ECM_CIPHER_AES256_CBC,
  ECM_CIPHER_AES128_GCM,
  ECM_CIPHER_AES256_GCM,
  ECM_CIPHER_DES_EDE3_ECB, /* legacy testing, 3-key 3DES, 24B key */
  ECM_CIPHER_DES_EDE3_CBC,
  ECM_CIPHER_DES_EDE_ECB, /* legacy testing, 2-key 3DES, 16B key */
  ECM_CIPHER_DES_EDE_CBC,
} ecm_cipher_t;

typedef enum {
  ECM_IV_NONE = 0, /* default, only legal with *-ECB */
  ECM_IV_ZERO,
  ECM_IV_RANDOM,
  ECM_IV_CP_NUMBER,
} ecm_iv_source_t;

typedef enum {
  ECM_PAD_NONE = 0, /* default */
  ECM_PAD_ZERO,
  ECM_PAD_PKCS7,
} ecm_padding_t;

typedef enum {
  ECM_INTEGRITY_NONE = 0, /* default */
  ECM_INTEGRITY_CRC32,
  ECM_INTEGRITY_HMAC_SHA256,
} ecm_integrity_t;

typedef enum {
  ECM_INTEGRITY_AFTER_ENCRYPT = 0, /* default, Encrypt-then-MAC */
  ECM_INTEGRITY_BEFORE_ENCRYPT,    /* MAC-then-Encrypt, tag inside ciphertext */
} ecm_integrity_order_t;

typedef enum {
  ECM_SHORT_KEY_TRUNCATE = 0, /* default, first N bytes of enc key */
  ECM_SHORT_KEY_SEPARATE_INFO, /* independent HKDF call, needs hkdf: true */
} ecm_short_key_source_t;

typedef enum {
  ECM_CP_LAYOUT_BACK = 0, /* default, cp_number in block's last 2 bytes */
  ECM_CP_LAYOUT_FRONT,    /* cp_number in bytes 0-1 */
} ecm_cp_layout_t;

typedef enum {
  ECM_TRUNCATE_LEFT = 0, /* default, NIST SP 800-107 convention */
  ECM_TRUNCATE_RIGHT,
} ecm_truncate_from_t;

typedef enum {
  ECM_CRC32_IEEE = 0, /* default, 0xEDB88320, zlib/png/gzip/ethernet */
  ECM_CRC32_CASTAGNOLI, /* 0x82F63B78, CRC-32C */
} ecm_crc32_variant_t;

typedef enum {
  ECM_CRC32_BIG = 0, /* default */
  ECM_CRC32_LITTLE,
} ecm_crc32_endian_t;

/* field_order/wire_order/cw_group tokens: a fixed keyword, or a format.headers[] reference */
typedef enum {
  ECM_TOK_ECM_ID = 0,
  ECM_TOK_CP_NUMBER,
  ECM_TOK_CW,
  ECM_TOK_CW_GROUP,
  ECM_TOK_INTEGRITY_TAG,
  ECM_TOK_IV,
  ECM_TOK_CIPHERTEXT,
  ECM_TOK_GCM_TAG,
  ECM_TOK_HEADER,
} ecm_token_kind_t;

typedef struct {
  ecm_token_kind_t kind;
  /* raw token text, valid iff kind == ECM_TOK_HEADER. resolved in ecm_profile_validate(),
     deferred: field_order/wire_order/cw_group can ref entries in any order */
  char id[ECM_PROFILE_ID_MAX];
} ecm_token_t;

typedef struct {
  ecm_token_t tok[ECM_PROFILE_TOKENS_MAX];
  int count;
} ecm_token_list_t;

typedef struct {
  char id[ECM_PROFILE_ID_MAX];
  unsigned char data[ECM_PROFILE_HEADER_LEN_MAX];
  int len;
} ecm_header_t;

typedef struct {
  ecm_header_t headers[ECM_PROFILE_HEADER_MAX];
  int header_count;
  int include_cp_number;
  int include_ecm_id;
  ecm_token_list_t field_order;
  int field_order_set; /* explicitly given in --ecm-profile */
  ecm_token_list_t wire_order;
  int wire_order_set;
  ecm_token_list_t cw_group; /* only meaningful when cw_count > 1 */
} ecm_format_t;

typedef struct {
  int hkdf; /* default 1: derive subkeys via HKDF. 0: raw SK for both roles */
  char enc_info[ECM_PROFILE_INFO_MAX];
  char mac_info[ECM_PROFILE_INFO_MAX];
  ecm_short_key_source_t short_key_source;
  char short_key_info[ECM_PROFILE_INFO_MAX];
  int short_key_info_set;
} ecm_key_derivation_t;

typedef struct {
  ecm_integrity_t type;
  ecm_integrity_order_t order;
  int truncate_tag; /* 0 = unset (full 32B), else 4 or 8, hmac-sha256 only */
  ecm_truncate_from_t truncate_from;
  int bind_ecm_id;    /* default 1 */
  int bind_cp_number; /* default 1 */
  ecm_crc32_variant_t crc32_variant;
  ecm_crc32_endian_t crc32_endian;
} ecm_integrity_cfg_t;

typedef struct {
  int set; /* 1 if --ecm-profile was given at all */
  ecm_cipher_t cipher;
  ecm_iv_source_t iv_source;
  ecm_padding_t padding;
  ecm_key_derivation_t key_derivation;
  ecm_cp_layout_t cp_number_layout;
  ecm_integrity_cfg_t integrity;
  ecm_format_t format;
  int cw_count; /* 0 = single-CW path (default) */
  unsigned ecm_id;
  int ecm_id_set; /* explicit ecm_id= given; unset falls back to ECM PID at decrypt time */
} ecm_profile_t;

/* parse --ecm-profile <spec> string (comma key=value, '+' list sep, ':' compound values, 'header=' repeatable).
   defaults match the fields above; out->set is always 1 on success. 0 ok, -1 malformed spec */
int ecm_profile_parse(const char *spec, ecm_profile_t *out);

/* structural validation, independent of cw_len (unknown until PMT arrives): cipher/iv_source/padding,
   header id/token consistency, field_order/wire_order/cw_group shape, truncate_tag range, key_derivation consistency, MtE tag placement.
   also: format.field_order/wire_order. 0 ok, -1 invalid */
int ecm_profile_validate(ecm_profile_t *p);

typedef struct {
  size_t plaintext_len;   /* per combo, or per whole group if cw_count > 1 */
  size_t iv_len;           /* 0, 8 (3DES CBC), 12 (GCM), or 16 (AES CBC) */
  size_t ciphertext_len;
  size_t gcm_tag_len;      /* 0 or 16 */
  size_t integrity_tag_len; /* 0 (none, or MtE: tag lives inside ciphertext), 4 (crc32), or truncated hmac-sha256 length */
  size_t wire_len;         /* total ECM payload size: bytes following section's own cp_number field */
} ecm_layout_t;

/* compute profile's wire byte lengths at given cw_len (8 or 16, from PMT's scrambling_descriptor, unknown at parse time):
   second validation pass, also rejects padding: none with plaintext not block-aligned for this cw_len. 0 ok, -1 invalid */
int ecm_profile_layout(const ecm_profile_t *p, int cw_len, ecm_layout_t *out);

typedef struct {
  unsigned cp_number; /* 0 if this combo's cp_number isn't carried on wire and can't be inferred */
  unsigned char cw[16];
} ecm_cw_combo_t;

/* decrypts ECM payload (bytes after section's own cp_number field, ecm_profile_layout()'s wire_len bytes) into cw_count CW combos.
   sk: this service's 32B key. cp_number_outer: section's own cp_number field. ecm_id_fallback: used when profile->ecm_id is unset and include_ecm_id/bind_ecm_id/bind_cp_number need one.
   ECM PID is natural fallback for a receiver with no SimulCrypt session visibility. 0 ok, -1 decrypt or integrity failure */
int ecm_profile_decrypt_cw(const ecm_profile_t *p, int cw_len, const unsigned char sk[CRYPTO_KEY_LEN], const unsigned char *wire, size_t wire_len, unsigned cp_number_outer,
                           unsigned ecm_id_fallback, ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX], int *combo_count);

#endif
