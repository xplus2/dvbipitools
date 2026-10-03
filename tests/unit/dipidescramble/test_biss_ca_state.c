/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/cas/biss/ca.h"
#include "lib/cas/biss/ca_sections.h"

#include "dipidescramble/biss_ca_state.h"

static char g_priv_path[] = "/tmp/dipidescramble_test_biss_ca_priv_XXXXXX";
static char g_pub_path[] = "/tmp/dipidescramble_test_biss_ca_pub_XXXXXX";

static void gen_keypair(void) {
  char cmd[1024];
  int fd;

  strcpy(g_priv_path, "/tmp/dipidescramble_test_biss_ca_priv_XXXXXX");
  fd = mkstemp(g_priv_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  strcpy(g_pub_path, "/tmp/dipidescramble_test_biss_ca_pub_XXXXXX");
  fd = mkstemp(g_pub_path);
  ck_assert_int_ge(fd, 0);
  close(fd);

  snprintf(cmd, sizeof cmd, "openssl genrsa -out %s 2048 2>/dev/null", g_priv_path);
  ck_assert_int_eq(system(cmd), 0);
  snprintf(cmd, sizeof cmd, "openssl rsa -in %s -pubout -out %s 2>/dev/null", g_priv_path, g_pub_path);
  ck_assert_int_eq(system(cmd), 0);
}

static void cleanup_keypair(void) {
  remove(g_priv_path);
  remove(g_pub_path);
}

static size_t build_test_emm(unsigned esid, const unsigned char ekid[BISS_CA_EKID_LEN], const biss_ca_key_t *pub, const unsigned char sk[BISS_CA_SK_LEN], int parity, unsigned char *out, size_t cap) {
  unsigned char session_data[BISS_CA_SESSION_DATA_LEN];
  biss_ca_emm_entry_t entry;

  ck_assert_uint_eq(biss_ca_build_session_data(sk, parity, session_data, sizeof session_data), BISS_CA_SESSION_DATA_LEN);
  memcpy(entry.entitlement_key_id, ekid, BISS_CA_EKID_LEN);
  ck_assert_int_eq(biss_ca_rsa_encrypt(pub, session_data, sizeof session_data, entry.encrypted_session_data), 0);
  return biss_ca_build_emm_section(0x81, esid, 0, 0, 0, 1, 0x81, &entry, 1, out, cap);
}

static size_t build_test_ecm(unsigned esid, const unsigned char sk[BISS_CA_SK_LEN], int parity, const unsigned char sw_even[BISS_CA_SW_LEN], const unsigned char sw_odd[BISS_CA_SW_LEN], unsigned char *out, size_t cap) {
  unsigned char iv[BISS_CA_IV_LEN], esw_even[BISS_CA_SW_LEN], esw_odd[BISS_CA_SW_LEN];

  ck_assert_int_eq(biss_ca_random(iv, sizeof iv), 0);
  ck_assert_int_eq(biss_ca_aes_cbc_encrypt(sk, iv, sw_even, esw_even), 0);
  ck_assert_int_eq(biss_ca_aes_cbc_encrypt(sk, iv, sw_odd, esw_odd), 0);
  return biss_ca_build_ecm_section(esid, 0, 0, 0, 1, parity, iv, esw_even, esw_odd, out, cap);
}

static biss_ca_state_t *state_from(const char *path) {
  return biss_ca_state_new(&path, 1);
}

START_TEST(new_rejects_missing_file) {
  ck_assert_ptr_null(state_from("/nonexistent/path.pem"));
  ck_assert_ptr_null(biss_ca_state_new(NULL, 0));
}
END_TEST

START_TEST(emm_matching_ekid_updates_sk_and_ecm_decodes) {
  biss_ca_key_t *pub;
  biss_ca_state_t *s;
  unsigned char ekid[BISS_CA_EKID_LEN];
  unsigned char sk[BISS_CA_SK_LEN] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  unsigned char sw_even[BISS_CA_SW_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
  unsigned char sw_odd[BISS_CA_SW_LEN] = {0x00, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
  unsigned char emm[4096], ecm[128];
  unsigned char out_even[BISS_CA_SW_LEN], out_odd[BISS_CA_SW_LEN];
  size_t emm_len, ecm_len;

  gen_keypair();
  pub = biss_ca_key_load_public_file(g_pub_path);
  ck_assert_ptr_nonnull(pub);
  ck_assert_int_eq(biss_ca_entitlement_key_id(pub, ekid), 0);

  s = state_from(g_priv_path);
  ck_assert_ptr_nonnull(s);

  emm_len = build_test_emm(0x1234, ekid, pub, sk, 0, emm, sizeof emm);
  ck_assert_uint_gt(emm_len, 0u);
  ck_assert_int_eq(biss_ca_state_on_emm(s, emm, emm_len), 1);
  /* same content again: unchanged */
  ck_assert_int_eq(biss_ca_state_on_emm(s, emm, emm_len), 0);

  ecm_len = build_test_ecm(0x1234, sk, 0, sw_even, sw_odd, ecm, sizeof ecm);
  ck_assert_uint_gt(ecm_len, 0u);
  ck_assert_int_eq(biss_ca_state_resolve_ecm(s, ecm, ecm_len, out_even, out_odd), 0);
  ck_assert_mem_eq(out_even, sw_even, BISS_CA_SW_LEN);
  ck_assert_mem_eq(out_odd, sw_odd, BISS_CA_SW_LEN);

  biss_ca_state_free(s);
  biss_ca_key_free(pub);
  cleanup_keypair();
}
END_TEST

START_TEST(emm_for_different_receiver_is_ignored) {
  biss_ca_key_t *pub;
  biss_ca_state_t *s;
  unsigned char wrong_ekid[BISS_CA_EKID_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
  unsigned char sk[BISS_CA_SK_LEN] = {0};
  unsigned char emm[4096];
  size_t emm_len;

  gen_keypair();
  pub = biss_ca_key_load_public_file(g_pub_path);
  s = state_from(g_priv_path);

  emm_len = build_test_emm(0x1234, wrong_ekid, pub, sk, 0, emm, sizeof emm);
  ck_assert_int_eq(biss_ca_state_on_emm(s, emm, emm_len), 0);

  biss_ca_state_free(s);
  biss_ca_key_free(pub);
  cleanup_keypair();
}
END_TEST

START_TEST(ecm_before_emm_has_no_sk_yet) {
  biss_ca_state_t *s;
  unsigned char sk[BISS_CA_SK_LEN] = {0};
  unsigned char sw[BISS_CA_SW_LEN] = {0};
  unsigned char ecm[128];
  unsigned char out_even[BISS_CA_SW_LEN], out_odd[BISS_CA_SW_LEN];
  size_t ecm_len;

  gen_keypair();
  s = state_from(g_priv_path);

  ecm_len = build_test_ecm(0x1234, sk, 0, sw, sw, ecm, sizeof ecm);
  ck_assert_int_eq(biss_ca_state_resolve_ecm(s, ecm, ecm_len, out_even, out_odd), -1);

  biss_ca_state_free(s);
  cleanup_keypair();
}
END_TEST

START_TEST(ecm_with_mismatched_esid_is_rejected) {
  biss_ca_key_t *pub;
  biss_ca_state_t *s;
  unsigned char ekid[BISS_CA_EKID_LEN];
  unsigned char sk[BISS_CA_SK_LEN] = {0};
  unsigned char sw[BISS_CA_SW_LEN] = {0};
  unsigned char emm[4096], ecm[128];
  unsigned char out_even[BISS_CA_SW_LEN], out_odd[BISS_CA_SW_LEN];
  size_t emm_len, ecm_len;

  gen_keypair();
  pub = biss_ca_key_load_public_file(g_pub_path);
  ck_assert_int_eq(biss_ca_entitlement_key_id(pub, ekid), 0);
  s = state_from(g_priv_path);

  emm_len = build_test_emm(0x1234, ekid, pub, sk, 0, emm, sizeof emm);
  ck_assert_int_eq(biss_ca_state_on_emm(s, emm, emm_len), 1); /* learns esid=0x1234 */

  ecm_len = build_test_ecm(0x5678, sk, 0, sw, sw, ecm, sizeof ecm);
  ck_assert_int_eq(biss_ca_state_resolve_ecm(s, ecm, ecm_len, out_even, out_odd), -1);

  biss_ca_state_free(s);
  biss_ca_key_free(pub);
  cleanup_keypair();
}
END_TEST

static char g_dir[] = "/tmp/dipidescramble_test_biss_ca_dir_XXXXXX";

static void gen_two_keys(void) {
  char cmd[1500];

  strcpy(g_dir, "/tmp/dipidescramble_test_biss_ca_dir_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  snprintf(cmd, sizeof cmd,
    "openssl genrsa -out %s/k1.pem 2048 2>/dev/null && openssl genrsa -out %s/k2.pem 2048 2>/dev/null && "
    "openssl rsa -in %s/k1.pem -pubout -out %s.pub1 2>/dev/null && openssl rsa -in %s/k2.pem -pubout -out %s.pub2 2>/dev/null",
    g_dir, g_dir, g_dir, g_dir, g_dir, g_dir);
  ck_assert_int_eq(system(cmd), 0);
}

static void cleanup_two_keys(void) {
  char cmd[600];

  snprintf(cmd, sizeof cmd, "rm -rf %s %s.pub1 %s.pub2", g_dir, g_dir, g_dir);
  ck_assert_int_eq(system(cmd), 0);
}

static int emm_for_key_is_taken(biss_ca_state_t *s, int which) {
  char pubpath[600];
  biss_ca_key_t *pub;
  unsigned char ekid[BISS_CA_EKID_LEN];
  unsigned char sk[BISS_CA_SK_LEN] = {9};
  unsigned char emm[4096];
  size_t len;
  int rc;

  snprintf(pubpath, sizeof pubpath, "%s.pub%d", g_dir, which);
  pub = biss_ca_key_load_public_file(pubpath);
  ck_assert_ptr_nonnull(pub);
  ck_assert_int_eq(biss_ca_entitlement_key_id(pub, ekid), 0);
  len = build_test_emm(0x1234, ekid, pub, sk, 0, emm, sizeof emm);
  rc = biss_ca_state_on_emm(s, emm, len);
  biss_ca_key_free(pub);
  return rc;
}

START_TEST(directory_loads_every_private_key) {
  biss_ca_state_t *s;

  gen_two_keys();
  s = state_from(g_dir);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(emm_for_key_is_taken(s, 2), 1);
  biss_ca_state_free(s);

  s = state_from(g_dir);
  ck_assert_int_eq(emm_for_key_is_taken(s, 1), 1);
  biss_ca_state_free(s);
  cleanup_two_keys();
}
END_TEST

START_TEST(repeated_paths_load_every_key) {
  biss_ca_state_t *s;
  char k1[600], k2[600];
  const char *paths[2];

  gen_two_keys();
  snprintf(k1, sizeof k1, "%s/k1.pem", g_dir);
  snprintf(k2, sizeof k2, "%s/k2.pem", g_dir);
  paths[0] = k1;
  paths[1] = k2;
  s = biss_ca_state_new(paths, 2);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(emm_for_key_is_taken(s, 2), 1);
  biss_ca_state_free(s);
  cleanup_two_keys();
}
END_TEST

START_TEST(multi_key_pem_file_loads_every_key) {
  biss_ca_state_t *s;
  char cmd[1500], both[600];

  gen_two_keys();
  snprintf(both, sizeof both, "%s.both", g_dir);
  snprintf(cmd, sizeof cmd, "cat %s/k1.pem %s/k2.pem > %s", g_dir, g_dir, both);
  ck_assert_int_eq(system(cmd), 0);
  s = state_from(both);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(emm_for_key_is_taken(s, 2), 1);
  biss_ca_state_free(s);
  remove(both);
  cleanup_two_keys();
}
END_TEST

START_TEST(unusable_path_among_good_ones_is_skipped) {
  biss_ca_state_t *s;
  char k2[600];
  const char *paths[2];

  gen_two_keys();
  snprintf(k2, sizeof k2, "%s/k2.pem", g_dir);
  paths[0] = "/nonexistent/path.pem";
  paths[1] = k2;
  s = biss_ca_state_new(paths, 2);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(emm_for_key_is_taken(s, 1), 0);
  ck_assert_int_eq(emm_for_key_is_taken(s, 2), 1);
  biss_ca_state_free(s);
  cleanup_two_keys();
}
END_TEST

static Suite *biss_ca_state_suite(void) {
  Suite *s = suite_create("biss_ca_state");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, new_rejects_missing_file);
  tcase_add_test(tc, emm_matching_ekid_updates_sk_and_ecm_decodes);
  tcase_add_test(tc, emm_for_different_receiver_is_ignored);
  tcase_add_test(tc, ecm_before_emm_has_no_sk_yet);
  tcase_add_test(tc, ecm_with_mismatched_esid_is_rejected);
  tcase_add_test(tc, directory_loads_every_private_key);
  tcase_add_test(tc, repeated_paths_load_every_key);
  tcase_add_test(tc, multi_key_pem_file_loads_every_key);
  tcase_add_test(tc, unusable_path_among_good_ones_is_skipped);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(biss_ca_state_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
