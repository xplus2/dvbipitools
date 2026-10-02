/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_fixture.h"

START_TEST(full_chain_csa2_recovers_cw) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN];
  unsigned char cw[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  unsigned char buf[1024];
  unsigned char cw_out[16];
  size_t n;

  for (int i = 0; i < CRYPTO_KEY_LEN; i++) {
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

  for (int i = 0; i < CRYPTO_KEY_LEN; i++) {
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

START_TEST(max_services_override_drops_emm_g_past_cap) {
  EVP_PKEY *pub;
  device_state_t *d = make_device_serial(&pub, TEST_SERIAL, 2);
  unsigned char bk[CRYPTO_KEY_LEN], sk[CRYPTO_KEY_LEN];
  unsigned char buf[1024];
  size_t n;

  for (int i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)(i + 10);
    sk[i] = (unsigned char)(200 - i);
  }

  n = build_emm_u(pub, bk, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);

  n = build_emm_g(bk, sk, 0x0001, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);
  n = build_emm_g(bk, sk, 0x0002, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);
  n = build_emm_g(bk, sk, 0x0003, buf, sizeof buf); /* 3rd distinct service_id, past override cap */
  ck_assert_int_eq(device_on_emm(d, buf, n), 0);

  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

START_TEST(full_chain_cissa_recovers_cw) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
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

  for (int i = 0; i < CRYPTO_KEY_LEN; i++) {
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
  ck_assert_ptr_null(device_state_new(path, "", &no_profile, 0));
  remove(path);
}
END_TEST

static device_state_t *make_profile_device(EVP_PKEY **key_out, const char *spec, const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char sk[CRYPTO_KEY_LEN], unsigned service_id) {
  EVP_PKEY *key = make_rsa_key();
  ecm_profile_t profile;
  device_state_t *d;
  unsigned char emm[1024];
  size_t n;

  ck_assert_int_eq(ecm_profile_parse(spec, &profile), 0);
  ck_assert_int_eq(ecm_profile_validate(&profile), 0);
  strcpy(g_key_path, "/tmp/dipidescramble_test_device_key_XXXXXX");
  write_key_pem(key, g_key_path);
  d = device_state_new(g_key_path, TEST_SERIAL, &profile, 0);
  ck_assert_ptr_nonnull(d);
  remove(g_key_path);
  n = build_emm_u(key, bk, emm, sizeof emm);
  ck_assert_int_eq(device_on_emm(d, emm, n), 1);
  n = build_emm_g(bk, sk, service_id, emm, sizeof emm);
  ck_assert_int_eq(device_on_emm(d, emm, n), 1);
  *key_out = key;
  return d;
}

static size_t ecb256_encrypt(const unsigned char key[32], const unsigned char *in, size_t n, unsigned char *out) {
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len = 0;

  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, out, &len, in, (int)n), 1);
  EVP_CIPHER_CTX_free(ctx);
  return (size_t)len;
}

static size_t profile_ecm(unsigned char *out, unsigned cp, const unsigned char *wire, size_t wire_len) {
  section_header(SC_SECTION_TID_ECM_EVEN, 2 + wire_len, out);
  out[3] = (unsigned char)(cp >> 8);
  out[4] = (unsigned char)cp;
  memcpy(out + 5, wire, wire_len);
  return 5 + wire_len;
}

static void test_keys(unsigned char bk[CRYPTO_KEY_LEN], unsigned char sk[CRYPTO_KEY_LEN]) {
  int i;

  for (i = 0; i < CRYPTO_KEY_LEN; i++) {
    bk[i] = (unsigned char)(i + 33);
    sk[i] = (unsigned char)(250 - i * 3);
  }
}

START_TEST(profile_branch_decrypts_a_cissa_cw_through_hkdf_derived_key) {
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char enc[32];
  unsigned char cw[16];
  unsigned char wire[16];
  unsigned char ecm[64];
  unsigned char out[16];
  EVP_PKEY *key;
  device_state_t *d;
  size_t n;
  int i;

  test_keys(bk, sk);
  for (i = 0; i < 16; i++)
    cw[i] = (unsigned char)(0x80 + i);
  d = make_profile_device(&key, "cipher=aes256-ecb", bk, sk, 0x0064);
  ref_hkdf(sk, "dipidescramble-ecm-enc", enc);
  ck_assert_uint_eq(ecb256_encrypt(enc, cw, 16, wire), 16u);
  n = profile_ecm(ecm, 0x0007, wire, 16);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 16, TEST_ECM_PID, out), 0);
  ck_assert_mem_eq(out, cw, 16);
  EVP_PKEY_free(key);
  device_state_free(d);
}
END_TEST

