/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "lib/cas/device_state_core.h"

#define EMM_HDR 3

static char g_dir[64];
static char g_key_path[128];
static EVP_PKEY *g_pkey;

static void key_begin(void) {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  FILE *f;

  ck_assert_ptr_nonnull(ctx);
  ck_assert_int_gt(EVP_PKEY_keygen_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 1024), 0);
  ck_assert_int_gt(EVP_PKEY_keygen(ctx, &g_pkey), 0);
  EVP_PKEY_CTX_free(ctx);
  snprintf(g_dir, sizeof g_dir, "/tmp/dev_core_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  snprintf(g_key_path, sizeof g_key_path, "%s/key.pem", g_dir);
  f = fopen(g_key_path, "w");
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(PEM_write_PrivateKey(f, g_pkey, NULL, NULL, 0, NULL, NULL), 1);
  fclose(f);
}

static void key_end(void) {
  unlink(g_key_path);
  rmdir(g_dir);
  EVP_PKEY_free(g_pkey);
  g_pkey = NULL;
}

static size_t build_emm_u(unsigned char *out, const char *serial, const unsigned char bk[CRYPTO_KEY_LEN], EVP_PKEY *enc_key, int corrupt) {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(enc_key, NULL);
  unsigned char ct[256];
  size_t ctlen = sizeof ct;
  size_t slen = strlen(serial);
  size_t off = EMM_HDR;

  ck_assert_int_gt(EVP_PKEY_encrypt_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING), 0);
  ck_assert_int_gt(EVP_PKEY_encrypt(ctx, ct, &ctlen, bk, CRYPTO_KEY_LEN), 0);
  EVP_PKEY_CTX_free(ctx);
  if (corrupt)
    ct[0] ^= 0x55;
  memset(out, 0, EMM_HDR);
  out[off++] = (unsigned char)slen;
  memcpy(out + off, serial, slen);
  off += slen;
  out[off++] = 0;
  out[off++] = 0;
  memcpy(out + off, ct, ctlen);
  return off + ctlen;
}

static void gcm_encrypt(const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char sk[CRYPTO_KEY_LEN], unsigned char out[CRYPTO_EMM_G_LEN]) {
  unsigned char *nonce = out;
  unsigned char *ct = out + CRYPTO_GCM_NONCE_LEN;
  unsigned char *tag = ct + CRYPTO_KEY_LEN;
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len = 0;

  memset(nonce, 0x21, CRYPTO_GCM_NONCE_LEN);
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CRYPTO_GCM_NONCE_LEN, NULL), 1);
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, NULL, NULL, bk, nonce), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, sk, CRYPTO_KEY_LEN), 1);
  ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &len), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, CRYPTO_GCM_TAG_LEN, tag), 1);
  EVP_CIPHER_CTX_free(ctx);
}

static size_t build_emm_g(unsigned char *out, unsigned service_id, const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char sk[CRYPTO_KEY_LEN], int corrupt) {
  memset(out, 0, EMM_HDR + 4);
  out[EMM_HDR] = (unsigned char)(service_id >> 8);
  out[EMM_HDR + 1] = (unsigned char)service_id;
  gcm_encrypt(bk, sk, out + EMM_HDR + 4);
  if (corrupt)
    out[EMM_HDR + 4 + CRYPTO_EMM_G_LEN - 1] ^= 0xFF;
  return EMM_HDR + 4 + CRYPTO_EMM_G_LEN;
}

static void fill(unsigned char *p, unsigned char base) {
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++)
    p[i] = (unsigned char)(base + i);
}

START_TEST(init_rejects_missing_key_and_oversized_serial) {
  device_core_t core;
  char serial[DEVICE_SERIAL_MAX + 1];

  key_begin();
  memset(serial, 'S', sizeof serial);
  serial[sizeof serial - 1] = '\0';
  ck_assert_int_eq(device_core_init(&core, "/nonexistent/key.pem", "x", 0), -1);
  ck_assert_int_eq(device_core_init(&core, g_key_path, serial, 0), -1);
  ck_assert_ptr_null(device_core_alloc(sizeof core, "/nonexistent/key.pem", NULL, 0));
  device_core_free_state(NULL);
  key_end();
}
END_TEST

