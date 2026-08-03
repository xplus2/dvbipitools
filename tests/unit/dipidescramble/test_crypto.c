/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "dipidescramble/crypto.h"

static EVP_PKEY *make_rsa_key(void) {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  EVP_PKEY *pkey = NULL;
  ck_assert_ptr_nonnull(ctx);
  ck_assert_int_gt(EVP_PKEY_keygen_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048), 0);
  ck_assert_int_gt(EVP_PKEY_keygen(ctx, &pkey), 0);
  EVP_PKEY_CTX_free(ctx);
  return pkey;
}

START_TEST(device_key_load_roundtrip) {
  EVP_PKEY *pkey = make_rsa_key();
  EVP_PKEY *loaded = NULL;
  char path[] = "/tmp/dipidescramble_test_key_XXXXXX";
  int fd = mkstemp(path);
  FILE *f;

  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL), 1);
  fclose(f);

  ck_assert_int_eq(device_key_load(path, &loaded), 0);
  ck_assert_ptr_nonnull(loaded);
  ck_assert_int_eq(EVP_PKEY_eq(pkey, loaded), 1);

  remove(path);
  EVP_PKEY_free(pkey);
  EVP_PKEY_free(loaded);
}
END_TEST

START_TEST(device_key_load_rejects_missing_file) {
  EVP_PKEY *out = NULL;
  ck_assert_int_eq(device_key_load("/nonexistent/path/to/key.pem", &out), -1);
}
END_TEST

START_TEST(emm_u_decrypt_recovers_bk) {
  EVP_PKEY *pkey = make_rsa_key();
  EVP_PKEY_CTX *ctx;
  unsigned char bk[CRYPTO_KEY_LEN], recovered[CRYPTO_KEY_LEN];
  unsigned char ct[512];
  size_t ctlen = sizeof ct;
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++)
    bk[i] = (unsigned char)(i * 7 + 1);

  ctx = EVP_PKEY_CTX_new(pkey, NULL);
  ck_assert_ptr_nonnull(ctx);
  ck_assert_int_gt(EVP_PKEY_encrypt_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING), 0);
  ck_assert_int_gt(EVP_PKEY_encrypt(ctx, ct, &ctlen, bk, sizeof bk), 0);
  EVP_PKEY_CTX_free(ctx);

  ck_assert_int_eq(device_emm_u_decrypt(pkey, ct, ctlen, recovered), 0);
  ck_assert_mem_eq(bk, recovered, CRYPTO_KEY_LEN);

  EVP_PKEY_free(pkey);
}
END_TEST

START_TEST(emm_u_decrypt_rejects_wrong_key) {
  EVP_PKEY *pkey = make_rsa_key();
  EVP_PKEY *other = make_rsa_key();
  EVP_PKEY_CTX *ctx;
  unsigned char bk[CRYPTO_KEY_LEN] = {0}, recovered[CRYPTO_KEY_LEN];
  unsigned char ct[512];
  size_t ctlen = sizeof ct;

  ctx = EVP_PKEY_CTX_new(pkey, NULL);
  ck_assert_int_gt(EVP_PKEY_encrypt_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING), 0);
  ck_assert_int_gt(EVP_PKEY_encrypt(ctx, ct, &ctlen, bk, sizeof bk), 0);
  EVP_PKEY_CTX_free(ctx);

  ck_assert_int_eq(device_emm_u_decrypt(other, ct, ctlen, recovered), -1);

  EVP_PKEY_free(pkey);
  EVP_PKEY_free(other);
}
END_TEST

