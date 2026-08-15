/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/log.h"
#include "lib/secure_zero.h"

#include "ecm_profile.h"
#include "version.h"

#define N(a) (sizeof(a) / sizeof(a)[0])

static const enum_map_t cipher_names[] = {
    {"aes128-ecb", ECM_CIPHER_AES128_ECB}, {"aes256-ecb", ECM_CIPHER_AES256_ECB},
    {"aes128-cbc", ECM_CIPHER_AES128_CBC}, {"aes256-cbc", ECM_CIPHER_AES256_CBC},
    {"aes128-gcm", ECM_CIPHER_AES128_GCM}, {"aes256-gcm", ECM_CIPHER_AES256_GCM},
    {"des-ede3-ecb", ECM_CIPHER_DES_EDE3_ECB}, {"des-ede3-cbc", ECM_CIPHER_DES_EDE3_CBC},
    {"des-ede-ecb", ECM_CIPHER_DES_EDE_ECB}, {"des-ede-cbc", ECM_CIPHER_DES_EDE_CBC}};
static const enum_map_t iv_source_names[] = {{"none", ECM_IV_NONE}, {"zero", ECM_IV_ZERO}, {"random", ECM_IV_RANDOM}, {"cp_number", ECM_IV_CP_NUMBER}};
static const enum_map_t padding_names[] = {{"none", ECM_PAD_NONE}, {"zero", ECM_PAD_ZERO}, {"pkcs7", ECM_PAD_PKCS7}};
static const enum_map_t short_key_source_names[] = {{"truncate", ECM_SHORT_KEY_TRUNCATE}, {"separate_info", ECM_SHORT_KEY_SEPARATE_INFO}};
static const enum_map_t cp_layout_names[] = {{"back", ECM_CP_LAYOUT_BACK}, {"front", ECM_CP_LAYOUT_FRONT}};
static const enum_map_t integrity_names[] = {{"none", ECM_INTEGRITY_NONE}, {"crc32", ECM_INTEGRITY_CRC32}, {"hmac-sha256", ECM_INTEGRITY_HMAC_SHA256}};
static const enum_map_t integrity_order_names[] = {{"after-encrypt", ECM_INTEGRITY_AFTER_ENCRYPT}, {"before-encrypt", ECM_INTEGRITY_BEFORE_ENCRYPT}};
static const enum_map_t truncate_from_names[] = {{"left", ECM_TRUNCATE_LEFT}, {"right", ECM_TRUNCATE_RIGHT}};
static const enum_map_t crc32_variant_names[] = {{"ieee", ECM_CRC32_IEEE}, {"castagnoli", ECM_CRC32_CASTAGNOLI}};
static const enum_map_t crc32_endian_names[] = {{"big", ECM_CRC32_BIG}, {"little", ECM_CRC32_LITTLE}};
static const enum_map_t fixed_token_names[] = {
    {"ecm_id", ECM_TOK_ECM_ID}, {"cp_number", ECM_TOK_CP_NUMBER}, {"cw", ECM_TOK_CW}, {"cw_group", ECM_TOK_CW_GROUP},
    {"integrity_tag", ECM_TOK_INTEGRITY_TAG}, {"iv", ECM_TOK_IV}, {"ciphertext", ECM_TOK_CIPHERTEXT}, {"gcm_tag", ECM_TOK_GCM_TAG}};

static int cipher_is_ecb(ecm_cipher_t c) {
  return c == ECM_CIPHER_AES128_ECB || c == ECM_CIPHER_AES256_ECB || c == ECM_CIPHER_DES_EDE_ECB || c == ECM_CIPHER_DES_EDE3_ECB;
}
static int cipher_is_cbc(ecm_cipher_t c) {
  return c == ECM_CIPHER_AES128_CBC || c == ECM_CIPHER_AES256_CBC || c == ECM_CIPHER_DES_EDE_CBC || c == ECM_CIPHER_DES_EDE3_CBC;
}
static int cipher_is_gcm(ecm_cipher_t c) {
  return c == ECM_CIPHER_AES128_GCM || c == ECM_CIPHER_AES256_GCM;
}
static int cipher_is_3des(ecm_cipher_t c) {
  return c == ECM_CIPHER_DES_EDE_ECB || c == ECM_CIPHER_DES_EDE_CBC || c == ECM_CIPHER_DES_EDE3_ECB || c == ECM_CIPHER_DES_EDE3_CBC;
}
static int cipher_block_size(ecm_cipher_t c) { return cipher_is_3des(c) ? 8 : 16; }

static int cipher_key_len(ecm_cipher_t c) {
  switch (c) {
  case ECM_CIPHER_AES256_ECB: case ECM_CIPHER_AES256_CBC: case ECM_CIPHER_AES256_GCM: return 32;
  case ECM_CIPHER_AES128_ECB: case ECM_CIPHER_AES128_CBC: case ECM_CIPHER_AES128_GCM: return 16;
  case ECM_CIPHER_DES_EDE_ECB: case ECM_CIPHER_DES_EDE_CBC: return 16;
  case ECM_CIPHER_DES_EDE3_ECB: case ECM_CIPHER_DES_EDE3_CBC: return 24;
  }
  return 0;
}

static crypto_ecm_cipher_t cipher_to_crypto(ecm_cipher_t c) {
  switch (c) {
  case ECM_CIPHER_AES256_ECB: case ECM_CIPHER_AES256_CBC: return CRYPTO_ECM_AES256;
  case ECM_CIPHER_DES_EDE_ECB: case ECM_CIPHER_DES_EDE_CBC: return CRYPTO_ECM_DES_EDE;
  case ECM_CIPHER_DES_EDE3_ECB: case ECM_CIPHER_DES_EDE3_CBC: return CRYPTO_ECM_DES_EDE3;
  default: return CRYPTO_ECM_AES128;
  }
}

