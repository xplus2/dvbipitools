/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_fixture.h"

#include <stddef.h>

#include <openssl/hmac.h>

#include "../log_capture.h"
#include "dipidescramble/ecm_profile.h"

START_TEST(parse_sets_defaults) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-ecb", &p), 0);
  ck_assert_int_eq(p.set, 1);
  ck_assert_int_eq(p.cipher, ECM_CIPHER_AES256_ECB);
  ck_assert_int_eq(p.iv_source, ECM_IV_NONE);
  ck_assert_int_eq(p.padding, ECM_PAD_NONE);
  ck_assert_int_eq(p.key_derivation.hkdf, 1);
  ck_assert_str_eq(p.key_derivation.enc_info, "dipidescramble-ecm-enc");
  ck_assert_str_eq(p.key_derivation.mac_info, "dipidescramble-ecm-mac");
  ck_assert_int_eq(p.integrity.bind_ecm_id, 1);
  ck_assert_int_eq(p.integrity.bind_cp_number, 1);
  ck_assert_int_eq(p.integrity.crc32_variant, ECM_CRC32_IEEE);

  ck_assert_int_eq(ecm_profile_validate(&p), 0);
  ck_assert_int_eq(p.format.field_order.count, 1);
  ck_assert_int_eq(p.format.field_order.tok[0].kind, ECM_TOK_CW);
}
END_TEST

START_TEST(parse_rejects_unknown_field) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("bogus=1", &p), -1);
}
END_TEST

START_TEST(parse_rejects_malformed_pair) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cipher", &p), -1);
}
END_TEST

START_TEST(parse_rejects_bad_enum_value) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cipher=aes999-ecb", &p), -1);
}
END_TEST

START_TEST(parse_default_field_order_with_header_and_flags) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("header=h1:8000,include_cp_number=1,include_ecm_id=1", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);
  ck_assert_int_eq(p.format.field_order.count, 4);
  ck_assert_int_eq(p.format.field_order.tok[0].kind, ECM_TOK_HEADER);
  ck_assert_str_eq(p.format.field_order.tok[0].id, "h1");
  ck_assert_int_eq(p.format.field_order.tok[1].kind, ECM_TOK_ECM_ID);
  ck_assert_int_eq(p.format.field_order.tok[2].kind, ECM_TOK_CP_NUMBER);
  ck_assert_int_eq(p.format.field_order.tok[3].kind, ECM_TOK_CW);
}
END_TEST

START_TEST(validate_rejects_ecb_with_iv) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-ecb,iv=zero", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
}
END_TEST

START_TEST(validate_rejects_cbc_without_iv) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cipher=aes128-cbc", &p), 0); /* iv defaults to none */
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
}
END_TEST

START_TEST(validate_rejects_gcm_with_padding) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-gcm,iv=random,padding=pkcs7", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
}
END_TEST

START_TEST(validate_accepts_gcm_with_nonrandom_iv) {
  /* receiver, not generator: no allow_insecure gate here, we decode whatever profile we're told */
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-gcm,iv=zero", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);
}
END_TEST

START_TEST(validate_rejects_truncate_tag_with_crc32) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("integrity=crc32,truncate_tag=8", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
}
END_TEST

START_TEST(validate_rejects_short_key_info_with_truncate_source) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("short_key_info=custom", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
}
END_TEST

START_TEST(validate_rejects_mte_without_explicit_field_order) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("integrity=hmac-sha256,integrity_order=before-encrypt", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
}
END_TEST

START_TEST(validate_rejects_cw_count_without_explicit_field_order) {
  ecm_profile_t p;
  ck_assert_int_eq(ecm_profile_parse("cw_count=2,cw_group=cp_number+cw", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
}
END_TEST

/* independent reflected CRC-32, IEEE polynomial: same public algorithm crypto_crc32() implements,
   built here from scratch rather than calling it, keeping round-trip test from just checking itself */
static uint32_t ref_crc32_ieee(const unsigned char *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  int b;
  for (i = 0; i < len; i++) {
    crc ^= data[i];
    for (b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return crc ^ 0xFFFFFFFFu;
}

START_TEST(roundtrip_ecb_legacy_equivalent) {
  ecm_profile_t p;
  unsigned char sk[CRYPTO_KEY_LEN], enc_key[32];
  unsigned char cw[16], wire[16];
  unsigned char ct[16];
  ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX];
  int combo_count = 0, i, len = 0;
  EVP_CIPHER_CTX *ctx;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) sk[i] = (unsigned char)(i * 3 + 1);
  for (i = 0; i < 16; i++) cw[i] = (unsigned char)(0xA0 + i);

  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-ecb", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);

  ref_hkdf(sk, "dipidescramble-ecm-enc", enc_key);
  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, enc_key, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, cw, 16), 1);
  EVP_CIPHER_CTX_free(ctx);
  memcpy(wire, ct, 16);

  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, 16, 0x0007, 0x0020, combos, &combo_count), 0);
  ck_assert_int_eq(combo_count, 1);
  ck_assert_mem_eq(combos[0].cw, cw, 16);
}
END_TEST

START_TEST(roundtrip_cbc_pkcs7_hmac_truncated) {
  ecm_profile_t p;
  unsigned char sk[CRYPTO_KEY_LEN], enc_key[32], mac_key[32];
  unsigned char cw[16];
  unsigned char plaintext[22]; /* h1(2) + ecm_id(2) + cp_number(2) + cw(16) */
  unsigned char padded[32];    /* pkcs7 to next 16B boundary */
  unsigned char ct[32], iv[16];
  unsigned char assoc[4], full_tag[32];
  unsigned char wire[40]; /* ciphertext(32) + tag(8) */
  ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX];
  int combo_count = 0, i, len = 0, finlen = 0;
  unsigned ecm_id = 0x0020, cp_number = 0x0007;
  EVP_CIPHER_CTX *ctx;
  unsigned int hlen;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) sk[i] = (unsigned char)(i * 7 + 3);
  for (i = 0; i < 16; i++) cw[i] = (unsigned char)(0x30 + i);

  ck_assert_int_eq(ecm_profile_parse(
      "cipher=aes128-cbc,iv=cp_number,padding=pkcs7,header=h1:8000,include_cp_number=1,"
      "include_ecm_id=1,integrity=hmac-sha256,truncate_tag=8",
      &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);

  plaintext[0] = 0x80; plaintext[1] = 0x00;
  plaintext[2] = (unsigned char)(ecm_id >> 8); plaintext[3] = (unsigned char)ecm_id;
  plaintext[4] = (unsigned char)(cp_number >> 8); plaintext[5] = (unsigned char)cp_number;
  memcpy(plaintext + 6, cw, 16);

  memcpy(padded, plaintext, sizeof plaintext);
  memset(padded + sizeof plaintext, (int)(sizeof padded - sizeof plaintext), sizeof padded - sizeof plaintext);

  ref_hkdf(sk, "dipidescramble-ecm-enc", enc_key);
  ref_hkdf(sk, "dipidescramble-ecm-mac", mac_key);

  memset(iv, 0, sizeof iv);
  iv[14] = (unsigned char)(cp_number >> 8);
  iv[15] = (unsigned char)cp_number;

  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, enc_key, iv), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, padded, sizeof padded), 1);
  ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &finlen), 1);
  EVP_CIPHER_CTX_free(ctx);
  ck_assert_int_eq(len + finlen, 32);

  assoc[0] = (unsigned char)(ecm_id >> 8); assoc[1] = (unsigned char)ecm_id;
  assoc[2] = (unsigned char)(cp_number >> 8); assoc[3] = (unsigned char)cp_number;
  {
    unsigned char buf[36];
    memcpy(buf, ct, 32);
    memcpy(buf + 32, assoc, 4);
    ck_assert_ptr_nonnull(HMAC(EVP_sha256(), mac_key, 32, buf, sizeof buf, full_tag, &hlen));
  }

  memcpy(wire, ct, 32);
  memcpy(wire + 32, full_tag, 8); /* truncate_tag=8, truncate_from=left (default) */

  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, sizeof wire, cp_number, ecm_id, combos, &combo_count), 0);
  ck_assert_int_eq(combo_count, 1);
  ck_assert_mem_eq(combos[0].cw, cw, 16);
  ck_assert_uint_eq(combos[0].cp_number, cp_number);

  wire[32] ^= 0xFF; /* corrupt tag */
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, sizeof wire, cp_number, ecm_id, combos, &combo_count), -1);
}
END_TEST