static void gcm_encrypt(const unsigned char key[CRYPTO_KEY_LEN], const unsigned char pt[CRYPTO_KEY_LEN], unsigned char out[CRYPTO_EMM_G_LEN]) {
  unsigned char *nonce = out, *ct = out + CRYPTO_GCM_NONCE_LEN, *tag = out + CRYPTO_GCM_NONCE_LEN + CRYPTO_KEY_LEN;
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len = 0;

  memset(nonce, 0x42, CRYPTO_GCM_NONCE_LEN);
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CRYPTO_GCM_NONCE_LEN, NULL), 1);
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, pt, CRYPTO_KEY_LEN), 1);
  ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &len), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, CRYPTO_GCM_TAG_LEN, tag), 1);
  EVP_CIPHER_CTX_free(ctx);
}

START_TEST(emm_g_decrypt_recovers_sk) {
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN], recovered[CRYPTO_KEY_LEN];
  unsigned char blob[CRYPTO_EMM_G_LEN];
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)(i + 1);
    sk[i] = (unsigned char)(255 - i);
  }
  gcm_encrypt(bk, sk, blob);

  ck_assert_int_eq(device_emm_g_decrypt(bk, blob, recovered), 0);
  ck_assert_mem_eq(sk, recovered, CRYPTO_KEY_LEN);
}
END_TEST

START_TEST(emm_g_decrypt_rejects_bad_tag) {
  unsigned char bk[CRYPTO_KEY_LEN] = {0}, sk[CRYPTO_KEY_LEN] = {0}, recovered[CRYPTO_KEY_LEN];
  unsigned char blob[CRYPTO_EMM_G_LEN];

  gcm_encrypt(bk, sk, blob);
  blob[CRYPTO_EMM_G_LEN - 1] ^= 0xFF; /* corrupt the tag */

  ck_assert_int_eq(device_emm_g_decrypt(bk, blob, recovered), -1);
}
END_TEST

static void ecb_encrypt_block(const unsigned char key[CRYPTO_KEY_LEN], const unsigned char pt[CRYPTO_CW_ENC_LEN], unsigned char out[CRYPTO_CW_ENC_LEN]) {
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len = 0;
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, out, &len, pt, CRYPTO_CW_ENC_LEN), 1);
  EVP_CIPHER_CTX_free(ctx);
}

START_TEST(ecm_decrypt_recovers_csa2_cw) {
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char block[CRYPTO_CW_ENC_LEN] = {0};
  unsigned char enc[CRYPTO_CW_ENC_LEN];
  unsigned char cw[8];
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++)
    sk[i] = (unsigned char)(i * 3);
  for (i = 0; i < 8; i++)
    block[i] = (unsigned char)(0xA0 + i); /* CW in the first 8 bytes, rest zero-padded */

  ecb_encrypt_block(sk, block, enc);

  ck_assert_int_eq(device_ecm_decrypt(sk, enc, 8, cw), 0);
  ck_assert_mem_eq(cw, block, 8);
}
END_TEST

START_TEST(ecm_decrypt_recovers_cissa_cw) {
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char block[CRYPTO_CW_ENC_LEN];
  unsigned char enc[CRYPTO_CW_ENC_LEN];
  unsigned char cw[16];
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++)
    sk[i] = (unsigned char)(i * 5 + 2);
  for (i = 0; i < 16; i++)
    block[i] = (unsigned char)(0x10 + i);

  ecb_encrypt_block(sk, block, enc);

  ck_assert_int_eq(device_ecm_decrypt(sk, enc, 16, cw), 0);
  ck_assert_mem_eq(cw, block, 16);
}
END_TEST

static Suite *crypto_suite(void) {
  Suite *s = suite_create("crypto");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, device_key_load_roundtrip);
  tcase_add_test(tc, device_key_load_rejects_missing_file);
  tcase_add_test(tc, emm_u_decrypt_recovers_bk);
  tcase_add_test(tc, emm_u_decrypt_rejects_wrong_key);
  tcase_add_test(tc, emm_g_decrypt_recovers_sk);
  tcase_add_test(tc, emm_g_decrypt_rejects_bad_tag);
  tcase_add_test(tc, ecm_decrypt_recovers_csa2_cw);
  tcase_add_test(tc, ecm_decrypt_recovers_cissa_cw);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(crypto_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
