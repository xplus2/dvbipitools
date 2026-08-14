/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

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

/* independent RFC 5869 HKDF-SHA256 (zero salt, single-block expand), not calling crypto.c's own
   crypto_hkdf_sha256 - this is the test's own reference implementation */
static void ref_hkdf(const unsigned char key[CRYPTO_KEY_LEN], const char *info, unsigned char out[32]) {
  unsigned char zero_salt[32] = {0};
  unsigned char prk[32];
  unsigned char t1[64];
  size_t infolen = strlen(info);
  unsigned int outlen;

  ck_assert_ptr_nonnull(HMAC(EVP_sha256(), zero_salt, sizeof zero_salt, key, CRYPTO_KEY_LEN, prk, &outlen));
  memcpy(t1, info, infolen);
  t1[infolen] = 0x01;
  ck_assert_ptr_nonnull(HMAC(EVP_sha256(), prk, sizeof prk, t1, infolen + 1, out, &outlen));
}

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
