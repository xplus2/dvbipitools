/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/log.h"

#include "../version.h"
#include "priv.h"

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

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int hex_decode(const char *hex, unsigned char *out, int out_max, int *out_len) {
  size_t hexlen = strlen(hex);
  if (hexlen == 0 || hexlen % 2 != 0 || (int)(hexlen / 2) > out_max)
    return -1;
  for (size_t i = 0; i < hexlen; i += 2) {
    int hi = hex_nibble(hex[i]);
    int lo = hex_nibble(hex[i + 1]);
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
  ecm_token_kind_t k;
  if (lookup_fixed_token(name, &k) == 0) {
    out->kind = k;
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
  const char *tok = strtok_r(val, "+", &saveptr);
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

static int set_cipher(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(cipher_names, N(cipher_names), val, &v) != 0) return -1;
  out->cipher = (ecm_cipher_t)v;
  return 0;
}
static int set_iv(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(iv_source_names, N(iv_source_names), val, &v) != 0) return -1;
  out->iv_source = (ecm_iv_source_t)v;
  return 0;
}
static int set_padding(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(padding_names, N(padding_names), val, &v) != 0) return -1;
  out->padding = (ecm_padding_t)v;
  return 0;
}
static int set_hkdf(ecm_profile_t *out, char *val) { return parse_bool(val, &out->key_derivation.hkdf); }
static int set_enc_info(ecm_profile_t *out, char *val) {
  if (strlen(val) >= ECM_PROFILE_INFO_MAX) return -1;
  strncpy(out->key_derivation.enc_info, val, ECM_PROFILE_INFO_MAX - 1);
  return 0;
}
static int set_mac_info(ecm_profile_t *out, char *val) {
  if (strlen(val) >= ECM_PROFILE_INFO_MAX) return -1;
  strncpy(out->key_derivation.mac_info, val, ECM_PROFILE_INFO_MAX - 1);
  return 0;
}
static int set_short_key_source(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(short_key_source_names, N(short_key_source_names), val, &v) != 0) return -1;
  out->key_derivation.short_key_source = (ecm_short_key_source_t)v;
  return 0;
}
static int set_short_key_info(ecm_profile_t *out, char *val) {
  if (strlen(val) >= ECM_PROFILE_INFO_MAX) return -1;
  strncpy(out->key_derivation.short_key_info, val, ECM_PROFILE_INFO_MAX - 1);
  out->key_derivation.short_key_info_set = 1;
  return 0;
}
static int set_header(ecm_profile_t *out, char *val) { return parse_header_entry(val, &out->format); }
static int set_include_cp_number(ecm_profile_t *out, char *val) { return parse_bool(val, &out->format.include_cp_number); }
static int set_include_ecm_id(ecm_profile_t *out, char *val) { return parse_bool(val, &out->format.include_ecm_id); }
static int set_field_order(ecm_profile_t *out, char *val) {
  if (parse_token_list(val, &out->format.field_order) != 0) return -1;
  out->format.field_order_set = 1;
  return 0;
}
static int set_wire_order(ecm_profile_t *out, char *val) {
  if (parse_token_list(val, &out->format.wire_order) != 0) return -1;
  out->format.wire_order_set = 1;
  return 0;
}
static int set_cp_number_layout(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(cp_layout_names, N(cp_layout_names), val, &v) != 0) return -1;
  out->cp_number_layout = (ecm_cp_layout_t)v;
  return 0;
}
static int set_integrity(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(integrity_names, N(integrity_names), val, &v) != 0) return -1;
  out->integrity.type = (ecm_integrity_t)v;
  return 0;
}
static int set_integrity_order(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(integrity_order_names, N(integrity_order_names), val, &v) != 0) return -1;
  out->integrity.order = (ecm_integrity_order_t)v;
  return 0;
}
static int set_truncate_tag(ecm_profile_t *out, char *val) {
  char *end;
  long n = strtol(val, &end, 10);
  if (*end != '\0' || n <= 0) return -1;
  out->integrity.truncate_tag = (int)n;
  return 0;
}
static int set_truncate_from(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(truncate_from_names, N(truncate_from_names), val, &v) != 0) return -1;
  out->integrity.truncate_from = (ecm_truncate_from_t)v;
  return 0;
}
static int set_bind_ecm_id(ecm_profile_t *out, char *val) { return parse_bool(val, &out->integrity.bind_ecm_id); }
static int set_bind_cp_number(ecm_profile_t *out, char *val) { return parse_bool(val, &out->integrity.bind_cp_number); }
static int set_crc32_variant(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(crc32_variant_names, N(crc32_variant_names), val, &v) != 0) return -1;
  out->integrity.crc32_variant = (ecm_crc32_variant_t)v;
  return 0;
}
static int set_crc32_endian(ecm_profile_t *out, char *val) {
  int v;
  if (map_lookup(crc32_endian_names, N(crc32_endian_names), val, &v) != 0) return -1;
  out->integrity.crc32_endian = (ecm_crc32_endian_t)v;
  return 0;
}
static int set_cw_count(ecm_profile_t *out, char *val) {
  char *end;
  long n = strtol(val, &end, 10);
  if (*end != '\0' || n < 0 || n > ECM_PROFILE_CW_MAX) return -1;
  out->cw_count = (int)n;
  return 0;
}
static int set_cw_group(ecm_profile_t *out, char *val) { return parse_token_list(val, &out->format.cw_group); }
static int set_ecm_id(ecm_profile_t *out, char *val) {
  char *end;
  unsigned long n = strtoul(val, &end, 0);
  if (*end != '\0' || n > 0xFFFFu) return -1;
  out->ecm_id = (unsigned)n;
  out->ecm_id_set = 1;
  return 0;
}