START_TEST(roundtrip_gcm_random_iv_no_binds) {
  ecm_profile_t p;
  unsigned char sk[CRYPTO_KEY_LEN], enc_key[32];
  unsigned char cw[8], nonce[12], ct[8], tag[16];
  unsigned char wire[12 + 8 + 16];
  ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX];
  int combo_count = 0, i, len = 0, finlen = 0;
  EVP_CIPHER_CTX *ctx;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) sk[i] = (unsigned char)(200 - i);
  for (i = 0; i < 8; i++) cw[i] = (unsigned char)(0x50 + i);
  for (i = 0; i < 12; i++) nonce[i] = (unsigned char)(0x11 * (i + 1));

  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-gcm,iv=random,bind_ecm_id=0,bind_cp_number=0", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);

  ref_hkdf(sk, "dipidescramble-ecm-enc", enc_key);

  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL), 1);
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, NULL, NULL, enc_key, nonce), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, cw, 8), 1);
  ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &finlen), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag), 1);
  EVP_CIPHER_CTX_free(ctx);

  memcpy(wire, nonce, 12);
  memcpy(wire + 12, ct, 8);
  memcpy(wire + 20, tag, 16);

  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 8, sk, wire, sizeof wire, 0x0001, 0x0020, combos, &combo_count), 0);
  ck_assert_int_eq(combo_count, 1);
  ck_assert_mem_eq(combos[0].cw, cw, 8);
}
END_TEST

START_TEST(roundtrip_ecb_crc32) {
  ecm_profile_t p;
  unsigned char sk[CRYPTO_KEY_LEN], enc_key[32];
  unsigned char cw[16], ct[16];
  unsigned char assoc[4], wire[20];
  ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX];
  int combo_count = 0, i, len = 0;
  unsigned ecm_id = 0x0020, cp_number = 0x0009;
  uint32_t crc;
  EVP_CIPHER_CTX *ctx;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) sk[i] = (unsigned char)(i + 5);
  for (i = 0; i < 16; i++) cw[i] = (unsigned char)(0x60 + i);

  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-ecb,integrity=crc32", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);

  ref_hkdf(sk, "dipidescramble-ecm-enc", enc_key);
  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, enc_key, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, cw, 16), 1);
  EVP_CIPHER_CTX_free(ctx);

  assoc[0] = (unsigned char)(ecm_id >> 8); assoc[1] = (unsigned char)ecm_id;
  assoc[2] = (unsigned char)(cp_number >> 8); assoc[3] = (unsigned char)cp_number;
  {
    unsigned char buf[20];
    memcpy(buf, ct, 16);
    memcpy(buf + 16, assoc, 4);
    crc = ref_crc32_ieee(buf, sizeof buf);
  }
  memcpy(wire, ct, 16);
  wire[16] = (unsigned char)(crc >> 24); wire[17] = (unsigned char)(crc >> 16);
  wire[18] = (unsigned char)(crc >> 8); wire[19] = (unsigned char)crc;

  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, sizeof wire, cp_number, ecm_id, combos, &combo_count), 0);
  ck_assert_int_eq(combo_count, 1);
  ck_assert_mem_eq(combos[0].cw, cw, 16);
}
END_TEST

typedef struct {
  unsigned cp;
  unsigned char cw[16];
} ref_combo_t;

static void prof(ecm_profile_t *p, const char *spec) {
  ck_assert_int_eq(ecm_profile_parse(spec, p), 0);
  ck_assert_int_eq(ecm_profile_validate(p), 0);
}

static const EVP_CIPHER *ref_cipher(ecm_cipher_t c) {
  switch (c) {
    case ECM_CIPHER_AES128_ECB: return EVP_aes_128_ecb();
    case ECM_CIPHER_AES256_ECB: return EVP_aes_256_ecb();
    case ECM_CIPHER_AES128_CBC: return EVP_aes_128_cbc();
    case ECM_CIPHER_AES256_CBC: return EVP_aes_256_cbc();
    case ECM_CIPHER_AES128_GCM: return EVP_aes_128_gcm();
    case ECM_CIPHER_AES256_GCM: return EVP_aes_256_gcm();
    default: break;
  }
  ck_abort_msg("reference encoder: unsupported cipher");
  return NULL;
}