START_TEST(init_applies_service_limit_defaults_and_ceiling) {
  device_core_t core;

  key_begin();
  ck_assert_int_eq(device_core_init(&core, g_key_path, NULL, 0), 0);
  ck_assert_uint_eq(core.max_services, 32u);
  ck_assert_uint_eq(core.serial_len, 0u);
  device_core_release(&core);
  ck_assert_int_eq(device_core_init(&core, g_key_path, "abc", 100000), 0);
  ck_assert_uint_eq(core.max_services, (size_t)DEVICE_MAX_SERVICES_CEILING);
  ck_assert_uint_eq(core.serial_len, 3u);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(services_active_counts_only_services_with_a_key) {
  device_core_t core;
  service_key_t *a;
  service_key_t *b;

  key_begin();
  ck_assert_int_eq(device_core_init(&core, g_key_path, NULL, 4), 0);
  ck_assert_uint_eq(device_core_services_active(&core), 0u);
  device_core_lock(&core);
  a = device_core_service_slot_locked(&core, 1, 1);
  b = device_core_service_slot_locked(&core, 2, 1);
  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_nonnull(b);
  a->have = 1;
  device_core_unlock(&core);
  ck_assert_uint_eq(device_core_services_active(&core), 1u);
  device_core_lock(&core);
  b->have = 1;
  device_core_unlock(&core);
  ck_assert_uint_eq(device_core_services_active(&core), 2u);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(emm_u_decrypt_failure_leaves_bk_unset) {
  device_core_t core;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char emm[512];
  size_t n;

  key_begin();
  fill(bk, 1);
  ck_assert_int_eq(device_core_init(&core, g_key_path, "SER1", 0), 0);
  n = build_emm_u(emm, "SER1", bk, g_pkey, 1);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, "t: "), 0);
  ck_assert_int_eq(core.have_bk, 0);
  n = build_emm_u(emm, "SER1", bk, g_pkey, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, "t: "), 1);
  ck_assert_int_eq(core.have_bk, 1);
  ck_assert_mem_eq(core.bk, bk, CRYPTO_KEY_LEN);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(emm_u_for_other_serial_or_too_short_is_skipped) {
  device_core_t core;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char emm[512];
  size_t n;

  key_begin();
  fill(bk, 9);
  ck_assert_int_eq(device_core_init(&core, g_key_path, "SER1", 0), 0);
  n = build_emm_u(emm, "OTHR", bk, g_pkey, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 0);
  n = build_emm_u(emm, "SER1X", bk, g_pkey, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, 2, ""), 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, EMM_HDR + 2, ""), 0);
  emm[EMM_HDR] = 200;
  ck_assert_int_eq(device_core_on_emm(&core, emm, EMM_HDR + 10, ""), 0);
  ck_assert_int_eq(core.have_bk, 0);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(emm_u_without_serial_filter_accepts_any_address) {
  device_core_t core;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char emm[512];
  size_t n;

  key_begin();
  fill(bk, 77);
  ck_assert_int_eq(device_core_init(&core, g_key_path, NULL, 0), 0);
  n = build_emm_u(emm, "ANYTHING", bk, g_pkey, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 1);
  ck_assert_mem_eq(core.bk, bk, CRYPTO_KEY_LEN);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(emm_g_decrypt_failure_leaves_service_without_key) {
  device_core_t core;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char emm[256];
  unsigned char got[CRYPTO_KEY_LEN];
  size_t n;

  key_begin();
  fill(bk, 3);
  fill(sk, 100);
  ck_assert_int_eq(device_core_init(&core, g_key_path, NULL, 0), 0);
  n = build_emm_g(emm, 0x0102, bk, sk, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 0);
  n = build_emm_u(emm, "S", bk, g_pkey, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 1);
  n = build_emm_g(emm, 0x0102, bk, sk, 1);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 0);
  ck_assert_uint_eq(device_core_services_active(&core), 0u);
  ck_assert_int_eq(device_core_copy_service_key(&core, 0x0102, 0, got), -1);
  n = build_emm_g(emm, 0x0102, bk, sk, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 1);
  ck_assert_uint_eq(device_core_services_active(&core), 1u);
  ck_assert_int_eq(device_core_copy_service_key(&core, 0x0102, 0, got), 0);
  ck_assert_mem_eq(got, sk, CRYPTO_KEY_LEN);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(emm_g_beyond_service_limit_is_dropped) {
  device_core_t core;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char emm[256];
  size_t n;

  key_begin();
  fill(bk, 5);
  fill(sk, 50);
  ck_assert_int_eq(device_core_init(&core, g_key_path, NULL, 1), 0);
  n = build_emm_u(emm, "S", bk, g_pkey, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 1);
  n = build_emm_g(emm, 1, bk, sk, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 1);
  n = build_emm_g(emm, 2, bk, sk, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 0);
  ck_assert_uint_eq(core.service_count, 1u);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(copy_service_key_sole_fallback_only_when_allowed) {
  device_core_t core;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char got[CRYPTO_KEY_LEN];
  unsigned char emm[256];
  size_t n;

  key_begin();
  fill(bk, 5);
  fill(sk, 60);
  ck_assert_int_eq(device_core_init(&core, g_key_path, NULL, 0), 0);
  n = build_emm_u(emm, "S", bk, g_pkey, 0);
  device_core_on_emm(&core, emm, n, "");
  n = build_emm_g(emm, 7, bk, sk, 0);
  ck_assert_int_eq(device_core_on_emm(&core, emm, n, ""), 1);
  ck_assert_int_eq(device_core_copy_service_key(&core, 99, 0, got), -1);
  ck_assert_int_eq(device_core_copy_service_key(&core, 99, 1, got), 0);
  ck_assert_mem_eq(got, sk, CRYPTO_KEY_LEN);
  device_core_release(&core);
  key_end();
}
END_TEST

START_TEST(alloc_and_free_state_round_trip) {
  device_core_t *core;

  key_begin();
  core = device_core_alloc(sizeof *core, g_key_path, "SER", 8);
  ck_assert_ptr_nonnull(core);
  ck_assert_uint_eq(core->max_services, 8u);
  device_core_free_state(core);
  key_end();
}
END_TEST

static Suite *device_state_core_suite(void) {
  Suite *s = suite_create("device_state_core");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_test(tc, init_rejects_missing_key_and_oversized_serial);
  tcase_add_test(tc, init_applies_service_limit_defaults_and_ceiling);
  tcase_add_test(tc, services_active_counts_only_services_with_a_key);
  tcase_add_test(tc, emm_u_decrypt_failure_leaves_bk_unset);
  tcase_add_test(tc, emm_u_for_other_serial_or_too_short_is_skipped);
  tcase_add_test(tc, emm_u_without_serial_filter_accepts_any_address);
  tcase_add_test(tc, emm_g_decrypt_failure_leaves_service_without_key);
  tcase_add_test(tc, emm_g_beyond_service_limit_is_dropped);
  tcase_add_test(tc, copy_service_key_sole_fallback_only_when_allowed);
  tcase_add_test(tc, alloc_and_free_state_round_trip);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(device_state_core_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
