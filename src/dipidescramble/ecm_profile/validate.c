/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "../version.h"
#include "priv.h"

#define VFAIL(...) do { log_line(TOOL_NAME ": --ecm-profile: " __VA_ARGS__); return -1; } while (0)

static int count_kind(const ecm_token_list_t *l, ecm_token_kind_t k) {
  int n = 0;
  for (int i = 0; i < l->count; i++) {
    if (l->tok[i].kind == k) n++;
  }
  return n;
}

static int build_default_field_order(ecm_profile_t *p) {
  ecm_token_list_t *l = &p->format.field_order;
  l->count = 0;
  if (p->format.header_count == 1) {
    l->tok[l->count].kind = ECM_TOK_HEADER;
    bufcpy(l->tok[l->count].id, ECM_PROFILE_ID_MAX, p->format.headers[0].id);
    l->count++;
  } else if (p->format.header_count > 1) {
    VFAIL("more than one format.headers entry has no default position, set field_order explicitly");
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
  int has_cw = 0;
  int has_cpn = 0;
  for (int i = 0; i < cwg->count; i++) {
    switch (cwg->tok[i].kind) {
      case ECM_TOK_CW:
        if (has_cw) VFAIL("cw_group: cw appears more than once");
        has_cw = 1;
        break;
      case ECM_TOK_CP_NUMBER:
        if (has_cpn) VFAIL("cw_group: cp_number appears more than once");
        has_cpn = 1;
        break;
      default:
        VFAIL("cw_group may only contain cp_number and/or cw");
    }
  }
  if (!has_cw) VFAIL("cw_group must contain cw");
  return 0;
}

int ecm_profile_validate(ecm_profile_t *p) {
  int is_ecb = cipher_is_ecb(p->cipher);
  int is_cbc = cipher_is_cbc(p->cipher);
  int is_gcm = cipher_is_gcm(p->cipher);
  int mte = p->integrity.order == ECM_INTEGRITY_BEFORE_ENCRYPT && p->integrity.type != ECM_INTEGRITY_NONE;

  if (is_ecb && p->iv_source != ECM_IV_NONE) VFAIL("*-ECB has no IV, iv must be none");
  if ((is_cbc || is_gcm) && p->iv_source == ECM_IV_NONE) VFAIL("iv=none only legal with an ECB cipher");
  if (is_gcm && p->padding != ECM_PAD_NONE) VFAIL("*-GCM is a stream cipher, padding must be none");

  if (p->integrity.truncate_tag != 0) {
    if (p->integrity.truncate_tag != 4 && p->integrity.truncate_tag != 8) VFAIL("truncate_tag must be 4 or 8");
    if (p->integrity.type != ECM_INTEGRITY_HMAC_SHA256) VFAIL("truncate_tag only applies to integrity=hmac-sha256");
  }

  if (p->key_derivation.short_key_info_set && p->key_derivation.short_key_source == ECM_SHORT_KEY_TRUNCATE)
    VFAIL("short_key_info is inert with short_key_source=truncate");
  if (p->key_derivation.short_key_source == ECM_SHORT_KEY_SEPARATE_INFO && !p->key_derivation.hkdf)
    VFAIL("short_key_source=separate_info needs hkdf=1");

  for (int i = 0; i < p->format.header_count; i++) {
    if (is_reserved_id(p->format.headers[i].id))
      VFAIL("header id \"%s\" is a reserved token keyword", p->format.headers[i].id);
    for (int j = i + 1; j < p->format.header_count; j++)
      if (strcmp(p->format.headers[i].id, p->format.headers[j].id) == 0)
        VFAIL("duplicate header id \"%s\"", p->format.headers[i].id);
  }

  if (mte && !p->format.field_order_set)
    VFAIL("integrity_order=before-encrypt needs an explicit field_order placing integrity_tag last");
  if (p->cw_count > 1 && !p->format.field_order_set)
    VFAIL("cw_count>1 needs an explicit field_order using the cw_group token");
  if (!p->format.field_order_set && build_default_field_order(p) != 0)
    return -1;

  if (p->cw_count > 1) {
    if (p->format.cw_group.count == 0) VFAIL("cw_count>1 needs format.cw_group set");
    if (validate_cw_group(&p->format.cw_group) != 0)
      return -1;
  } else if (p->format.cw_group.count != 0) {
    VFAIL("cw_group is only legal when cw_count>1");
  }

  {
    const ecm_token_list_t *fo = &p->format.field_order;
    int ecm_id_n = count_kind(fo, ECM_TOK_ECM_ID);
    int cpn_n = count_kind(fo, ECM_TOK_CP_NUMBER);
    int cw_n = count_kind(fo, ECM_TOK_CW);
    int cwg_n = count_kind(fo, ECM_TOK_CW_GROUP);
    int itag_n = count_kind(fo, ECM_TOK_INTEGRITY_TAG);
    if (ecm_id_n != (p->format.include_ecm_id ? 1 : 0))
      VFAIL("field_order must contain ecm_id exactly once iff include_ecm_id=1");
    if (p->cw_count > 1) {
      if (cw_n != 0) VFAIL("cw is only legal inside cw_group when cw_count>1");
      if (cwg_n != 1) VFAIL("field_order must contain cw_group exactly once when cw_count>1");
      if (cpn_n > 1 || (cpn_n == 1 && !p->format.include_cp_number))
        VFAIL("field_order: cp_number placement inconsistent with include_cp_number");
    } else {
      if (cwg_n != 0) VFAIL("cw_group is only legal when cw_count>1");
      if (cw_n != 1) VFAIL("field_order must contain cw exactly once");
      if (cpn_n != (p->format.include_cp_number ? 1 : 0))
        VFAIL("field_order must contain cp_number exactly once iff include_cp_number=1");
    }
    if (mte) {
      if (itag_n != 1 || fo->tok[fo->count - 1].kind != ECM_TOK_INTEGRITY_TAG)
        VFAIL("integrity_order=before-encrypt needs integrity_tag exactly once, last in field_order");
    } else if (itag_n != 0) {
      VFAIL("integrity_tag in field_order needs integrity_order=before-encrypt");
    }
    for (int i = 0; i < fo->count; i++) {
      switch (fo->tok[i].kind) {
        case ECM_TOK_IV:
        case ECM_TOK_CIPHERTEXT:
        case ECM_TOK_GCM_TAG:
          VFAIL("field_order may not contain iv/ciphertext/gcm_tag (wire_order tokens)");
        case ECM_TOK_HEADER:
          if (!find_header(&p->format, fo->tok[i].id))
            VFAIL("field_order references unknown header id \"%s\"", fo->tok[i].id);
          break;
        default:
          break;
      }
    }
  }

  if (!p->format.wire_order_set)
    build_default_wire_order(p, &p->format.wire_order);
  {
    const ecm_token_list_t *wo = &p->format.wire_order;
    int has_iv = is_gcm || (is_cbc && p->iv_source == ECM_IV_RANDOM);
    int has_itag = p->integrity.type != ECM_INTEGRITY_NONE && p->integrity.order == ECM_INTEGRITY_AFTER_ENCRYPT;
    int iv_n = count_kind(wo, ECM_TOK_IV);
    int ct_n = count_kind(wo, ECM_TOK_CIPHERTEXT);
    int gcm_n = count_kind(wo, ECM_TOK_GCM_TAG);
    int itag_n = count_kind(wo, ECM_TOK_INTEGRITY_TAG);

    if (iv_n != (has_iv ? 1 : 0)) VFAIL("wire_order: iv presence inconsistent with cipher/iv_source");
    if (ct_n != 1) VFAIL("wire_order must contain ciphertext exactly once");
    if (gcm_n != (is_gcm ? 1 : 0)) VFAIL("wire_order: gcm_tag presence inconsistent with cipher");
    if (itag_n != (has_itag ? 1 : 0)) VFAIL("wire_order: integrity_tag presence inconsistent with integrity settings");
    for (int i = 0; i < wo->count; i++) {
      switch (wo->tok[i].kind) {
        case ECM_TOK_ECM_ID:
        case ECM_TOK_CP_NUMBER:
        case ECM_TOK_CW:
        case ECM_TOK_CW_GROUP:
          VFAIL("wire_order may not contain ecm_id/cp_number/cw/cw_group (field_order tokens)");
        case ECM_TOK_HEADER:
          if (!find_header(&p->format, wo->tok[i].id))
            VFAIL("wire_order references unknown header id \"%s\"", wo->tok[i].id);
          break;
        default:
          break;
      }
    }
  }
  return 0;
}