static uint32_t ref_crc(int castagnoli, const unsigned char *data, size_t len) {
  uint32_t poly = castagnoli ? 0x82F63B78u : 0xEDB88320u;
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  int b;

  for (i = 0; i < len; i++) {
    crc ^= data[i];
    for (b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (poly & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return crc ^ 0xFFFFFFFFu;
}

static size_t ref_tag(const ecm_profile_t *p, const unsigned char mac_key[32], const unsigned char *data, size_t dlen, const unsigned char *assoc, size_t alen, unsigned char *out) {
  unsigned char buf[600];
  unsigned char full[32];
  unsigned int hl;
  size_t want;

  memcpy(buf, data, dlen);
  memcpy(buf + dlen, assoc, alen);
  if (p->integrity.type == ECM_INTEGRITY_CRC32) {
    uint32_t v = ref_crc(p->integrity.crc32_variant == ECM_CRC32_CASTAGNOLI, buf, dlen + alen);

    if (p->integrity.crc32_endian == ECM_CRC32_BIG) {
      out[0] = (unsigned char)(v >> 24);
      out[1] = (unsigned char)(v >> 16);
      out[2] = (unsigned char)(v >> 8);
      out[3] = (unsigned char)v;
    } else {
      out[3] = (unsigned char)(v >> 24);
      out[2] = (unsigned char)(v >> 16);
      out[1] = (unsigned char)(v >> 8);
      out[0] = (unsigned char)v;
    }
    return 4;
  }
  ck_assert_ptr_nonnull(HMAC(EVP_sha256(), mac_key, 32, buf, dlen + alen, full, &hl));
  want = p->integrity.truncate_tag ? (size_t)p->integrity.truncate_tag : 32u;
  if (p->integrity.truncate_from == ECM_TRUNCATE_RIGHT)
    memcpy(out, full + (32 - want), want);
  else
    memcpy(out, full, want);
  return want;
}

static const ecm_header_t *ref_header(const ecm_profile_t *p, const char *id) {
  int i;

  for (i = 0; i < p->format.header_count; i++)
    if (strcmp(p->format.headers[i].id, id) == 0)
      return &p->format.headers[i];
  ck_abort_msg("reference encoder: unknown header");
  return NULL;
}

static size_t ref_encode(const ecm_profile_t *p, int cw_len, const unsigned char sk[32], unsigned cp_outer, unsigned ecm_id, const ref_combo_t *combos, const unsigned char *rand_iv, unsigned char *wire) {
  unsigned char enc_full[32];
  unsigned char mac_key[32];
  unsigned char enc_key[32];
  unsigned char pt[512];
  unsigned char ct[512];
  unsigned char iv[16];
  unsigned char assoc[4];
  unsigned char itag[32];
  unsigned char gtag[16];
  size_t klen = (p->cipher == ECM_CIPHER_AES128_ECB || p->cipher == ECM_CIPHER_AES128_CBC || p->cipher == ECM_CIPHER_AES128_GCM) ? 16 : 32;
  int gcm = p->cipher == ECM_CIPHER_AES128_GCM || p->cipher == ECM_CIPHER_AES256_GCM;
  int cbc = p->cipher == ECM_CIPHER_AES128_CBC || p->cipher == ECM_CIPHER_AES256_CBC;
  size_t ivlen = gcm ? 12 : 16;
  size_t n = 0;
  size_t alen = 0;
  size_t ctlen;
  size_t itag_len = 0;
  size_t off = 0;
  unsigned used_id = p->ecm_id_set ? p->ecm_id : ecm_id;
  int len = 0;
  int fin = 0;
  int i;
  int c;
  int t;
  EVP_CIPHER_CTX *ctx;

  if (p->key_derivation.hkdf) {
    ref_hkdf(sk, p->key_derivation.enc_info, enc_full);
    ref_hkdf(sk, p->key_derivation.mac_info, mac_key);
  } else {
    memcpy(enc_full, sk, 32);
    memcpy(mac_key, sk, 32);
  }
  if (klen < 32 && p->key_derivation.short_key_source == ECM_SHORT_KEY_SEPARATE_INFO) {
    unsigned char sep[32];

    ref_hkdf(sk, p->key_derivation.short_key_info, sep);
    memcpy(enc_key, sep, klen);
  } else {
    memcpy(enc_key, enc_full, klen);
  }
  if (p->integrity.bind_ecm_id) {
    assoc[alen++] = (unsigned char)(used_id >> 8);
    assoc[alen++] = (unsigned char)used_id;
  }
  if (p->integrity.bind_cp_number) {
    assoc[alen++] = (unsigned char)(cp_outer >> 8);
    assoc[alen++] = (unsigned char)cp_outer;
  }

  for (i = 0; i < p->format.field_order.count; i++) {
    const ecm_token_t *tk = &p->format.field_order.tok[i];

    switch (tk->kind) {
      case ECM_TOK_HEADER: {
        const ecm_header_t *h = ref_header(p, tk->id);

        memcpy(pt + n, h->data, (size_t)h->len);
        n += (size_t)h->len;
        break;
      }
      case ECM_TOK_ECM_ID:
        pt[n++] = (unsigned char)(used_id >> 8);
        pt[n++] = (unsigned char)used_id;
        break;
      case ECM_TOK_CP_NUMBER:
        pt[n++] = (unsigned char)(combos[0].cp >> 8);
        pt[n++] = (unsigned char)combos[0].cp;
        break;
      case ECM_TOK_CW:
        memcpy(pt + n, combos[0].cw, (size_t)cw_len);
        n += (size_t)cw_len;
        break;
      case ECM_TOK_CW_GROUP:
        for (c = 0; c < p->cw_count; c++) {
          for (t = 0; t < p->format.cw_group.count; t++) {
            if (p->format.cw_group.tok[t].kind == ECM_TOK_CP_NUMBER) {
              pt[n++] = (unsigned char)(combos[c].cp >> 8);
              pt[n++] = (unsigned char)combos[c].cp;
            } else {
              memcpy(pt + n, combos[c].cw, (size_t)cw_len);
              n += (size_t)cw_len;
            }
          }
        }
        break;
      default:
        break;
    }
  }
  if (p->integrity.type != ECM_INTEGRITY_NONE && p->integrity.order == ECM_INTEGRITY_BEFORE_ENCRYPT) {
    itag_len = ref_tag(p, mac_key, pt, n, assoc, alen, itag);
    memcpy(pt + n, itag, itag_len);
    n += itag_len;
  }
  if (!gcm) {
    size_t pad;

    if (p->padding == ECM_PAD_PKCS7) {
      pad = 16 - n % 16;
      memset(pt + n, (int)pad, pad);
      n += pad;
    } else if (p->padding == ECM_PAD_ZERO) {
      pad = (16 - n % 16) % 16;
      if (n == 0)
        pad = 16;
      memset(pt + n, 0, pad);
      n += pad;
    }
    ck_assert_uint_eq(n % 16, 0u);
  }

  memset(iv, 0, sizeof iv);
  if (p->iv_source == ECM_IV_RANDOM) {
    memcpy(iv, rand_iv, ivlen);
  } else if (p->iv_source == ECM_IV_CP_NUMBER) {
    size_t hi = p->cp_number_layout == ECM_CP_LAYOUT_BACK ? ivlen - 2 : 0;

    iv[hi] = (unsigned char)(cp_outer >> 8);
    iv[hi + 1] = (unsigned char)cp_outer;
  }

  ctx = EVP_CIPHER_CTX_new();
  if (gcm) {
    ck_assert_int_eq(EVP_EncryptInit_ex(ctx, ref_cipher(p->cipher), NULL, NULL, NULL), 1);
    ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL), 1);
    ck_assert_int_eq(EVP_EncryptInit_ex(ctx, NULL, NULL, enc_key, iv), 1);
    if (alen)
      ck_assert_int_eq(EVP_EncryptUpdate(ctx, NULL, &len, assoc, (int)alen), 1);
    ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, pt, (int)n), 1);
    ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &fin), 1);
    ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, gtag), 1);
  } else {
    ck_assert_int_eq(EVP_EncryptInit_ex(ctx, ref_cipher(p->cipher), NULL, enc_key, cbc ? iv : NULL), 1);
    ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
    ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, pt, (int)n), 1);
    ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &fin), 1);
  }
  EVP_CIPHER_CTX_free(ctx);
  ctlen = (size_t)(len + fin);
  if (p->integrity.type != ECM_INTEGRITY_NONE && p->integrity.order == ECM_INTEGRITY_AFTER_ENCRYPT)
    itag_len = ref_tag(p, mac_key, ct, ctlen, assoc, alen, itag);
  else if (p->integrity.order == ECM_INTEGRITY_BEFORE_ENCRYPT)
    itag_len = 0;

  for (i = 0; i < p->format.wire_order.count; i++) {
    const ecm_token_t *tk = &p->format.wire_order.tok[i];

    switch (tk->kind) {
      case ECM_TOK_IV:
        memcpy(wire + off, iv, ivlen);
        off += ivlen;
        break;
      case ECM_TOK_CIPHERTEXT:
        memcpy(wire + off, ct, ctlen);
        off += ctlen;
        break;
      case ECM_TOK_GCM_TAG:
        memcpy(wire + off, gtag, 16);
        off += 16;
        break;
      case ECM_TOK_INTEGRITY_TAG:
        memcpy(wire + off, itag, itag_len);
        off += itag_len;
        break;
      case ECM_TOK_HEADER: {
        const ecm_header_t *h = ref_header(p, tk->id);

        memcpy(wire + off, h->data, (size_t)h->len);
        off += (size_t)h->len;
        break;
      }
      default:
        break;
    }
  }
  return off;
}