static const ecm_header_t *find_header(const ecm_format_t *fmt, const char *id) {
  int i;
  for (i = 0; i < fmt->header_count; i++)
    if (strcmp(fmt->headers[i].id, id) == 0)
      return &fmt->headers[i];
  return NULL;
}

static int is_reserved_id(const char *id) {
  int v;
  return map_lookup(fixed_token_names, N(fixed_token_names), id, &v) == 0;
}

static int count_kind(const ecm_token_list_t *l, ecm_token_kind_t k) {
  int i, n = 0;
  for (i = 0; i < l->count; i++)
    if (l->tok[i].kind == k)
      n++;
  return n;
}

static size_t integrity_tag_wire_len(const ecm_profile_t *p) {
  if (p->integrity.type == ECM_INTEGRITY_NONE)
    return 0;
  if (p->integrity.type == ECM_INTEGRITY_CRC32)
    return 4;
  return p->integrity.truncate_tag ? (size_t)p->integrity.truncate_tag : CRYPTO_HMAC_SHA256_LEN;
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int hex_decode(const char *hex, unsigned char *out, int out_max, int *out_len) {
  size_t hexlen = strlen(hex), i;
  if (hexlen == 0 || hexlen % 2 != 0 || (int)(hexlen / 2) > out_max)
    return -1;
  for (i = 0; i < hexlen; i += 2) {
    int hi = hex_nibble(hex[i]), lo = hex_nibble(hex[i + 1]);
    if (hi < 0 || lo < 0)
      return -1;
    out[i / 2] = (unsigned char)((hi << 4) | lo);
  }
  *out_len = (int)(hexlen / 2);
  return 0;
}

static int parse_bool(const char *s, int *out) {
  if (strcmp(s, "0") == 0) { *out = 0; return 0; }
  if (strcmp(s, "1") == 0) { *out = 1; return 0; }
  return -1;
}

static int profile_err(const char *field, const char *val) {
  log_line(TOOL_NAME ": --ecm-profile: invalid value for %s: %s", field, val);
  return -1;
}

static int parse_header_entry(char *val, ecm_format_t *fmt) {
  char *colon = strchr(val, ':');
  ecm_header_t *h;
  if (!colon || colon == val)
    return -1;
  *colon = 0;
  if (fmt->header_count >= ECM_PROFILE_HEADER_MAX)
    return -1;
  if (strlen(val) == 0 || strlen(val) >= ECM_PROFILE_ID_MAX)
    return -1;
  h = &fmt->headers[fmt->header_count];
  memset(h, 0, sizeof *h);
  strncpy(h->id, val, ECM_PROFILE_ID_MAX - 1);
  if (hex_decode(colon + 1, h->data, ECM_PROFILE_HEADER_LEN_MAX, &h->len) != 0)
    return -1;
  fmt->header_count++;
  return 0;
}

static int parse_token(const char *name, ecm_token_t *out) {
  int v;
  if (map_lookup(fixed_token_names, N(fixed_token_names), name, &v) == 0) {
    out->kind = (ecm_token_kind_t)v;
    out->id[0] = 0;
    return 0;
  }
  if (strlen(name) == 0 || strlen(name) >= ECM_PROFILE_ID_MAX)
    return -1;
  out->kind = ECM_TOK_HEADER;
  strncpy(out->id, name, ECM_PROFILE_ID_MAX - 1);
  out->id[ECM_PROFILE_ID_MAX - 1] = 0;
  return 0;
}

static int parse_token_list(char *val, ecm_token_list_t *out) {
  char *saveptr = NULL;
  char *tok = strtok_r(val, "+", &saveptr);
  out->count = 0;
  while (tok) {
    if (out->count >= ECM_PROFILE_TOKENS_MAX)
      return -1;
    if (parse_token(tok, &out->tok[out->count]) != 0)
      return -1;
    out->count++;
    tok = strtok_r(NULL, "+", &saveptr);
  }
  return out->count > 0 ? 0 : -1;
}

int ecm_profile_parse(const char *spec, ecm_profile_t *out) {
  char buf[ECM_PROFILE_SPEC_MAX];
  char *saveptr = NULL;
  char *pair;

  if (strlen(spec) >= sizeof buf) {
    log_line(TOOL_NAME ": --ecm-profile: spec too long (max %zu bytes)", sizeof buf - 1);
    return -1;
  }
  memset(out, 0, sizeof *out);
  out->set = 1;
  out->key_derivation.hkdf = 1;
  strncpy(out->key_derivation.enc_info, "dipidescramble-ecm-enc", ECM_PROFILE_INFO_MAX - 1);
  strncpy(out->key_derivation.mac_info, "dipidescramble-ecm-mac", ECM_PROFILE_INFO_MAX - 1);
  out->key_derivation.short_key_source = ECM_SHORT_KEY_TRUNCATE;
  strncpy(out->key_derivation.short_key_info, "dipidescramble-ecm-enc-128", ECM_PROFILE_INFO_MAX - 1);
  out->cp_number_layout = ECM_CP_LAYOUT_BACK;
  out->integrity.truncate_from = ECM_TRUNCATE_LEFT;
  out->integrity.bind_ecm_id = 1;
  out->integrity.bind_cp_number = 1;
  out->integrity.crc32_variant = ECM_CRC32_IEEE;
  out->integrity.crc32_endian = ECM_CRC32_BIG;

  strncpy(buf, spec, sizeof buf - 1);
  buf[sizeof buf - 1] = 0;

  pair = strtok_r(buf, ",", &saveptr);
  while (pair) {
    char *eq = strchr(pair, '=');
    char *key, *val;
    int v;

    if (!eq) {
      log_line(TOOL_NAME ": --ecm-profile: malformed field %s (expected key=value)", pair);
      return -1;
    }
    *eq = 0;
    key = pair;
    val = eq + 1;

    if (strcmp(key, "cipher") == 0) {
      if (map_lookup(cipher_names, N(cipher_names), val, &v) != 0) return profile_err(key, val);
      out->cipher = (ecm_cipher_t)v;
    } else if (strcmp(key, "iv") == 0) {
      if (map_lookup(iv_source_names, N(iv_source_names), val, &v) != 0) return profile_err(key, val);
      out->iv_source = (ecm_iv_source_t)v;
    } else if (strcmp(key, "padding") == 0) {
      if (map_lookup(padding_names, N(padding_names), val, &v) != 0) return profile_err(key, val);
      out->padding = (ecm_padding_t)v;
    } else if (strcmp(key, "hkdf") == 0) {
      if (parse_bool(val, &out->key_derivation.hkdf) != 0) return profile_err(key, val);
    } else if (strcmp(key, "enc_info") == 0) {
      if (strlen(val) >= ECM_PROFILE_INFO_MAX) return profile_err(key, val);
      strncpy(out->key_derivation.enc_info, val, ECM_PROFILE_INFO_MAX - 1);
    } else if (strcmp(key, "mac_info") == 0) {
      if (strlen(val) >= ECM_PROFILE_INFO_MAX) return profile_err(key, val);
      strncpy(out->key_derivation.mac_info, val, ECM_PROFILE_INFO_MAX - 1);
    } else if (strcmp(key, "short_key_source") == 0) {
      if (map_lookup(short_key_source_names, N(short_key_source_names), val, &v) != 0) return profile_err(key, val);
      out->key_derivation.short_key_source = (ecm_short_key_source_t)v;
    } else if (strcmp(key, "short_key_info") == 0) {
      if (strlen(val) >= ECM_PROFILE_INFO_MAX) return profile_err(key, val);
      strncpy(out->key_derivation.short_key_info, val, ECM_PROFILE_INFO_MAX - 1);
      out->key_derivation.short_key_info_set = 1;
    } else if (strcmp(key, "header") == 0) {
      if (parse_header_entry(val, &out->format) != 0) return profile_err(key, val);
    } else if (strcmp(key, "include_cp_number") == 0) {
      if (parse_bool(val, &out->format.include_cp_number) != 0) return profile_err(key, val);
    } else if (strcmp(key, "include_ecm_id") == 0) {
      if (parse_bool(val, &out->format.include_ecm_id) != 0) return profile_err(key, val);
    } else if (strcmp(key, "field_order") == 0) {
      if (parse_token_list(val, &out->format.field_order) != 0) return profile_err(key, val);
      out->format.field_order_set = 1;
    } else if (strcmp(key, "wire_order") == 0) {
      if (parse_token_list(val, &out->format.wire_order) != 0) return profile_err(key, val);
      out->format.wire_order_set = 1;
    } else if (strcmp(key, "cp_number_layout") == 0) {
      if (map_lookup(cp_layout_names, N(cp_layout_names), val, &v) != 0) return profile_err(key, val);
      out->cp_number_layout = (ecm_cp_layout_t)v;
    } else if (strcmp(key, "integrity") == 0) {
      if (map_lookup(integrity_names, N(integrity_names), val, &v) != 0) return profile_err(key, val);
      out->integrity.type = (ecm_integrity_t)v;
    } else if (strcmp(key, "integrity_order") == 0) {
      if (map_lookup(integrity_order_names, N(integrity_order_names), val, &v) != 0) return profile_err(key, val);
      out->integrity.order = (ecm_integrity_order_t)v;
    } else if (strcmp(key, "truncate_tag") == 0) {
      char *end;
      long n = strtol(val, &end, 10);
      if (*end != '\0' || n <= 0) return profile_err(key, val);
      out->integrity.truncate_tag = (int)n;
    } else if (strcmp(key, "truncate_from") == 0) {
      if (map_lookup(truncate_from_names, N(truncate_from_names), val, &v) != 0) return profile_err(key, val);
      out->integrity.truncate_from = (ecm_truncate_from_t)v;
    } else if (strcmp(key, "bind_ecm_id") == 0) {
      if (parse_bool(val, &out->integrity.bind_ecm_id) != 0) return profile_err(key, val);
    } else if (strcmp(key, "bind_cp_number") == 0) {
      if (parse_bool(val, &out->integrity.bind_cp_number) != 0) return profile_err(key, val);
    } else if (strcmp(key, "crc32_variant") == 0) {
      if (map_lookup(crc32_variant_names, N(crc32_variant_names), val, &v) != 0) return profile_err(key, val);
      out->integrity.crc32_variant = (ecm_crc32_variant_t)v;
    } else if (strcmp(key, "crc32_endian") == 0) {
      if (map_lookup(crc32_endian_names, N(crc32_endian_names), val, &v) != 0) return profile_err(key, val);
      out->integrity.crc32_endian = (ecm_crc32_endian_t)v;
    } else if (strcmp(key, "cw_count") == 0) {
      char *end;
      long n = strtol(val, &end, 10);
      if (*end != '\0' || n < 0 || n > ECM_PROFILE_CW_MAX) return profile_err(key, val);
      out->cw_count = (int)n;
    } else if (strcmp(key, "cw_group") == 0) {
      if (parse_token_list(val, &out->format.cw_group) != 0) return profile_err(key, val);
    } else if (strcmp(key, "ecm_id") == 0) {
      char *end;
      unsigned long n = strtoul(val, &end, 0);
      if (*end != '\0' || n > 0xFFFFu) return profile_err(key, val);
      out->ecm_id = (unsigned)n;
      out->ecm_id_set = 1;
    } else {
      log_line(TOOL_NAME ": --ecm-profile: unknown field %s", key);
      return -1;
    }
    pair = strtok_r(NULL, ",", &saveptr);
  }
  return 0;
}

static int build_default_field_order(ecm_profile_t *p) {
  ecm_token_list_t *l = &p->format.field_order;
  l->count = 0;
  if (p->format.header_count == 1) {
    l->tok[l->count].kind = ECM_TOK_HEADER;
    strncpy(l->tok[l->count].id, p->format.headers[0].id, ECM_PROFILE_ID_MAX - 1);
    l->tok[l->count].id[ECM_PROFILE_ID_MAX - 1] = 0;
    l->count++;
  } else if (p->format.header_count > 1) {
    log_line(TOOL_NAME ": --ecm-profile: more than one format.headers entry has no default position, set field_order explicitly");
    return -1;
  }
  if (p->format.include_ecm_id) { l->tok[l->count].kind = ECM_TOK_ECM_ID; l->tok[l->count].id[0] = 0; l->count++; }
  if (p->format.include_cp_number) { l->tok[l->count].kind = ECM_TOK_CP_NUMBER; l->tok[l->count].id[0] = 0; l->count++; }
  l->tok[l->count].kind = ECM_TOK_CW; l->tok[l->count].id[0] = 0; l->count++;
  return 0;
}

static void build_default_wire_order(const ecm_profile_t *p, ecm_token_list_t *l) {
  int has_iv = cipher_is_gcm(p->cipher) || (cipher_is_cbc(p->cipher) && p->iv_source == ECM_IV_RANDOM);
  int has_itag = p->integrity.type != ECM_INTEGRITY_NONE && p->integrity.order == ECM_INTEGRITY_AFTER_ENCRYPT;
  l->count = 0;
  if (has_iv) { l->tok[l->count].kind = ECM_TOK_IV; l->tok[l->count].id[0] = 0; l->count++; }
  l->tok[l->count].kind = ECM_TOK_CIPHERTEXT; l->tok[l->count].id[0] = 0; l->count++;
  if (cipher_is_gcm(p->cipher)) { l->tok[l->count].kind = ECM_TOK_GCM_TAG; l->tok[l->count].id[0] = 0; l->count++; }
  if (has_itag) { l->tok[l->count].kind = ECM_TOK_INTEGRITY_TAG; l->tok[l->count].id[0] = 0; l->count++; }
}

static int validate_cw_group(const ecm_token_list_t *cwg) {
  int has_cw = 0, has_cpn = 0;
  int i;
  for (i = 0; i < cwg->count; i++) {
    if (cwg->tok[i].kind == ECM_TOK_CW) {
      if (has_cw) {
        log_line(TOOL_NAME ": --ecm-profile: cw_group: cw appears more than once");
        return -1;
      }
      has_cw = 1;
    } else if (cwg->tok[i].kind == ECM_TOK_CP_NUMBER) {
      if (has_cpn) {
        log_line(TOOL_NAME ": --ecm-profile: cw_group: cp_number appears more than once");
        return -1;
      }
      has_cpn = 1;
    } else {
      log_line(TOOL_NAME ": --ecm-profile: cw_group may only contain cp_number and/or cw");
      return -1;
    }
  }
  if (!has_cw) {
    log_line(TOOL_NAME ": --ecm-profile: cw_group must contain cw");
    return -1;
  }
  return 0;
}

int ecm_profile_validate(ecm_profile_t *p) {
  int i, j;
  int is_ecb = cipher_is_ecb(p->cipher), is_cbc = cipher_is_cbc(p->cipher), is_gcm = cipher_is_gcm(p->cipher);
  int mte = p->integrity.order == ECM_INTEGRITY_BEFORE_ENCRYPT && p->integrity.type != ECM_INTEGRITY_NONE;

  if (is_ecb && p->iv_source != ECM_IV_NONE) {
    log_line(TOOL_NAME ": --ecm-profile: *-ECB has no IV, iv must be none"); return -1;
  }
  if ((is_cbc || is_gcm) && p->iv_source == ECM_IV_NONE) {
    log_line(TOOL_NAME ": --ecm-profile: iv=none only legal with an ECB cipher"); return -1;
  }
  if (is_gcm && p->padding != ECM_PAD_NONE) {
    log_line(TOOL_NAME ": --ecm-profile: *-GCM is a stream cipher, padding must be none"); return -1;
  }

  if (p->integrity.truncate_tag != 0) {
    if (p->integrity.truncate_tag != 4 && p->integrity.truncate_tag != 8) {
      log_line(TOOL_NAME ": --ecm-profile: truncate_tag must be 4 or 8"); return -1;
    }
    if (p->integrity.type != ECM_INTEGRITY_HMAC_SHA256) {
      log_line(TOOL_NAME ": --ecm-profile: truncate_tag only applies to integrity=hmac-sha256"); return -1;
    }
  }

  if (p->key_derivation.short_key_info_set && p->key_derivation.short_key_source == ECM_SHORT_KEY_TRUNCATE) {
    log_line(TOOL_NAME ": --ecm-profile: short_key_info is inert with short_key_source=truncate"); return -1;
  }
  if (p->key_derivation.short_key_source == ECM_SHORT_KEY_SEPARATE_INFO && !p->key_derivation.hkdf) {
    log_line(TOOL_NAME ": --ecm-profile: short_key_source=separate_info needs hkdf=1"); return -1;
  }

  for (i = 0; i < p->format.header_count; i++) {
    if (is_reserved_id(p->format.headers[i].id)) {
      log_line(TOOL_NAME ": --ecm-profile: header id \"%s\" is a reserved token keyword", p->format.headers[i].id); return -1;
    }
    for (j = i + 1; j < p->format.header_count; j++)
      if (strcmp(p->format.headers[i].id, p->format.headers[j].id) == 0) {
        log_line(TOOL_NAME ": --ecm-profile: duplicate header id \"%s\"", p->format.headers[i].id); return -1;
      }
  }

  if (mte && !p->format.field_order_set) {
    log_line(TOOL_NAME ": --ecm-profile: integrity_order=before-encrypt needs an explicit field_order placing integrity_tag last"); return -1;
  }
  if (p->cw_count > 1 && !p->format.field_order_set) {
    log_line(TOOL_NAME ": --ecm-profile: cw_count>1 needs an explicit field_order using the cw_group token"); return -1;
  }
  if (!p->format.field_order_set && build_default_field_order(p) != 0)
    return -1;

  if (p->cw_count > 1) {
    if (p->format.cw_group.count == 0) {
      log_line(TOOL_NAME ": --ecm-profile: cw_count>1 needs format.cw_group set"); return -1;
    }
    if (validate_cw_group(&p->format.cw_group) != 0)
      return -1;
  } else if (p->format.cw_group.count != 0) {
    log_line(TOOL_NAME ": --ecm-profile: cw_group is only legal when cw_count>1"); return -1;
  }

  {
    const ecm_token_list_t *fo = &p->format.field_order;
    int ecm_id_n = count_kind(fo, ECM_TOK_ECM_ID), cpn_n = count_kind(fo, ECM_TOK_CP_NUMBER);
    int cw_n = count_kind(fo, ECM_TOK_CW), cwg_n = count_kind(fo, ECM_TOK_CW_GROUP), itag_n = count_kind(fo, ECM_TOK_INTEGRITY_TAG);
    if (ecm_id_n != (p->format.include_ecm_id ? 1 : 0)) {
      log_line(TOOL_NAME ": --ecm-profile: field_order must contain ecm_id exactly once iff include_ecm_id=1"); return -1;
    }
    if (p->cw_count > 1) {
      if (cw_n != 0) { log_line(TOOL_NAME ": --ecm-profile: cw is only legal inside cw_group when cw_count>1"); return -1; }
      if (cwg_n != 1) { log_line(TOOL_NAME ": --ecm-profile: field_order must contain cw_group exactly once when cw_count>1"); return -1; }
      if (cpn_n > 1 || (cpn_n == 1 && !p->format.include_cp_number)) {
        log_line(TOOL_NAME ": --ecm-profile: field_order: cp_number placement inconsistent with include_cp_number"); return -1;
      }
    } else {
      if (cwg_n != 0) { log_line(TOOL_NAME ": --ecm-profile: cw_group is only legal when cw_count>1"); return -1; }
      if (cw_n != 1) { log_line(TOOL_NAME ": --ecm-profile: field_order must contain cw exactly once"); return -1; }
      if (cpn_n != (p->format.include_cp_number ? 1 : 0)) {
        log_line(TOOL_NAME ": --ecm-profile: field_order must contain cp_number exactly once iff include_cp_number=1"); return -1;
      }
    }
    if (mte) {
      if (itag_n != 1 || fo->tok[fo->count - 1].kind != ECM_TOK_INTEGRITY_TAG) {
        log_line(TOOL_NAME ": --ecm-profile: integrity_order=before-encrypt needs integrity_tag exactly once, last in field_order"); return -1;
      }
    } else if (itag_n != 0) {
      log_line(TOOL_NAME ": --ecm-profile: integrity_tag in field_order needs integrity_order=before-encrypt"); return -1;
    }
    for (i = 0; i < fo->count; i++) {
      if (fo->tok[i].kind == ECM_TOK_IV || fo->tok[i].kind == ECM_TOK_CIPHERTEXT || fo->tok[i].kind == ECM_TOK_GCM_TAG) {
        log_line(TOOL_NAME ": --ecm-profile: field_order may not contain iv/ciphertext/gcm_tag (wire_order tokens)"); return -1;
      }
      if (fo->tok[i].kind == ECM_TOK_HEADER && !find_header(&p->format, fo->tok[i].id)) {
        log_line(TOOL_NAME ": --ecm-profile: field_order references unknown header id \"%s\"", fo->tok[i].id); return -1;
      }
    }
  }

  if (!p->format.wire_order_set)
    build_default_wire_order(p, &p->format.wire_order);
  {
    const ecm_token_list_t *wo = &p->format.wire_order;
    int has_iv = is_gcm || (is_cbc && p->iv_source == ECM_IV_RANDOM);
    int has_itag = p->integrity.type != ECM_INTEGRITY_NONE && p->integrity.order == ECM_INTEGRITY_AFTER_ENCRYPT;
    int iv_n = count_kind(wo, ECM_TOK_IV), ct_n = count_kind(wo, ECM_TOK_CIPHERTEXT);
    int gcm_n = count_kind(wo, ECM_TOK_GCM_TAG), itag_n = count_kind(wo, ECM_TOK_INTEGRITY_TAG);

    if (iv_n != (has_iv ? 1 : 0)) { log_line(TOOL_NAME ": --ecm-profile: wire_order: iv presence inconsistent with cipher/iv_source"); return -1; }
    if (ct_n != 1) { log_line(TOOL_NAME ": --ecm-profile: wire_order must contain ciphertext exactly once"); return -1; }
    if (gcm_n != (is_gcm ? 1 : 0)) { log_line(TOOL_NAME ": --ecm-profile: wire_order: gcm_tag presence inconsistent with cipher"); return -1; }
    if (itag_n != (has_itag ? 1 : 0)) { log_line(TOOL_NAME ": --ecm-profile: wire_order: integrity_tag presence inconsistent with integrity settings"); return -1; }
    for (i = 0; i < wo->count; i++) {
      if (wo->tok[i].kind == ECM_TOK_ECM_ID || wo->tok[i].kind == ECM_TOK_CP_NUMBER || wo->tok[i].kind == ECM_TOK_CW || wo->tok[i].kind == ECM_TOK_CW_GROUP) {
        log_line(TOOL_NAME ": --ecm-profile: wire_order may not contain ecm_id/cp_number/cw/cw_group (field_order tokens)"); return -1;
      }
      if (wo->tok[i].kind == ECM_TOK_HEADER && !find_header(&p->format, wo->tok[i].id)) {
        log_line(TOOL_NAME ": --ecm-profile: wire_order references unknown header id \"%s\"", wo->tok[i].id); return -1;
      }
    }
  }
  return 0;
}

static size_t cw_group_unit_len(const ecm_token_list_t *cwg, int cw_len) {
  size_t unit = 0;
  int j;
  for (j = 0; j < cwg->count; j++) {
    if (cwg->tok[j].kind == ECM_TOK_CW)
      unit += (size_t)cw_len;
    else if (cwg->tok[j].kind == ECM_TOK_CP_NUMBER)
      unit += 2;
  }
  return unit;
}

int ecm_profile_layout(const ecm_profile_t *p, int cw_len, ecm_layout_t *out) {
  size_t plaintext_len = 0, block = (size_t)cipher_block_size(p->cipher);
  int is_gcm = cipher_is_gcm(p->cipher), is_cbc = cipher_is_cbc(p->cipher);
  size_t itag_len = integrity_tag_wire_len(p);
  const ecm_token_list_t *fo = &p->format.field_order;
  int i, k;
  memset(out, 0, sizeof *out);
  for (i = 0; i < fo->count; i++) {
    switch (fo->tok[i].kind) {
    case ECM_TOK_HEADER: {
      const ecm_header_t *h = find_header(&p->format, fo->tok[i].id);
      plaintext_len += h ? (size_t)h->len : 0;
      break;
    }
    case ECM_TOK_ECM_ID: plaintext_len += 2; break;
    case ECM_TOK_CP_NUMBER: plaintext_len += 2; break;
    case ECM_TOK_CW: plaintext_len += (size_t)cw_len; break;
    case ECM_TOK_CW_GROUP:
      plaintext_len += cw_group_unit_len(&p->format.cw_group, cw_len) * (size_t)p->cw_count;
      break;
    case ECM_TOK_INTEGRITY_TAG: plaintext_len += itag_len; break;
    default: break;
    }
  }
  out->plaintext_len = plaintext_len;

  if (is_gcm) {
    out->iv_len = CRYPTO_GCM_NONCE_LEN;
    out->ciphertext_len = plaintext_len;
    out->gcm_tag_len = CRYPTO_GCM_TAG_LEN;
  } else {
    out->iv_len = (is_cbc && p->iv_source == ECM_IV_RANDOM) ? block : 0;
    if (p->padding == ECM_PAD_NONE) {
      if (block == 0 || plaintext_len % block != 0)
        return -1;
      out->ciphertext_len = plaintext_len;
    } else if (p->padding == ECM_PAD_ZERO) {
      out->ciphertext_len = block ? ((plaintext_len + block - 1) / block) * block : plaintext_len;
      if (out->ciphertext_len == 0) out->ciphertext_len = block;
    } else {
      out->ciphertext_len = block ? (plaintext_len / block + 1) * block : plaintext_len;
    }
  }
  out->integrity_tag_len = (p->integrity.order == ECM_INTEGRITY_AFTER_ENCRYPT) ? itag_len : 0;
  out->wire_len = out->iv_len + out->ciphertext_len + out->gcm_tag_len + out->integrity_tag_len;

  for (k = 0; k < p->format.wire_order.count; k++)
    if (p->format.wire_order.tok[k].kind == ECM_TOK_HEADER) {
      const ecm_header_t *h = find_header(&p->format, p->format.wire_order.tok[k].id);
      out->wire_len += h ? (size_t)h->len : 0;
    }
  return 0;
}

static int locate_wire_components(const ecm_profile_t *p, const ecm_layout_t *lay, const unsigned char *wire, size_t wire_len, const unsigned char **iv, const unsigned char **ct, const unsigned char **gcm_tag, const unsigned char **itag) {
  size_t off = 0;
  int i;
  *iv = NULL; *ct = NULL; *gcm_tag = NULL; *itag = NULL;
  for (i = 0; i < p->format.wire_order.count; i++) {
    const ecm_token_t *t = &p->format.wire_order.tok[i];
    size_t len = 0;
    const unsigned char **dest = NULL;

    if (t->kind == ECM_TOK_HEADER) {
      const ecm_header_t *h = find_header(&p->format, t->id);
      if (!h || off + (size_t)h->len > wire_len)
        return -1;
      if (memcmp(wire + off, h->data, (size_t)h->len) != 0)
        return -1; /* marker mismatch: wrong profile, or corrupt/foreign ECM section */
      off += (size_t)h->len;
      continue;
    }
    switch (t->kind) {
    case ECM_TOK_IV: len = lay->iv_len; dest = iv; break;
    case ECM_TOK_CIPHERTEXT: len = lay->ciphertext_len; dest = ct; break;
    case ECM_TOK_GCM_TAG: len = lay->gcm_tag_len; dest = gcm_tag; break;
    case ECM_TOK_INTEGRITY_TAG: len = lay->integrity_tag_len; dest = itag; break;
    default: return -1;
    }
    if (len == 0)
      continue;
    if (off + len > wire_len)
      return -1;
    *dest = wire + off;
    off += len;
  }
  return off == wire_len ? 0 : -1;
}

static void serialize_crc32(uint32_t v, ecm_crc32_endian_t endian, unsigned char out[4]) {
  if (endian == ECM_CRC32_BIG) {
    out[0] = (unsigned char)(v >> 24); out[1] = (unsigned char)(v >> 16);
    out[2] = (unsigned char)(v >> 8); out[3] = (unsigned char)v;
  } else {
    out[0] = (unsigned char)v; out[1] = (unsigned char)(v >> 8);
    out[2] = (unsigned char)(v >> 16); out[3] = (unsigned char)(v >> 24);
  }
}

/* recomputes integrity tag data||assoc (EtM: data=ciphertext, MtE: data=plain before tag).
   CRC32 stays plain, unkeyed IEEE/Castagnoli checksum */
static int compute_tag(const ecm_profile_t *p, const unsigned char mac_key[CRYPTO_KEY_LEN], const unsigned char *data, size_t data_len, const unsigned char *assoc, size_t assoc_len,
                       unsigned char *tag_out, size_t *tag_len_out) {
  unsigned char buf[ECM_PROFILE_WIRE_MAX + 8];
  if (data_len + assoc_len > sizeof buf)
    return -1;
  memcpy(buf, data, data_len);
  memcpy(buf + data_len, assoc, assoc_len);
  if (p->integrity.type == ECM_INTEGRITY_CRC32) {
    uint32_t crc = crypto_crc32(p->integrity.crc32_variant == ECM_CRC32_CASTAGNOLI, buf, data_len + assoc_len);
    serialize_crc32(crc, p->integrity.crc32_endian, tag_out);
    *tag_len_out = 4;
    return 0;
  }
  /* hmac-sha256 */
  {
    unsigned char full[CRYPTO_HMAC_SHA256_LEN];
    size_t want = p->integrity.truncate_tag ? (size_t)p->integrity.truncate_tag : CRYPTO_HMAC_SHA256_LEN;
    if (crypto_hmac_sha256(mac_key, buf, data_len + assoc_len, full) != 0)
      return -1;
    if (p->integrity.truncate_from == ECM_TRUNCATE_RIGHT)
      memcpy(tag_out, full + (CRYPTO_HMAC_SHA256_LEN - want), want);
    else
      memcpy(tag_out, full, want);
    *tag_len_out = want;
    return 0;
  }
}

/* one cw_group's worth of tokens for a single combo, advancing *off. cp_number
   falls back to cp_number_outer + combo index (mod 65536) if not carried in the group */
static void extract_cw_group_combo(const ecm_token_list_t *cwg, const unsigned char *plaintext, size_t *off, int cw_len,
                                    ecm_cw_combo_t *combo, unsigned cp_number_fallback) {
  int j, has_cpn = 0;
  for (j = 0; j < cwg->count; j++) {
    if (cwg->tok[j].kind == ECM_TOK_CP_NUMBER) {
      has_cpn = 1;
      combo->cp_number = ((unsigned)plaintext[*off] << 8) | plaintext[*off + 1];
      *off += 2;
    } else if (cwg->tok[j].kind == ECM_TOK_CW) {
      memcpy(combo->cw, plaintext + *off, (size_t)cw_len);
      *off += (size_t)cw_len;
    }
  }
  if (!has_cpn)
    combo->cp_number = cp_number_fallback;
}

int ecm_profile_decrypt_cw(const ecm_profile_t *p, int cw_len, const unsigned char sk[CRYPTO_KEY_LEN], const unsigned char *wire, size_t wire_len, unsigned cp_number_outer,
                           unsigned ecm_id_fallback, ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX], int *combo_count) {
  ecm_layout_t lay;
  unsigned char full_enc_key[CRYPTO_KEY_LEN], mac_key[CRYPTO_KEY_LEN], enc_key[CRYPTO_KEY_LEN];
  unsigned char iv[16];
  unsigned char plaintext[ECM_PROFILE_WIRE_MAX];
  unsigned char assoc[4];
  const unsigned char *iv_ptr, *ct_ptr, *gcm_tag_ptr, *itag_ptr;
  unsigned ecm_id = p->ecm_id_set ? p->ecm_id : ecm_id_fallback;
  int is_gcm, is_cbc;
  size_t klen, block, assoc_len = 0, off;
  int i;
  if (ecm_profile_layout(p, cw_len, &lay) != 0 || lay.wire_len != wire_len || lay.plaintext_len > sizeof plaintext)
    return -1;
  if (locate_wire_components(p, &lay, wire, wire_len, &iv_ptr, &ct_ptr, &gcm_tag_ptr, &itag_ptr) != 0)
    return -1;

  is_gcm = cipher_is_gcm(p->cipher);
  is_cbc = cipher_is_cbc(p->cipher);
  block = (size_t)cipher_block_size(p->cipher);

  if (p->key_derivation.hkdf) {
    if (crypto_hkdf_sha256(sk, p->key_derivation.enc_info, full_enc_key) != 0)
      return -1;
    if (p->integrity.type == ECM_INTEGRITY_HMAC_SHA256 && crypto_hkdf_sha256(sk, p->key_derivation.mac_info, mac_key) != 0)
      return -1;
  } else {
    memcpy(full_enc_key, sk, CRYPTO_KEY_LEN);
    memcpy(mac_key, sk, CRYPTO_KEY_LEN);
  }

  klen = (size_t)cipher_key_len(p->cipher);
  if (klen < CRYPTO_KEY_LEN && p->key_derivation.short_key_source == ECM_SHORT_KEY_SEPARATE_INFO) {
    unsigned char sep[CRYPTO_HMAC_SHA256_LEN];
    if (crypto_hkdf_sha256(sk, p->key_derivation.short_key_info, sep) != 0)
      return -1;
    memcpy(enc_key, sep, klen);
    secure_zero(sep, sizeof sep);
  } else {
    memcpy(enc_key, full_enc_key, klen);
  }
  secure_zero(full_enc_key, sizeof full_enc_key);

  if (is_gcm || is_cbc) {
    size_t ivlen = is_gcm ? (size_t)CRYPTO_GCM_NONCE_LEN : block;
    if (p->iv_source == ECM_IV_RANDOM) {
      memcpy(iv, iv_ptr, ivlen);
    } else {
      memset(iv, 0, ivlen);
      if (p->iv_source == ECM_IV_CP_NUMBER) {
        size_t hi = p->cp_number_layout == ECM_CP_LAYOUT_BACK ? ivlen - 2 : 0;
        iv[hi] = (unsigned char)(cp_number_outer >> 8);
        iv[hi + 1] = (unsigned char)cp_number_outer;
      }
    }
  }

  if (p->integrity.bind_ecm_id) { assoc[assoc_len++] = (unsigned char)(ecm_id >> 8); assoc[assoc_len++] = (unsigned char)ecm_id; }
  if (p->integrity.bind_cp_number) { assoc[assoc_len++] = (unsigned char)(cp_number_outer >> 8); assoc[assoc_len++] = (unsigned char)cp_number_outer; }

  if (p->integrity.type != ECM_INTEGRITY_NONE && p->integrity.order == ECM_INTEGRITY_AFTER_ENCRYPT) {
    unsigned char tag[CRYPTO_HMAC_SHA256_LEN];
    size_t tag_len;
    if (compute_tag(p, mac_key, ct_ptr, lay.ciphertext_len, assoc, assoc_len, tag, &tag_len) != 0)
      return -1;
    if (tag_len != lay.integrity_tag_len || memcmp(tag, itag_ptr, tag_len) != 0)
      return -1;
  }

  if (is_gcm) {
    if (crypto_ecm_gcm_decrypt(klen == 16 ? 128 : 256, enc_key, iv, assoc, assoc_len, ct_ptr, lay.ciphertext_len, gcm_tag_ptr, plaintext) != 0)
      return -1;
  } else {
    if (crypto_ecm_block_decrypt(cipher_to_crypto(p->cipher), is_cbc, enc_key, is_cbc ? iv : NULL, ct_ptr, lay.ciphertext_len, plaintext) != 0)
      return -1;
  }
  secure_zero(enc_key, sizeof enc_key);

  if (p->integrity.type != ECM_INTEGRITY_NONE && p->integrity.order == ECM_INTEGRITY_BEFORE_ENCRYPT) {
    unsigned char tag[CRYPTO_HMAC_SHA256_LEN];
    size_t tag_len, data_len = lay.plaintext_len - integrity_tag_wire_len(p);
    if (compute_tag(p, mac_key, plaintext, data_len, assoc, assoc_len, tag, &tag_len) != 0)
      return -1;
    if (tag_len != integrity_tag_wire_len(p) || memcmp(tag, plaintext + data_len, tag_len) != 0)
      return -1;
  }
  secure_zero(mac_key, sizeof mac_key);
  *combo_count = 0;
  off = 0;
  combos[0].cp_number = cp_number_outer; /* default when field_order doesn't carry cp_number at all */
  for (i = 0; i < p->format.field_order.count; i++) {
    const ecm_token_t *t = &p->format.field_order.tok[i];
    switch (t->kind) {
    case ECM_TOK_HEADER: {
      const ecm_header_t *h = find_header(&p->format, t->id);
      off += h ? (size_t)h->len : 0;
      break;
    }
    case ECM_TOK_ECM_ID: off += 2; break;
    case ECM_TOK_CP_NUMBER:
      combos[0].cp_number = ((unsigned)plaintext[off] << 8) | plaintext[off + 1];
      off += 2;
      break;
    case ECM_TOK_CW:
      memcpy(combos[0].cw, plaintext + off, (size_t)cw_len);
      off += (size_t)cw_len;
      *combo_count = 1;
      break;
    case ECM_TOK_CW_GROUP: {
      int c;
      for (c = 0; c < p->cw_count; c++)
        extract_cw_group_combo(&p->format.cw_group, plaintext, &off, cw_len, &combos[c], (cp_number_outer + (unsigned)c) & 0xFFFFu);
      *combo_count = p->cw_count;
      break;
    }
    case ECM_TOK_INTEGRITY_TAG: off += integrity_tag_wire_len(p); break;
    default: break;
    }
  }
  secure_zero(plaintext, sizeof plaintext);
  return *combo_count > 0 ? 0 : -1;
}
