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

#include "dipidescramble/crypto.h"
#include "dipidescramble/device.h"

#define SC_SECTION_TID_EMM 0x82
#define SC_SECTION_TID_ECM_EVEN 0x80
#define TEST_SERIAL "test-serial-01"
#define TEST_ECM_PID 0x0020

static char g_key_path[] = "/tmp/dipidescramble_test_device_key_XXXXXX";
static const ecm_profile_t no_profile; /* zero-initialized: profile.set == 0, legacy path */

static void section_header(unsigned char table_id, size_t payload_len, unsigned char out[3]) {
  out[0] = table_id;
  out[1] = (unsigned char)(0x70 | ((payload_len >> 8) & 0x0F));
  out[2] = (unsigned char)(payload_len & 0xFF);
}

static device_state_t *make_device_serial(EVP_PKEY **pub_out, const char *serial) {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  EVP_PKEY *pkey = NULL;
  int fd;
  FILE *f;
  device_state_t *d;

  ck_assert_int_gt(EVP_PKEY_keygen_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048), 0);
  ck_assert_int_gt(EVP_PKEY_keygen(ctx, &pkey), 0);
  EVP_PKEY_CTX_free(ctx);

  strcpy(g_key_path, "/tmp/dipidescramble_test_device_key_XXXXXX");
  fd = mkstemp(g_key_path);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_int_eq(PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL), 1);
  fclose(f);

  d = device_state_new(g_key_path, serial, &no_profile);
  ck_assert_ptr_nonnull(d);
  remove(g_key_path);

  *pub_out = pkey;
  return d;
}

static device_state_t *make_device(EVP_PKEY **pub_out) {
  return make_device_serial(pub_out, TEST_SERIAL);
}

/* builds a real EMM-U section: header + [1B addr_len][serial][2B bk_version][RSA-OAEP(bk)] */
static size_t build_emm_u_for(EVP_PKEY *pub, const unsigned char bk[CRYPTO_KEY_LEN], const char *serial, unsigned char *out, size_t cap) {
  size_t addr_len = strlen(serial);
  unsigned char ct[512];
  size_t ctlen = sizeof ct;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pub, NULL);
  size_t payload_len;

  ck_assert_int_gt(EVP_PKEY_encrypt_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING), 0);
  ck_assert_int_gt(EVP_PKEY_encrypt(ctx, ct, &ctlen, bk, CRYPTO_KEY_LEN), 0);
  EVP_PKEY_CTX_free(ctx);

  payload_len = 1 + addr_len + 2 + ctlen;
  ck_assert_uint_le(3 + payload_len, cap);

  section_header(SC_SECTION_TID_EMM, payload_len, out);
  out[3] = (unsigned char)addr_len;
  memcpy(out + 4, serial, addr_len);
  out[4 + addr_len] = 0;
  out[5 + addr_len] = 1; /* bk_version, arbitrary */
  memcpy(out + 6 + addr_len, ct, ctlen);
  return 3 + payload_len;
}

static size_t build_emm_u(EVP_PKEY *pub, const unsigned char bk[CRYPTO_KEY_LEN], unsigned char *out, size_t cap) {
  return build_emm_u_for(pub, bk, TEST_SERIAL, out, cap);
}

/* builds a real EMM-G section: header + [2B service_id][2B sk_version][AES-256-GCM(sk under bk)] */
static size_t build_emm_g(const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char sk[CRYPTO_KEY_LEN], unsigned service_id, unsigned char *out, size_t cap) {
  unsigned char *blob;
  unsigned char *nonce, *ct, *tag;
  EVP_CIPHER_CTX *ctx;
  int len = 0;
  size_t payload_len = 4 + CRYPTO_EMM_G_LEN;

  ck_assert_uint_le(3 + payload_len, cap);
  section_header(SC_SECTION_TID_EMM, payload_len, out);
  out[3] = (unsigned char)(service_id >> 8);
  out[4] = (unsigned char)service_id;
  out[5] = 0;
  out[6] = 1; /* sk_version, arbitrary */

  blob = out + 7;
  nonce = blob;
  ct = blob + CRYPTO_GCM_NONCE_LEN;
  tag = blob + CRYPTO_GCM_NONCE_LEN + CRYPTO_KEY_LEN;
  memset(nonce, 0x24, CRYPTO_GCM_NONCE_LEN);

  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CRYPTO_GCM_NONCE_LEN, NULL), 1);
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, NULL, NULL, bk, nonce), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, sk, CRYPTO_KEY_LEN), 1);
  ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &len), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, CRYPTO_GCM_TAG_LEN, tag), 1);
  EVP_CIPHER_CTX_free(ctx);

  return 3 + payload_len;
}