START_TEST(profile_branch_duplicates_a_csa2_cw_into_both_halves) {
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char enc[32];
  unsigned char plain[16] = {1, 2, 3, 4, 5, 6, 7, 8};
  unsigned char wire[16];
  unsigned char ecm[64];
  unsigned char out[16];
  EVP_PKEY *key;
  device_state_t *d;
  size_t n;

  test_keys(bk, sk);
  d = make_profile_device(&key, "cipher=aes256-ecb,padding=zero", bk, sk, 0x0064);
  ref_hkdf(sk, "dipidescramble-ecm-enc", enc);
  ck_assert_uint_eq(ecb256_encrypt(enc, plain, 16, wire), 16u);
  n = profile_ecm(ecm, 0x0007, wire, 16);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 8, TEST_ECM_PID, out), 0);
  ck_assert_mem_eq(out, plain, 8);
  ck_assert_mem_eq(out + 8, plain, 8);
  EVP_PKEY_free(key);
  device_state_free(d);
}
END_TEST

START_TEST(profile_branch_applies_only_the_first_combo_of_a_cw_group) {
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char enc[32];
  unsigned char plain[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  unsigned char wire[16];
  unsigned char ecm[64];
  unsigned char out[16];
  EVP_PKEY *key;
  device_state_t *d;
  size_t n;

  test_keys(bk, sk);
  d = make_profile_device(&key, "cipher=aes256-ecb,cw_count=2,cw_group=cw,field_order=cw_group", bk, sk, 0x0064);
  ref_hkdf(sk, "dipidescramble-ecm-enc", enc);
  ck_assert_uint_eq(ecb256_encrypt(enc, plain, 16, wire), 16u);
  n = profile_ecm(ecm, 0x0007, wire, 16);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 8, TEST_ECM_PID, out), 0);
  ck_assert_mem_eq(out, plain, 8);
  ck_assert_mem_eq(out + 8, plain, 8);
  EVP_PKEY_free(key);
  device_state_free(d);
}
END_TEST

START_TEST(profile_branch_uses_the_ecm_pid_as_the_ecm_id_fallback_for_binding) {
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char enc[32];
  unsigned char mac[32];
  unsigned char cw[16] = {9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8};
  unsigned char macbuf[16 + 4];
  unsigned char wire[16 + 32];
  unsigned char ecm[128];
  unsigned char out[16];
  unsigned int hl;
  EVP_PKEY *key;
  device_state_t *d;
  size_t n;

  test_keys(bk, sk);
  d = make_profile_device(&key, "cipher=aes256-ecb,integrity=hmac-sha256", bk, sk, 0x0064);
  ref_hkdf(sk, "dipidescramble-ecm-enc", enc);
  ref_hkdf(sk, "dipidescramble-ecm-mac", mac);
  ck_assert_uint_eq(ecb256_encrypt(enc, cw, 16, wire), 16u);
  memcpy(macbuf, wire, 16);
  macbuf[16] = (unsigned char)(TEST_ECM_PID >> 8);
  macbuf[17] = (unsigned char)TEST_ECM_PID;
  macbuf[18] = 0x00;
  macbuf[19] = 0x07;
  ck_assert_ptr_nonnull(HMAC(EVP_sha256(), mac, 32, macbuf, sizeof macbuf, wire + 16, &hl));
  n = profile_ecm(ecm, 0x0007, wire, sizeof wire);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 16, TEST_ECM_PID, out), 0);
  ck_assert_mem_eq(out, cw, 16);
  memset(out, 0xEE, sizeof out);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 16, TEST_ECM_PID + 1, out), -1);
  ck_assert_uint_eq(out[0], 0xEEu);
  ck_assert_uint_eq(out[15], 0xEEu);
  EVP_PKEY_free(key);
  device_state_free(d);
}
END_TEST