static void ref_sk(unsigned char sk[32]) {
  int i;

  for (i = 0; i < 32; i++)
    sk[i] = (unsigned char)(0x40 + i * 5);
}

static void ref_cw(ref_combo_t *c, unsigned cp, unsigned char base) {
  int i;

  c->cp = cp;
  for (i = 0; i < 16; i++)
    c->cw[i] = (unsigned char)(base + i);
}

typedef struct {
  const char *spec;
  int cw_len;
} rt_case_t;

START_TEST(reference_round_trip_matches_for_many_profile_shapes) {
  static const rt_case_t cases[] = {
    {"cipher=aes256-ecb", 16},
    {"cipher=aes256-ecb,padding=zero", 8},
    {"cipher=aes128-ecb,padding=zero", 8},
    {"cipher=aes128-ecb,hkdf=0", 16},
    {"cipher=aes128-ecb,short_key_source=separate_info", 16},
    {"cipher=aes128-ecb,short_key_source=separate_info,short_key_info=other", 16},
    {"cipher=aes256-cbc,iv=zero", 16},
    {"cipher=aes256-cbc,iv=cp_number,cp_number_layout=front", 16},
    {"cipher=aes256-cbc,iv=cp_number,cp_number_layout=back", 16},
    {"cipher=aes128-cbc,iv=random,padding=pkcs7", 8},
    {"cipher=aes256-gcm,iv=random", 16},
    {"cipher=aes128-gcm,iv=zero,bind_ecm_id=0", 8},
    {"cipher=aes256-gcm,iv=cp_number,include_cp_number=1", 16},
    {"cipher=aes256-ecb,integrity=crc32", 16},
    {"cipher=aes256-ecb,integrity=crc32,crc32_variant=castagnoli,crc32_endian=little", 16},
    {"cipher=aes256-ecb,integrity=hmac-sha256", 16},
    {"cipher=aes256-ecb,integrity=hmac-sha256,truncate_tag=4,truncate_from=right", 16},
    {"cipher=aes256-ecb,integrity=hmac-sha256,truncate_tag=8,truncate_from=left", 16},
    {"cipher=aes256-ecb,integrity=hmac-sha256,bind_ecm_id=0,bind_cp_number=0", 16},
    {"cipher=aes256-cbc,iv=random,integrity=hmac-sha256,integrity_order=before-encrypt,field_order=cw+integrity_tag,padding=pkcs7", 16},
    {"cipher=aes256-ecb,integrity=crc32,integrity_order=before-encrypt,field_order=cp_number+cw+integrity_tag,include_cp_number=1,padding=zero", 16},
    {"cipher=aes256-ecb,header=h1:8000,include_cp_number=1,include_ecm_id=1,padding=zero", 16},
    {"cipher=aes256-cbc,iv=random,header=m:C0DE,wire_order=m+iv+ciphertext,padding=pkcs7", 16},
    {"cipher=aes256-ecb,ecm_id=0x1234,include_ecm_id=1,integrity=crc32,padding=zero", 16},
  };
  const rt_case_t *tc = &cases[_i];
  ecm_profile_t p;
  unsigned char sk[32];
  unsigned char wire[600];
  unsigned char rand_iv[16];
  ref_combo_t in;
  ecm_cw_combo_t out[ECM_PROFILE_CW_MAX];
  int count = 0;
  size_t wl;
  int i;

  for (i = 0; i < 16; i++)
    rand_iv[i] = (unsigned char)(0xC0 + i);
  ref_sk(sk);
  ref_cw(&in, 0x0123, 0x70);
  prof(&p, tc->spec);
  wl = ref_encode(&p, tc->cw_len, sk, 0x0123, 0x0042, &in, rand_iv, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, tc->cw_len, sk, wire, wl, 0x0123, 0x0042, out, &count), 0);
  ck_assert_int_eq(count, 1);
  ck_assert_mem_eq(out[0].cw, in.cw, (size_t)tc->cw_len);
}
END_TEST

START_TEST(cw_groups_decode_every_combo_with_wire_or_derived_cp_numbers) {
  ecm_profile_t p;
  unsigned char sk[32];
  unsigned char wire[600];
  ref_combo_t in[3];
  ecm_cw_combo_t out[ECM_PROFILE_CW_MAX];
  int count = 0;
  size_t wl;

  ref_sk(sk);
  ref_cw(&in[0], 0x0010, 0x10);
  ref_cw(&in[1], 0x0011, 0x20);
  ref_cw(&in[2], 0x0012, 0x30);

  prof(&p, "cipher=aes256-ecb,cw_count=2,cw_group=cw,field_order=cw_group");
  wl = ref_encode(&p, 16, sk, 0x00A0, 0x0042, in, NULL, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x00A0, 0x0042, out, &count), 0);
  ck_assert_int_eq(count, 2);
  ck_assert_mem_eq(out[0].cw, in[0].cw, 16);
  ck_assert_mem_eq(out[1].cw, in[1].cw, 16);
  ck_assert_uint_eq(out[0].cp_number, 0x00A0u);
  ck_assert_uint_eq(out[1].cp_number, 0x00A1u);

  prof(&p, "cipher=aes256-ecb,cw_count=2,cw_group=cp_number+cw,field_order=cw_group,padding=zero");
  wl = ref_encode(&p, 16, sk, 0x00A0, 0x0042, in, NULL, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x00A0, 0x0042, out, &count), 0);
  ck_assert_int_eq(count, 2);
  ck_assert_uint_eq(out[0].cp_number, 0x0010u);
  ck_assert_uint_eq(out[1].cp_number, 0x0011u);
  ck_assert_mem_eq(out[1].cw, in[1].cw, 16);

  prof(&p, "cipher=aes256-cbc,iv=random,padding=pkcs7,cw_count=3,cw_group=cw+cp_number,field_order=header_marker+cw_group,header=header_marker:A5");
  {
    unsigned char iv[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    wl = ref_encode(&p, 8, sk, 0x00A0, 0x0042, in, iv, wire);
    ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 8, sk, wire, wl, 0x00A0, 0x0042, out, &count), 0);
  }
  ck_assert_int_eq(count, 3);
  ck_assert_mem_eq(out[2].cw, in[2].cw, 8);
  ck_assert_uint_eq(out[2].cp_number, 0x0012u);

  prof(&p, "cipher=aes256-gcm,iv=random,cw_count=2,cw_group=cp_number+cw,field_order=ecm_id+cw_group,include_ecm_id=1");
  {
    unsigned char iv[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2};

    wl = ref_encode(&p, 16, sk, 0x00A0, 0x0042, in, iv, wire);
    ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x00A0, 0x0042, out, &count), 0);
  }
  ck_assert_int_eq(count, 2);
  ck_assert_mem_eq(out[0].cw, in[0].cw, 16);
}
END_TEST

