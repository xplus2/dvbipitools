/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/cas/biss/ca.h"

static int visit(biss_ca_key_t *k, void *ctx) {
  (void)k;
  (void)ctx;
  return 0;
}

START_TEST(every_entry_point_reports_unavailable) {
  unsigned char out[BISS_CA_RSA_BYTES];
  unsigned char sk[BISS_CA_SK_LEN] = {0};
  unsigned char iv[BISS_CA_IV_LEN] = {0};
  unsigned char sw[BISS_CA_SW_LEN] = {0};
  unsigned char ekid[BISS_CA_EKID_LEN];
  size_t olen = 0;
  ck_assert_ptr_null(biss_ca_key_load_public_file("/nonexistent.pem"));
  ck_assert_ptr_null(biss_ca_key_load_private_file("/nonexistent.pem"));
  ck_assert_ptr_null(biss_ca_key_load_public_mem("x", 1));
  ck_assert_ptr_null(biss_ca_key_load_private_mem("x", 1));
  biss_ca_key_free(NULL);
  ck_assert_int_eq(biss_ca_key_foreach_file("/nonexistent.pem", 0, visit, NULL), -1);
  ck_assert_int_eq(biss_ca_key_foreach_mem("x", 1, 1, visit, NULL), -1);
  ck_assert_int_eq(biss_ca_entitlement_key_id(NULL, ekid), -1);
  ck_assert_int_eq(biss_ca_rsa_encrypt(NULL, sw, sizeof sw, out), -1);
  ck_assert_int_eq(biss_ca_rsa_decrypt(NULL, out, sw, sizeof sw, &olen), -1);
  ck_assert_int_eq(biss_ca_aes_cbc_encrypt(sk, iv, sw, out), -1);
  ck_assert_int_eq(biss_ca_aes_cbc_decrypt(sk, iv, sw, out), -1);
  ck_assert_int_eq(biss_ca_random(out, sizeof out), -1);
}
END_TEST

static Suite *biss_ca_stub_suite(void) {
  Suite *s = suite_create("biss_ca_stub");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, every_entry_point_reports_unavailable);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(biss_ca_stub_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
