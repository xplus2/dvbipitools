/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "priv.h"

static size_t cw_group_unit_len(const ecm_token_list_t *cwg, int cw_len) {
  size_t unit = 0;
  for (int j = 0; j < cwg->count; j++) {
    if (cwg->tok[j].kind == ECM_TOK_CW)
      unit += (size_t)cw_len;
    else if (cwg->tok[j].kind == ECM_TOK_CP_NUMBER)
      unit += 2;
  }
  return unit;
}

int ecm_profile_layout(const ecm_profile_t *p, int cw_len, ecm_layout_t *out) {
  size_t plaintext_len = 0;
  size_t block = (size_t)cipher_block_size(p->cipher);
  int is_gcm = cipher_is_gcm(p->cipher);
  int is_cbc = cipher_is_cbc(p->cipher);
  size_t itag_len = integrity_tag_wire_len(p);
  const ecm_token_list_t *fo = &p->format.field_order;
  memset(out, 0, sizeof *out);
  for (int i = 0; i < fo->count; i++) {
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

  for (int k = 0; k < p->format.wire_order.count; k++)
    if (p->format.wire_order.tok[k].kind == ECM_TOK_HEADER) {
      const ecm_header_t *h = find_header(&p->format, p->format.wire_order.tok[k].id);
      out->wire_len += h ? (size_t)h->len : 0;
    }
  return 0;
}