typedef enum { TAMPER_CT, TAMPER_TAIL, TAMPER_LEN_PLUS, TAMPER_LEN_MINUS, TAMPER_HEADER } tamper_t;

typedef struct {
  const char *spec;
  tamper_t how;
} tamper_case_t;

START_TEST(tampered_wire_is_rejected_per_integrity_mode) {
  static const tamper_case_t cases[] = {
    {"cipher=aes256-ecb,integrity=crc32", TAMPER_CT},
    {"cipher=aes256-ecb,integrity=crc32", TAMPER_TAIL},
    {"cipher=aes256-ecb,integrity=hmac-sha256", TAMPER_CT},
    {"cipher=aes256-ecb,integrity=hmac-sha256", TAMPER_TAIL},
    {"cipher=aes256-ecb,integrity=hmac-sha256,truncate_tag=4,truncate_from=right", TAMPER_TAIL},
    {"cipher=aes256-ecb,integrity=hmac-sha256,truncate_tag=8", TAMPER_TAIL},
    {"cipher=aes256-ecb,integrity=crc32,integrity_order=before-encrypt,field_order=cw+integrity_tag,padding=zero", TAMPER_CT},
    {"cipher=aes256-ecb,integrity=hmac-sha256,integrity_order=before-encrypt,field_order=cw+integrity_tag,padding=zero", TAMPER_CT},
    {"cipher=aes256-gcm,iv=random", TAMPER_CT},
    {"cipher=aes256-gcm,iv=random", TAMPER_TAIL},
    {"cipher=aes256-ecb,header=m:C0DE,wire_order=m+ciphertext,padding=zero", TAMPER_HEADER},
    {"cipher=aes256-ecb", TAMPER_LEN_PLUS},
    {"cipher=aes256-ecb", TAMPER_LEN_MINUS},
    {"cipher=aes256-ecb,integrity=crc32", TAMPER_LEN_MINUS},
  };
  const tamper_case_t *tc = &cases[_i];
  ecm_profile_t p;
  unsigned char sk[32];
  unsigned char wire[600];
  unsigned char iv[16] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3};
  ref_combo_t in;
  ecm_cw_combo_t out[ECM_PROFILE_CW_MAX];
  int count = 0;
  size_t wl;

  ref_sk(sk);
  ref_cw(&in, 0x0123, 0x70);
  prof(&p, tc->spec);
  wl = ref_encode(&p, 16, sk, 0x0123, 0x0042, &in, iv, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0123, 0x0042, out, &count), 0);
  switch (tc->how) {
    case TAMPER_CT: wire[p.format.header_count ? 2 : 0] ^= 0x01; break;
    case TAMPER_TAIL: wire[wl - 1] ^= 0x01; break;
    case TAMPER_HEADER: wire[0] ^= 0x01; break;
    case TAMPER_LEN_PLUS: wire[wl] = 0; wl++; break;
    case TAMPER_LEN_MINUS: wl--; break;
  }
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0123, 0x0042, out, &count), -1);
}
END_TEST

START_TEST(bindings_reject_foreign_ecm_id_and_cp_number_unless_disabled) {
  ecm_profile_t p;
  unsigned char sk[32];
  unsigned char wire[600];
  ref_combo_t in;
  ecm_cw_combo_t out[ECM_PROFILE_CW_MAX];
  int count = 0;
  size_t wl;

  ref_sk(sk);
  ref_cw(&in, 0x0123, 0x70);

  prof(&p, "cipher=aes256-ecb,integrity=hmac-sha256");
  wl = ref_encode(&p, 16, sk, 0x0123, 0x0042, &in, NULL, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0123, 0x0043, out, &count), -1);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0124, 0x0042, out, &count), -1);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0123, 0x0042, out, &count), 0);

  prof(&p, "cipher=aes256-ecb,integrity=hmac-sha256,bind_ecm_id=0");
  wl = ref_encode(&p, 16, sk, 0x0123, 0x0042, &in, NULL, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0123, 0x0099, out, &count), 0);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0124, 0x0099, out, &count), -1);

  prof(&p, "cipher=aes256-ecb,integrity=hmac-sha256,bind_cp_number=0");
  wl = ref_encode(&p, 16, sk, 0x0123, 0x0042, &in, NULL, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0777, 0x0042, out, &count), 0);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0777, 0x0043, out, &count), -1);

  prof(&p, "cipher=aes256-gcm,iv=random,ecm_id=0x0042");
  wl = ref_encode(&p, 16, sk, 0x0123, 0x0001, &in, (const unsigned char *)"0123456789ab", wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0123, 0x7777, out, &count), 0);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, sk, wire, wl, 0x0124, 0x7777, out, &count), -1);
}
END_TEST

START_TEST(wrong_key_or_wrong_cw_len_is_rejected) {
  ecm_profile_t p;
  unsigned char sk[32];
  unsigned char other[32];
  unsigned char wire[600];
  ref_combo_t in;
  ecm_cw_combo_t out[ECM_PROFILE_CW_MAX];
  int count = 0;
  size_t wl;

  ref_sk(sk);
  memset(other, 0x99, sizeof other);
  ref_cw(&in, 0x0123, 0x70);
  prof(&p, "cipher=aes256-ecb,integrity=hmac-sha256");
  wl = ref_encode(&p, 16, sk, 0x0123, 0x0042, &in, NULL, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, other, wire, wl, 0x0123, 0x0042, out, &count), -1);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 8, sk, wire, wl, 0x0123, 0x0042, out, &count), -1);
  prof(&p, "cipher=aes256-ecb,integrity=crc32");
  wl = ref_encode(&p, 16, sk, 0x0123, 0x0042, &in, NULL, wire);
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 16, other, wire, wl, 0x0123, 0x0042, out, &count), 0);
  ck_assert_mem_ne(out[0].cw, in.cw, 16);
  prof(&p, "cipher=aes256-ecb,padding=none,field_order=cw");
  ck_assert_int_eq(ecm_profile_decrypt_cw(&p, 8, sk, wire, 8, 0x0123, 0x0042, out, &count), -1);
}
END_TEST

