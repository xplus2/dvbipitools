/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "dipicam378/crypto.h"
#include "dipicam378/device.h"

#define SC_SECTION_TID_EMM 0x82
#define SC_SECTION_TID_ECM_EVEN 0x80
#define TEST_SERIAL "test-serial-01"

static char g_key_path[] = "/tmp/dipicam378_test_device_key_XXXXXX";

static void section_header(unsigned char table_id, size_t payload_len, unsigned char out[3]) {
  out[0] = table_id;
  out[1] = (unsigned char)(0x70 | ((payload_len >> 8) & 0x0F));
  out[2] = (unsigned char)(payload_len & 0xFF);
}

static device_state_t *make_device_full(EVP_PKEY **pub_out, int cw_len, const char *serial, unsigned caid) {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  EVP_PKEY *pkey = NULL;
  int fd;
  FILE *f;
  device_state_t *d;

  ck_assert_int_gt(EVP_PKEY_keygen_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048), 0);
  ck_assert_int_gt(EVP_PKEY_keygen(ctx, &pkey), 0);
  EVP_PKEY_CTX_free(ctx);

  strcpy(g_key_path, "/tmp/dipicam378_test_device_key_XXXXXX");
  fd = mkstemp(g_key_path);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_int_eq(PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL), 1);
  fclose(f);

  d = device_state_new(g_key_path, cw_len, serial, caid);
  ck_assert_ptr_nonnull(d);
  remove(g_key_path);

  *pub_out = pkey;
  return d;
}

static device_state_t *make_device_serial(EVP_PKEY **pub_out, int cw_len, const char *serial) {
  return make_device_full(pub_out, cw_len, serial, 0);
}

static device_state_t *make_device(EVP_PKEY **pub_out, int cw_len) {
  return make_device_serial(pub_out, cw_len, TEST_SERIAL);
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
   AES-256-ECB(cw zero-padded to 16 bytes, under sk), matches Simulcrypt ECMG's ECM payload layout */
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
  device_state_t *d = make_device(&pub, 8);
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
  device_on_emm(d, buf, n);

  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 0, cw_out), 0);
  /* build_ecm() uses SC_SECTION_TID_ECM_EVEN: cw lands in the even half [8:16),
     odd half [0:8) stays zeroed - never duplicated (see device_resolve_cw()) */
  ck_assert_mem_eq(cw_out + 8, cw, 8);
  {
    unsigned char zero[8] = {0};
    ck_assert_mem_eq(cw_out, zero, 8);
  }

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(csa2_odd_ecm_fills_odd_half_only) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub, 8);
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN];
  unsigned char cw[8] = {0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)(i + 30);
    sk[i] = (unsigned char)(150 - i);
  }

  n = build_emm_u(pub, bk, buf, sizeof buf);
  device_on_emm(d, buf, n);
  n = build_emm_g(bk, sk, 0x0065, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  buf[0] = 0x81; /* SC_SECTION_TID_ECM_ODD, overriding build_ecm()'s default even */
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0065, 0, cw_out), 0);
  ck_assert_mem_eq(cw_out, cw, 8);
  {
    unsigned char zero[8] = {0};
    ck_assert_mem_eq(cw_out + 8, zero, 8);
  }

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(full_chain_cissa_recovers_cw) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub, 16);
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
  device_on_emm(d, buf, n);

  n = build_emm_g(bk, sk, 0x00C8, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 16, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x00C8, 0, cw_out), 0);
  ck_assert_mem_eq(cw_out, cw, 16);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_fails_for_unknown_service) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub, 8);
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[64];
  unsigned char cw_out[16];
  size_t n = build_ecm(sk, cw, 8, buf, sizeof buf);

  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x9999, 0, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_fails_before_any_emm_g) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub, 8);
  unsigned char bk[CRYPTO_KEY_LEN] = {0};
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;

  /* EMM-U only, no EMM-G ever arrives for this service */
  n = build_emm_u(pub, bk, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 0, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(emm_g_ignored_before_emm_u_sets_bk) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub, 8);
  unsigned char bk[CRYPTO_KEY_LEN] = {0};
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;

  /* EMM-G arrives before any EMM-U ever set a BK - must be ignored, not crash */
  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 0, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(emm_u_for_another_serial_is_ignored) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub, 8);
  unsigned char bk[CRYPTO_KEY_LEN] = {0};
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;

  n = build_emm_u_for(pub, bk, "somebody-elses-serial", buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  device_on_emm(d, buf, n); /* SK cache needs a BK first - must stay unset */

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 0, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(no_serial_configured_accepts_any_emm_u) {
  EVP_PKEY *pub;
  device_state_t *d = make_device_serial(&pub, 8, NULL);
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

  n = build_emm_u_for(pub, bk, "whatever-serial", buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_emm_g(bk, sk, 0x0001, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0001, 0, cw_out), 0);
  ck_assert_mem_eq(cw_out + 8, cw, 8); /* build_ecm() defaults to SC_SECTION_TID_ECM_EVEN */

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_rejects_wrong_caid) {
  EVP_PKEY *pub;
  device_state_t *d = make_device_full(&pub, 8, TEST_SERIAL, 0x0B75);
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

  /* SK is fully established - a wrong caid must still be rejected, permanently,
     not just "not found yet" */
  n = build_emm_u(pub, bk, buf, sizeof buf);
  device_on_emm(d, buf, n);
  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 0x0B76, cw_out), -2);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_accepts_matching_caid) {
  EVP_PKEY *pub;
  device_state_t *d = make_device_full(&pub, 8, TEST_SERIAL, 0x0B75);
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
  device_on_emm(d, buf, n);
  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  device_on_emm(d, buf, n);

  n = build_ecm(sk, cw, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 0x0B75, cw_out), 0);
  ck_assert_mem_eq(cw_out + 8, cw, 8); /* build_ecm() defaults to SC_SECTION_TID_ECM_EVEN */

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(resolve_cw_no_sk_yet_stays_transient_even_with_caid_configured) {
  EVP_PKEY *pub;
  device_state_t *d = make_device_full(&pub, 8, TEST_SERIAL, 0x0B75);
  unsigned char sk[CRYPTO_KEY_LEN] = {0};
  unsigned char cw[8] = {0};
  unsigned char buf[64];
  unsigned char cw_out[16];
  /* no EMM-G ever sent: SK not established, but caid matches - must be -1
     (transient, oscam should keep retrying), never -2 (permanent) */
  size_t n = build_ecm(sk, cw, 8, buf, sizeof buf);

  ck_assert_int_eq(device_resolve_cw(d, buf, n, 0x0064, 0x0B75, cw_out), -1);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

static Suite *device_suite(void) {
  Suite *s = suite_create("device");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, full_chain_csa2_recovers_cw);
  tcase_add_test(tc, csa2_odd_ecm_fills_odd_half_only);
  tcase_add_test(tc, full_chain_cissa_recovers_cw);
  tcase_add_test(tc, resolve_cw_fails_for_unknown_service);
  tcase_add_test(tc, resolve_cw_fails_before_any_emm_g);
  tcase_add_test(tc, emm_g_ignored_before_emm_u_sets_bk);
  tcase_add_test(tc, emm_u_for_another_serial_is_ignored);
  tcase_add_test(tc, no_serial_configured_accepts_any_emm_u);
  tcase_add_test(tc, resolve_cw_rejects_wrong_caid);
  tcase_add_test(tc, resolve_cw_accepts_matching_caid);
  tcase_add_test(tc, resolve_cw_no_sk_yet_stays_transient_even_with_caid_configured);
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
