/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/ecmg_client/cw_encryption.h"

START_TEST(every_entry_point_reports_unavailable) {
  cwenc_config_t cfg;
  cwenc_selection_t sel;
  unsigned char key[CWENC_DES_KEY_LEN] = {0};
  unsigned char blk[CWENC_AES_BLOCK_LEN] = {0};
  unsigned char out[CWENC_AES_BLOCK_LEN * 2];
  unsigned char mac[CWENC_HMAC_SHA256_LEN];
  unsigned char cw[16] = {0};
  memset(&cfg, 0, sizeof cfg);
  memset(&sel, 0, sizeof sel);
  ck_assert_int_eq(cwenc_des_ecb_encrypt(key, cw, 8, out), -1);
  ck_assert_int_eq(cwenc_aes_ecb_encrypt(128, key, blk, out), -1);
  ck_assert_int_eq(cwenc_aes_ctr_xcrypt(128, key, blk, cw, out, sizeof cw), -1);
  ck_assert_int_eq(cwenc_hkdf_sha256(key, sizeof key, blk, sizeof blk, mac), -1);
  cfg.algo = CWENC_ALGO_OFF;
  ck_assert_int_eq(cwenc_config_validate(&cfg, 8), 0);
  cfg.algo = CWENC_ALGO_OFF + 1;
  ck_assert_int_eq(cwenc_config_validate(&cfg, 8), -1);
  ck_assert_int_eq(cwenc_encrypt_cw(&cfg, &sel, 8, 1, 2, 3, cw), -1);
}
END_TEST

static Suite *cw_stub_suite(void) {
  Suite *s = suite_create("cw_encryption_stub");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, every_entry_point_reports_unavailable);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cw_stub_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