/* builds a real ECM section: header + CP_CW_COMBINATION's cp_number(2) +
   AES-256-ECB(cw zero-padded to 16 bytes, under sk) - matches the real
   Simulcrypt ECMG's ECM payload layout */
static size_t build_ecm(const unsigned char sk[CRYPTO_KEY_LEN], const unsigned char *cw, int cw_len, unsigned char *out, size_t cap) {
  unsigned char block[CRYPTO_CW_ENC_LEN];
  EVP_CIPHER_CTX *ctx;
  int len = 0;

  ck_assert_uint_le(5 + CRYPTO_CW_ENC_LEN, cap);
  memset(block, 0, sizeof block);
  memcpy(block, cw, (size_t)cw_len);

  section_header(SC_SECTION_TID_ECM_EVEN, 2 + CRYPTO_CW_ENC_LEN, out);
  out[3] = 0;
  out[4] = 1; /* cp_number, arbitrary */
  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, sk, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, out + 5, &len, block, CRYPTO_CW_ENC_LEN), 1);
  EVP_CIPHER_CTX_free(ctx);

  return 5 + CRYPTO_CW_ENC_LEN;
}

START_TEST(full_chain_csa2_recovers_cw) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN];
  unsigned char cw[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)(i + 10);
    sk[i] = (unsigned char)(200 - i);
  }

  n = build_emm_u(pub, bk, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 8, TEST_ECM_PID, cw_out), 0);
  ck_assert_mem_eq(cw_out, cw, 8);
  ck_assert_mem_eq(cw_out + 8, cw, 8); /* CSA2: duplicated into both halves */

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_ignores_srvid_mismatch_with_one_cached_service) {
  /* --sid need not equal the CAS's service_id - mux-wide CW, one session per process */
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN];
  unsigned char cw[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)(i + 10);
    sk[i] = (unsigned char)(200 - i);
  }

  n = build_emm_u(pub, bk, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf); /* CAS-side service_id 0x0064 */
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x012D, 8, TEST_ECM_PID, cw_out), 0); /* mismatched srvid 0x012D (301) */
  ck_assert_mem_eq(cw_out, cw, 8);
  ck_assert_mem_eq(cw_out + 8, cw, 8);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(full_chain_cissa_recovers_cw) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN];
  unsigned char cw[16];
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)(i + 20);
    sk[i] = (unsigned char)(100 - i);
  }
  for (i = 0; i < 16; i++)
    cw[i] = (unsigned char)(0x30 + i);

  n = build_emm_u(pub, bk, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_emm_g(bk, sk, 0x00C8, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_ecm(sk, cw, 16, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x00C8, 16, TEST_ECM_PID, cw_out), 0);
  ck_assert_mem_eq(cw_out, cw, 16);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_fails_for_unknown_service) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[64];
  unsigned char cw_out[16];
  size_t n = build_ecm(sk, cw, 8, buf, sizeof buf);

  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x9999, 8, TEST_ECM_PID, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_fails_before_any_emm_g) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN] = {0};
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;

  n = build_emm_u(pub, bk, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 8, TEST_ECM_PID, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_rejects_bad_cw_len) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN];
  unsigned char cw[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)i;
    sk[i] = (unsigned char)(255 - i);
  }
  n = build_emm_u(pub, bk, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);
  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 12, TEST_ECM_PID, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(emm_g_ignored_before_emm_u_sets_bk) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN] = {0};
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;

  /* EMM-G arrives before any EMM-U ever set a BK - must be ignored, not crash */
  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 0);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 8, TEST_ECM_PID, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(emm_u_for_another_serial_is_ignored) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN] = {0};
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;

  n = build_emm_u_for(pub, bk, "somebody-elses-serial", buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 0);

  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 0); /* SK cache needs a BK first - stays unset */

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 8, TEST_ECM_PID, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(device_state_new_rejects_empty_serial) {
  char path[] = "/tmp/dipidescramble_test_device_key2_XXXXXX";
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  ck_assert_ptr_null(device_state_new(path, "", &no_profile));
  remove(path);
}
END_TEST

static Suite *device_suite(void) {
  Suite *s = suite_create("device");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, full_chain_csa2_recovers_cw);
  tcase_add_test(tc, resolve_cw_ignores_srvid_mismatch_with_one_cached_service);
  tcase_add_test(tc, full_chain_cissa_recovers_cw);
  tcase_add_test(tc, resolve_cw_fails_for_unknown_service);
  tcase_add_test(tc, resolve_cw_fails_before_any_emm_g);
  tcase_add_test(tc, resolve_cw_rejects_bad_cw_len);
  tcase_add_test(tc, emm_g_ignored_before_emm_u_sets_bk);
  tcase_add_test(tc, emm_u_for_another_serial_is_ignored);
  tcase_add_test(tc, device_state_new_rejects_empty_serial);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(device_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
