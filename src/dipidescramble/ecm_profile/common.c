/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/argutil.h"

#include "priv.h"

#define N(a) (sizeof(a) / sizeof(a)[0])

static const enum_map_t fixed_token_names[] = {
    {"ecm_id", ECM_TOK_ECM_ID}, {"cp_number", ECM_TOK_CP_NUMBER}, {"cw", ECM_TOK_CW}, {"cw_group", ECM_TOK_CW_GROUP},
    {"integrity_tag", ECM_TOK_INTEGRITY_TAG}, {"iv", ECM_TOK_IV}, {"ciphertext", ECM_TOK_CIPHERTEXT}, {"gcm_tag", ECM_TOK_GCM_TAG}};

int lookup_fixed_token(const char *name, ecm_token_kind_t *out) {
  int v;
  if (map_lookup(fixed_token_names, N(fixed_token_names), name, &v) != 0)
    return -1;
  *out = (ecm_token_kind_t)v;
  return 0;
}

int is_reserved_id(const char *id) {
  ecm_token_kind_t k;
  return lookup_fixed_token(id, &k) == 0;
}

int cipher_is_ecb(ecm_cipher_t c) {
  return c == ECM_CIPHER_AES128_ECB || c == ECM_CIPHER_AES256_ECB || c == ECM_CIPHER_DES_EDE_ECB || c == ECM_CIPHER_DES_EDE3_ECB;
}
int cipher_is_cbc(ecm_cipher_t c) {
  return c == ECM_CIPHER_AES128_CBC || c == ECM_CIPHER_AES256_CBC || c == ECM_CIPHER_DES_EDE_CBC || c == ECM_CIPHER_DES_EDE3_CBC;
}
int cipher_is_gcm(ecm_cipher_t c) {
  return c == ECM_CIPHER_AES128_GCM || c == ECM_CIPHER_AES256_GCM;
}
int cipher_is_3des(ecm_cipher_t c) {
  return c == ECM_CIPHER_DES_EDE_ECB || c == ECM_CIPHER_DES_EDE_CBC || c == ECM_CIPHER_DES_EDE3_ECB || c == ECM_CIPHER_DES_EDE3_CBC;
}
int cipher_block_size(ecm_cipher_t c) { return cipher_is_3des(c) ? 8 : 16; }

int cipher_key_len(ecm_cipher_t c) {
  switch (c) {
  case ECM_CIPHER_AES256_ECB: case ECM_CIPHER_AES256_CBC: case ECM_CIPHER_AES256_GCM: return 32;
  case ECM_CIPHER_AES128_ECB: case ECM_CIPHER_AES128_CBC: case ECM_CIPHER_AES128_GCM: return 16;
  case ECM_CIPHER_DES_EDE_ECB: case ECM_CIPHER_DES_EDE_CBC: return 16;
  case ECM_CIPHER_DES_EDE3_ECB: case ECM_CIPHER_DES_EDE3_CBC: return 24;
  }
  return 0;
}

crypto_ecm_cipher_t cipher_to_crypto(ecm_cipher_t c) {
  switch (c) {
  case ECM_CIPHER_AES256_ECB: case ECM_CIPHER_AES256_CBC: return CRYPTO_ECM_AES256;
  case ECM_CIPHER_DES_EDE_ECB: case ECM_CIPHER_DES_EDE_CBC: return CRYPTO_ECM_DES_EDE;
  case ECM_CIPHER_DES_EDE3_ECB: case ECM_CIPHER_DES_EDE3_CBC: return CRYPTO_ECM_DES_EDE3;
  default: return CRYPTO_ECM_AES128;
  }
}

const ecm_header_t *find_header(const ecm_format_t *fmt, const char *id) {
  for (int i = 0; i < fmt->header_count; i++)
    if (strcmp(fmt->headers[i].id, id) == 0)
      return &fmt->headers[i];
  return NULL;
}

size_t integrity_tag_wire_len(const ecm_profile_t *p) {
  if (p->integrity.type == ECM_INTEGRITY_NONE)
    return 0;
  if (p->integrity.type == ECM_INTEGRITY_CRC32)
    return 4;
  return p->integrity.truncate_tag ? (size_t)p->integrity.truncate_tag : CRYPTO_HMAC_SHA256_LEN;
}