START_TEST(profile_branch_failure_leaves_the_output_untouched) {
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char enc[32];
  unsigned char cw[16] = {1};
  unsigned char wire[16 + 4];
  unsigned char ecm[64];
  unsigned char out[16];
  EVP_PKEY *key;
  device_state_t *d;
  size_t n;
  uint32_t crc = 0;

  test_keys(bk, sk);
  d = make_profile_device(&key, "cipher=aes256-ecb,integrity=crc32,bind_ecm_id=0,bind_cp_number=0", bk, sk, 0x0064);
  ref_hkdf(sk, "dipidescramble-ecm-enc", enc);
  ck_assert_uint_eq(ecb256_encrypt(enc, cw, 16, wire), 16u);
  {
    uint32_t c = 0xFFFFFFFFu;
    int i;
    int b;

    for (i = 0; i < 16; i++) {
      c ^= wire[i];
      for (b = 0; b < 8; b++)
        c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    crc = c ^ 0xFFFFFFFFu;
  }
  wire[16] = (unsigned char)(crc >> 24);
  wire[17] = (unsigned char)(crc >> 16);
  wire[18] = (unsigned char)(crc >> 8);
  wire[19] = (unsigned char)crc;
  n = profile_ecm(ecm, 0x0007, wire, sizeof wire);
  memset(out, 0xAA, sizeof out);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 16, TEST_ECM_PID, out), 0);
  ck_assert_mem_eq(out, cw, 16);

  ecm[5] ^= 0x01;
  memset(out, 0xAA, sizeof out);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 16, TEST_ECM_PID, out), -1);
  ck_assert_uint_eq(out[0], 0xAAu);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n - 1, 0x0064, 16, TEST_ECM_PID, out), -1);
  ck_assert_int_eq(device_resolve_cw(d, ecm, 4, 0x0064, 16, TEST_ECM_PID, out), -1);
  EVP_PKEY_free(key);
  device_state_free(d);
}
END_TEST

START_TEST(legacy_branch_rejects_a_too_short_ecm_payload) {
  EVP_PKEY *pub;
  device_state_t *d = make_device(&pub);
  unsigned char bk[CRYPTO_KEY_LEN] = {1};
  unsigned char sk[CRYPTO_KEY_LEN] = {2};
  unsigned char buf[1024];
  unsigned char out[16] = {0};
  size_t n;

  n = build_emm_u(pub, bk, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);
  n = build_emm_g(bk, sk, 0x0064, buf, sizeof buf);
  ck_assert_int_eq(device_on_emm(d, buf, n), 1);
  n = build_ecm(sk, out, 8, buf, sizeof buf);
  ck_assert_int_eq(device_resolve_cw(d, buf, 5 + CRYPTO_CW_ENC_LEN - 1, 0x0064, 8, TEST_ECM_PID, out), -1);
  ck_assert_int_eq(device_resolve_cw(d, buf, 4, 0x0064, 8, TEST_ECM_PID, out), -1);
  EVP_PKEY_free(pub);
  device_state_free(d);
}
END_TEST

static Suite *device_suite(void) {
  Suite *s = suite_create("device");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, full_chain_csa2_recovers_cw);
  tcase_add_test(tc, resolve_cw_ignores_srvid_mismatch_with_one_cached_service);
  tcase_add_test(tc, max_services_override_drops_emm_g_past_cap);
  tcase_add_test(tc, full_chain_cissa_recovers_cw);
  tcase_add_test(tc, resolve_cw_fails_for_unknown_service);
  tcase_add_test(tc, resolve_cw_fails_before_any_emm_g);
  tcase_add_test(tc, resolve_cw_rejects_bad_cw_len);
  tcase_add_test(tc, emm_g_ignored_before_emm_u_sets_bk);
  tcase_add_test(tc, emm_u_for_another_serial_is_ignored);
  tcase_add_test(tc, device_state_new_rejects_empty_serial);
  tcase_add_test(tc, profile_branch_decrypts_a_cissa_cw_through_hkdf_derived_key);
  tcase_add_test(tc, profile_branch_duplicates_a_csa2_cw_into_both_halves);
  tcase_add_test(tc, profile_branch_applies_only_the_first_combo_of_a_cw_group);
  tcase_add_test(tc, profile_branch_uses_the_ecm_pid_as_the_ecm_id_fallback_for_binding);
  tcase_add_test(tc, profile_branch_failure_leaves_the_output_untouched);
  tcase_add_test(tc, legacy_branch_rejects_a_too_short_ecm_payload);
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