typedef enum { PK_INT, PK_STR } pk_kind_t;

typedef struct {
  const char *spec;
  pk_kind_t kind;
  size_t off;
  int num;
  const char *str;
} setter_case_t;

#define SC(spec, kind, field, num, str) {spec, kind, offsetof(ecm_profile_t, field), num, str}

static const setter_case_t setter_cases[] = {
  SC("hkdf=0", PK_INT, key_derivation.hkdf, 0, NULL),
  SC("hkdf=1", PK_INT, key_derivation.hkdf, 1, NULL),
  SC("enc_info=my-enc", PK_STR, key_derivation.enc_info, 0, "my-enc"),
  SC("mac_info=my-mac", PK_STR, key_derivation.mac_info, 0, "my-mac"),
  SC("short_key_source=separate_info", PK_INT, key_derivation.short_key_source, ECM_SHORT_KEY_SEPARATE_INFO, NULL),
  SC("short_key_source=truncate", PK_INT, key_derivation.short_key_source, ECM_SHORT_KEY_TRUNCATE, NULL),
  SC("short_key_info=sk-info", PK_STR, key_derivation.short_key_info, 0, "sk-info"),
  SC("short_key_info=sk-info", PK_INT, key_derivation.short_key_info_set, 1, NULL),
  SC("cp_number_layout=front", PK_INT, cp_number_layout, ECM_CP_LAYOUT_FRONT, NULL),
  SC("cp_number_layout=back", PK_INT, cp_number_layout, ECM_CP_LAYOUT_BACK, NULL),
  SC("truncate_from=right", PK_INT, integrity.truncate_from, ECM_TRUNCATE_RIGHT, NULL),
  SC("truncate_from=left", PK_INT, integrity.truncate_from, ECM_TRUNCATE_LEFT, NULL),
  SC("crc32_variant=castagnoli", PK_INT, integrity.crc32_variant, ECM_CRC32_CASTAGNOLI, NULL),
  SC("crc32_variant=ieee", PK_INT, integrity.crc32_variant, ECM_CRC32_IEEE, NULL),
  SC("crc32_endian=little", PK_INT, integrity.crc32_endian, ECM_CRC32_LITTLE, NULL),
  SC("crc32_endian=big", PK_INT, integrity.crc32_endian, ECM_CRC32_BIG, NULL),
  SC("ecm_id=0x1F4", PK_INT, ecm_id, 500, NULL),
  SC("ecm_id=300", PK_INT, ecm_id, 300, NULL),
  SC("ecm_id=300", PK_INT, ecm_id_set, 1, NULL),
  SC("ecm_id=0xFFFF", PK_INT, ecm_id, 0xFFFF, NULL),
  SC("truncate_tag=8", PK_INT, integrity.truncate_tag, 8, NULL),
  SC("integrity_order=before-encrypt", PK_INT, integrity.order, ECM_INTEGRITY_BEFORE_ENCRYPT, NULL),
  SC("bind_ecm_id=0", PK_INT, integrity.bind_ecm_id, 0, NULL),
  SC("bind_cp_number=0", PK_INT, integrity.bind_cp_number, 0, NULL),
  SC("include_cp_number=1", PK_INT, format.include_cp_number, 1, NULL),
  SC("include_ecm_id=1", PK_INT, format.include_ecm_id, 1, NULL),
  SC("cw_count=8", PK_INT, cw_count, 8, NULL),
  SC("cipher=des-ede3-cbc", PK_INT, cipher, ECM_CIPHER_DES_EDE3_CBC, NULL),
  SC("cipher=des-ede-ecb", PK_INT, cipher, ECM_CIPHER_DES_EDE_ECB, NULL),
  SC("iv=cp_number", PK_INT, iv_source, ECM_IV_CP_NUMBER, NULL),
  SC("padding=pkcs7", PK_INT, padding, ECM_PAD_PKCS7, NULL),
  SC("integrity=hmac-sha256", PK_INT, integrity.type, ECM_INTEGRITY_HMAC_SHA256, NULL),
};

START_TEST(parse_setters_store_their_value) {
  const setter_case_t *c = &setter_cases[_i];
  ecm_profile_t p;
  const unsigned char *base;

  ck_assert_int_eq(ecm_profile_parse(c->spec, &p), 0);
  base = (const unsigned char *)&p + c->off;
  if (c->kind == PK_INT)
    ck_assert_int_eq(*(const int *)base, c->num);
  else
    ck_assert_str_eq((const char *)base, c->str);
}
END_TEST

static const char *const bad_specs[] = {
  "hkdf=2",
  "hkdf=yes",
  "short_key_source=bogus",
  "cp_number_layout=up",
  "truncate_from=up",
  "crc32_variant=md5",
  "crc32_endian=middle",
  "ecm_id=0x10000",
  "ecm_id=abc",
  "ecm_id=12x",
  "truncate_tag=abc",
  "truncate_tag=0",
  "truncate_tag=-4",
  "cw_count=9",
  "cw_count=-1",
  "cw_count=x",
  "include_cp_number=2",
  "include_ecm_id=on",
  "bind_ecm_id=2",
  "bind_cp_number=x",
  "iv=bogus",
  "padding=bogus",
  "integrity=md5",
  "integrity_order=during-encrypt",
  "field_order=",
  "wire_order=",
  "cw_group=",
  "field_order=a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q",
  "wire_order=a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q",
  "cw_group=a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q",
  "field_order=0123456789abcdef0123456789abcdef",
  "header=nocolon",
  "header=:80",
  "header=a:zz",
  "header=a:8",
  "header=a:",
  "header=a:000102030405060708090A0B0C0D0E0F10",
  "header=0123456789abcdef:80",
  "header=a:80,header=b:80,header=c:80,header=d:80,header=e:80",
  "enc_info=0123456789012345678901234567890123456789012345678901234567890123",
  "mac_info=0123456789012345678901234567890123456789012345678901234567890123",
  "short_key_info=0123456789012345678901234567890123456789012345678901234567890123",
};

START_TEST(parse_rejects_malformed_values) {
  ecm_profile_t p;
  char log[LOG_CAPTURE_BUF];

  log_capture_begin();
  ck_assert_int_eq(ecm_profile_parse(bad_specs[_i], &p), -1);
  log_capture_end(log, sizeof log);
  ck_assert_ptr_nonnull(strstr(log, "--ecm-profile"));
}
END_TEST

START_TEST(parse_rejects_a_spec_longer_than_the_buffer) {
  ecm_profile_t p;
  static char spec[ECM_PROFILE_SPEC_MAX + 8];

  memset(spec, 'a', sizeof spec - 1);
  spec[sizeof spec - 1] = '\0';
  ck_assert_int_eq(ecm_profile_parse(spec, &p), -1);
}
END_TEST