typedef int (*ecm_field_setter_fn)(ecm_profile_t *out, char *val);

typedef struct {
  const char *key;
  ecm_field_setter_fn set;
} ecm_field_setter_t;

static const ecm_field_setter_t field_setters[] = {
    {"cipher", set_cipher},
    {"iv", set_iv},
    {"padding", set_padding},
    {"hkdf", set_hkdf},
    {"enc_info", set_enc_info},
    {"mac_info", set_mac_info},
    {"short_key_source", set_short_key_source},
    {"short_key_info", set_short_key_info},
    {"header", set_header},
    {"include_cp_number", set_include_cp_number},
    {"include_ecm_id", set_include_ecm_id},
    {"field_order", set_field_order},
    {"wire_order", set_wire_order},
    {"cp_number_layout", set_cp_number_layout},
    {"integrity", set_integrity},
    {"integrity_order", set_integrity_order},
    {"truncate_tag", set_truncate_tag},
    {"truncate_from", set_truncate_from},
    {"bind_ecm_id", set_bind_ecm_id},
    {"bind_cp_number", set_bind_cp_number},
    {"crc32_variant", set_crc32_variant},
    {"crc32_endian", set_crc32_endian},
    {"cw_count", set_cw_count},
    {"cw_group", set_cw_group},
    {"ecm_id", set_ecm_id},
};

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
    char *key;
    char *val;

    if (!eq) {
      log_line(TOOL_NAME ": --ecm-profile: malformed field %s (expected key=value)", pair);
      return -1;
    }
    *eq = 0;
    key = pair;
    val = eq + 1;

    {
      const ecm_field_setter_t *found = NULL;
      for (size_t fi = 0; fi < N(field_setters); fi++)
        if (strcmp(key, field_setters[fi].key) == 0) {
          found = &field_setters[fi];
          break;
        }
      if (!found) {
        log_line(TOOL_NAME ": --ecm-profile: unknown field %s", key);
        return -1;
      }
      if (found->set(out, val) != 0)
        return profile_err(key, val);
    }
    pair = strtok_r(NULL, ",", &saveptr);
  }
  return 0;
}
