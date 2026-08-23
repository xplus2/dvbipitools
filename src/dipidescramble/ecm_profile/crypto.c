/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/secure_zero.h"
#include "priv.h"

static int locate_wire_components(const ecm_profile_t *p, const ecm_layout_t *lay, const unsigned char *wire, size_t wire_len, const unsigned char **iv, const unsigned char **ct, const unsigned char **gcm_tag, const unsigned char **itag) {
  size_t off = 0;
  *iv = NULL; *ct = NULL; *gcm_tag = NULL; *itag = NULL;
  for (int i = 0; i < p->format.wire_order.count; i++) {
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

/* cw_groups tokens for single combo, advances *off. cp_number falls back to cp_number_outer + combo index (mod 65536) unless carried */
static void extract_cw_group_combo(const ecm_token_list_t *cwg, const unsigned char *plaintext, size_t *off, int cw_len,
                                   ecm_cw_combo_t *combo, unsigned cp_number_fallback) {
  int has_cpn = 0;
  for (int j = 0; j < cwg->count; j++) {
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
  int is_gcm, is_cbc;
  size_t klen, block, assoc_len = 0, off;
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

  if (p->integrity.bind_ecm_id) { unsigned ecm_id = p->ecm_id_set ? p->ecm_id : ecm_id_fallback; assoc[assoc_len++] = (unsigned char)(ecm_id >> 8); assoc[assoc_len++] = (unsigned char)ecm_id; }
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
  for (int i = 0; i < p->format.field_order.count; i++) {
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
      for (int c = 0; c < p->cw_count; c++)
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