START_TEST(parse_token_lists_and_headers_keep_order_and_ids) {
  ecm_profile_t p;

  ck_assert_int_eq(ecm_profile_parse("header=a:01,header=b:0203,field_order=a+ecm_id+cp_number+b+cw,wire_order=iv+ciphertext+gcm_tag+integrity_tag,cw_count=2,cw_group=cp_number+cw", &p), 0);
  ck_assert_int_eq(p.format.header_count, 2);
  ck_assert_int_eq(p.format.headers[0].len, 1);
  ck_assert_int_eq(p.format.headers[1].len, 2);
  ck_assert_uint_eq(p.format.headers[1].data[1], 0x03u);
  ck_assert_int_eq(p.format.field_order_set, 1);
  ck_assert_int_eq(p.format.field_order.count, 5);
  ck_assert_int_eq(p.format.field_order.tok[0].kind, ECM_TOK_HEADER);
  ck_assert_str_eq(p.format.field_order.tok[0].id, "a");
  ck_assert_int_eq(p.format.field_order.tok[1].kind, ECM_TOK_ECM_ID);
  ck_assert_int_eq(p.format.field_order.tok[2].kind, ECM_TOK_CP_NUMBER);
  ck_assert_int_eq(p.format.field_order.tok[3].kind, ECM_TOK_HEADER);
  ck_assert_int_eq(p.format.field_order.tok[4].kind, ECM_TOK_CW);
  ck_assert_int_eq(p.format.wire_order_set, 1);
  ck_assert_int_eq(p.format.wire_order.count, 4);
  ck_assert_int_eq(p.format.wire_order.tok[0].kind, ECM_TOK_IV);
  ck_assert_int_eq(p.format.wire_order.tok[3].kind, ECM_TOK_INTEGRITY_TAG);
  ck_assert_int_eq(p.format.cw_group.count, 2);
  ck_assert_int_eq(p.format.cw_group.tok[1].kind, ECM_TOK_CW);
  ck_assert_int_eq(p.cw_count, 2);
}
END_TEST

START_TEST(parse_accepts_the_longest_allowed_info_strings_and_ids) {
  ecm_profile_t p;

  ck_assert_int_eq(ecm_profile_parse("enc_info=012345678901234567890123456789012345678901234567890123456789012,header=012345678901234:80", &p), 0);
  ck_assert_uint_eq(strlen(p.key_derivation.enc_info), 63u);
  ck_assert_uint_eq(strlen(p.format.headers[0].id), 15u);
}
END_TEST

typedef struct {
  const char *spec;
  const char *expect;
} vfail_case_t;

static const vfail_case_t vfail_cases[] = {
  {"cipher=aes256-ecb,iv=zero", "iv must be none"},
  {"cipher=aes128-cbc", "iv=none only legal"},
  {"cipher=aes256-gcm,iv=random,padding=zero", "padding must be none"},
  {"integrity=hmac-sha256,truncate_tag=5", "truncate_tag must be 4 or 8"},
  {"integrity=crc32,truncate_tag=4", "only applies to integrity=hmac-sha256"},
  {"short_key_info=x", "inert with short_key_source=truncate"},
  {"short_key_source=separate_info,hkdf=0", "needs hkdf=1"},
  {"header=cw:80", "reserved token keyword"},
  {"header=iv:80", "reserved token keyword"},
  {"header=a:80,header=a:81", "duplicate header id"},
  {"integrity=hmac-sha256,integrity_order=before-encrypt", "needs an explicit field_order"},
  {"cw_count=2,cw_group=cw", "needs an explicit field_order"},
  {"header=a:80,header=b:81", "no default position"},
  {"cw_count=2,field_order=cw_group", "needs format.cw_group set"},
  {"cw_count=2,cw_group=cw+cw,field_order=cw_group", "cw appears more than once"},
  {"cw_count=2,cw_group=cp_number+cp_number+cw,field_order=cw_group", "cp_number appears more than once"},
  {"cw_count=2,cw_group=cw+ecm_id,field_order=cw_group", "may only contain cp_number and/or cw"},
  {"cw_count=2,cw_group=cp_number,field_order=cw_group", "must contain cw"},
  {"cw_group=cw", "only legal when cw_count>1"},
  {"include_ecm_id=1,field_order=cw", "ecm_id exactly once iff include_ecm_id=1"},
  {"field_order=ecm_id+cw", "ecm_id exactly once iff include_ecm_id=1"},
  {"cw_count=2,cw_group=cw,field_order=cw+cw_group", "cw is only legal inside cw_group"},
  {"cw_count=2,cw_group=cw,field_order=cp_number", "cw_group exactly once"},
  {"cw_count=2,cw_group=cw,field_order=cp_number+cp_number+cw_group,include_cp_number=1", "cp_number placement inconsistent"},
  {"cw_count=2,cw_group=cw,field_order=cp_number+cw_group", "cp_number placement inconsistent"},
  {"field_order=cw_group+cw", "cw_group is only legal when cw_count>1"},
  {"field_order=cp_number", "cw exactly once"},
  {"field_order=cw+cw", "cw exactly once"},
  {"include_cp_number=1,field_order=cw", "cp_number exactly once iff include_cp_number=1"},
  {"field_order=cp_number+cw", "cp_number exactly once iff include_cp_number=1"},
  {"integrity=crc32,integrity_order=before-encrypt,field_order=integrity_tag+cw", "integrity_tag exactly once, last"},
  {"integrity=crc32,integrity_order=before-encrypt,field_order=cw+integrity_tag+integrity_tag", "integrity_tag exactly once, last"},
  {"integrity=crc32,field_order=cw+integrity_tag", "needs integrity_order=before-encrypt"},
  {"field_order=iv+cw", "wire_order tokens"},
  {"field_order=ciphertext+cw", "wire_order tokens"},
  {"field_order=gcm_tag+cw", "wire_order tokens"},
  {"field_order=ghost+cw", "unknown header id"},
  {"cipher=aes256-ecb,wire_order=iv+ciphertext", "iv presence inconsistent"},
  {"cipher=aes256-gcm,iv=random,wire_order=ciphertext+gcm_tag", "iv presence inconsistent"},
  {"cipher=aes256-ecb,wire_order=ciphertext+ciphertext", "ciphertext exactly once"},
  {"cipher=aes256-ecb,wire_order=iv_marker", "ciphertext exactly once"},
  {"cipher=aes256-gcm,iv=random,wire_order=iv+ciphertext", "gcm_tag presence inconsistent"},
  {"cipher=aes256-ecb,integrity=crc32,wire_order=ciphertext", "integrity_tag presence inconsistent"},
  {"cipher=aes256-ecb,wire_order=ciphertext+integrity_tag", "integrity_tag presence inconsistent"},
  {"cipher=aes256-ecb,wire_order=ciphertext+cw", "field_order tokens"},
  {"cipher=aes256-ecb,wire_order=ciphertext+ecm_id", "field_order tokens"},
  {"cipher=aes256-ecb,wire_order=ciphertext+cp_number", "field_order tokens"},
  {"cipher=aes256-ecb,wire_order=ciphertext+ghost", "unknown header id"},
};

START_TEST(validate_reports_each_structural_failure) {
  const vfail_case_t *c = &vfail_cases[_i];
  ecm_profile_t p;
  char log[LOG_CAPTURE_BUF];

  ck_assert_int_eq(ecm_profile_parse(c->spec, &p), 0);
  log_capture_begin();
  ck_assert_int_eq(ecm_profile_validate(&p), -1);
  log_capture_end(log, sizeof log);
  ck_assert_msg(strstr(log, c->expect) != NULL, "case %d spec=%s log=%s", _i, c->spec, log);
}
END_TEST

START_TEST(validate_accepts_a_valid_cw_group_and_fills_the_default_orders) {
  ecm_profile_t p;

  ck_assert_int_eq(ecm_profile_parse("cipher=aes256-cbc,iv=random,integrity=hmac-sha256,cw_count=2,cw_group=cp_number+cw,field_order=ecm_id+cw_group,include_ecm_id=1,padding=pkcs7", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);
  ck_assert_int_eq(p.format.wire_order.count, 3);
  ck_assert_int_eq(p.format.wire_order.tok[0].kind, ECM_TOK_IV);
  ck_assert_int_eq(p.format.wire_order.tok[1].kind, ECM_TOK_CIPHERTEXT);
  ck_assert_int_eq(p.format.wire_order.tok[2].kind, ECM_TOK_INTEGRITY_TAG);
}
END_TEST

START_TEST(validate_accepts_header_references_in_both_orders) {
  ecm_profile_t p;

  ck_assert_int_eq(ecm_profile_parse("header=m:C0DE,field_order=m+cw,wire_order=m+ciphertext", &p), 0);
  ck_assert_int_eq(ecm_profile_validate(&p), 0);
}
END_TEST

START_TEST(layout_computes_wire_sizes_and_rejects_unaligned_plaintext) {
  ecm_profile_t p;
  ecm_layout_t lay;

  prof(&p, "cipher=aes256-ecb");
  ck_assert_int_eq(ecm_profile_layout(&p, 16, &lay), 0);
  ck_assert_uint_eq(lay.plaintext_len, 16u);
  ck_assert_uint_eq(lay.wire_len, 16u);
  ck_assert_int_eq(ecm_profile_layout(&p, 8, &lay), -1);

  prof(&p, "cipher=aes256-gcm,iv=random");
  ck_assert_int_eq(ecm_profile_layout(&p, 8, &lay), 0);
  ck_assert_uint_eq(lay.iv_len, 12u);
  ck_assert_uint_eq(lay.gcm_tag_len, 16u);
  ck_assert_uint_eq(lay.wire_len, 12u + 8u + 16u);

  prof(&p, "cipher=aes256-cbc,iv=random,padding=pkcs7,integrity=hmac-sha256,truncate_tag=8,header=m:80,wire_order=m+iv+ciphertext+integrity_tag");
  ck_assert_int_eq(ecm_profile_layout(&p, 16, &lay), 0);
  ck_assert_uint_eq(lay.ciphertext_len, 32u);
  ck_assert_uint_eq(lay.integrity_tag_len, 8u);
  ck_assert_uint_eq(lay.wire_len, 1u + 16u + 32u + 8u);

  prof(&p, "cipher=aes256-ecb,padding=zero");
  ck_assert_int_eq(ecm_profile_layout(&p, 8, &lay), 0);
  ck_assert_uint_eq(lay.ciphertext_len, 16u);

  prof(&p, "cipher=des-ede3-cbc,iv=random,padding=pkcs7");
  ck_assert_int_eq(ecm_profile_layout(&p, 8, &lay), 0);
  ck_assert_uint_eq(lay.iv_len, 8u);
  ck_assert_uint_eq(lay.ciphertext_len, 16u);
}
END_TEST

static Suite *ecm_profile_suite(void) {
  Suite *s = suite_create("ecm_profile");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_sets_defaults);
  tcase_add_test(tc, parse_rejects_unknown_field);
  tcase_add_test(tc, parse_rejects_malformed_pair);
  tcase_add_test(tc, parse_rejects_bad_enum_value);
  tcase_add_test(tc, parse_default_field_order_with_header_and_flags);
  tcase_add_test(tc, validate_rejects_ecb_with_iv);
  tcase_add_test(tc, validate_rejects_cbc_without_iv);
  tcase_add_test(tc, validate_rejects_gcm_with_padding);
  tcase_add_test(tc, validate_accepts_gcm_with_nonrandom_iv);
  tcase_add_test(tc, validate_rejects_truncate_tag_with_crc32);
  tcase_add_test(tc, validate_rejects_short_key_info_with_truncate_source);
  tcase_add_test(tc, validate_rejects_mte_without_explicit_field_order);
  tcase_add_test(tc, validate_rejects_cw_count_without_explicit_field_order);
  tcase_add_test(tc, roundtrip_ecb_legacy_equivalent);
  tcase_add_test(tc, roundtrip_cbc_pkcs7_hmac_truncated);
  tcase_add_test(tc, roundtrip_gcm_random_iv_no_binds);
  tcase_add_test(tc, roundtrip_ecb_crc32);
  tcase_add_loop_test(tc, reference_round_trip_matches_for_many_profile_shapes, 0, 23);
  tcase_add_test(tc, cw_groups_decode_every_combo_with_wire_or_derived_cp_numbers);
  tcase_add_loop_test(tc, tampered_wire_is_rejected_per_integrity_mode, 0, 14);
  tcase_add_test(tc, bindings_reject_foreign_ecm_id_and_cp_number_unless_disabled);
  tcase_add_test(tc, wrong_key_or_wrong_cw_len_is_rejected);
  tcase_add_loop_test(tc, parse_setters_store_their_value, 0, sizeof setter_cases / sizeof setter_cases[0]);
  tcase_add_loop_test(tc, parse_rejects_malformed_values, 0, sizeof bad_specs / sizeof bad_specs[0]);
  tcase_add_test(tc, parse_rejects_a_spec_longer_than_the_buffer);
  tcase_add_test(tc, parse_token_lists_and_headers_keep_order_and_ids);
  tcase_add_test(tc, parse_accepts_the_longest_allowed_info_strings_and_ids);
  tcase_add_loop_test(tc, validate_reports_each_structural_failure, 0, sizeof vfail_cases / sizeof vfail_cases[0]);
  tcase_add_test(tc, validate_accepts_a_valid_cw_group_and_fills_the_default_orders);
  tcase_add_test(tc, validate_accepts_header_references_in_both_orders);
  tcase_add_test(tc, layout_computes_wire_sizes_and_rejects_unaligned_plaintext);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ecm_profile_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
