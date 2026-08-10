/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/cas/cas_group.h"

START_TEST(fallback_no_vendors_is_active) {
  ck_assert_int_eq(cas_group_fallback_active_calc(0, NULL, NULL), 1);
}
END_TEST

START_TEST(fallback_all_alive_none_required_is_inactive) {
  int required[3] = {0, 0, 0};
  int alive[3] = {1, 1, 1};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_one_alive_none_required_is_inactive) {
  int required[3] = {0, 0, 0};
  int alive[3] = {0, 1, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_zero_alive_none_required_is_active) {
  int required[3] = {0, 0, 0};
  int alive[3] = {0, 0, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 1);
}
END_TEST

START_TEST(fallback_required_down_others_alive_is_active) {
  int required[3] = {1, 0, 0};
  int alive[3] = {0, 1, 1};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 1);
}
END_TEST

START_TEST(fallback_required_alive_others_down_is_inactive) {
  int required[3] = {1, 0, 0};
  int alive[3] = {1, 0, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_all_required_all_alive_is_inactive) {
  int required[3] = {1, 1, 1};
  int alive[3] = {1, 1, 1};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_one_of_two_required_down_is_active) {
  int required[2] = {1, 1};
  int alive[2] = {1, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(2, required, alive), 1);
}
END_TEST

START_TEST(csa1_checksum_matches_known_values) {
  /* k0,k1,k2 = 0x01,0x02,0x03 -> checksum 0x06; k4,k5,k6 = 0x10,0x20,0x30 -> checksum 0x60 */
  unsigned char cw[8] = {0x01, 0x02, 0x03, 0xAA, 0x10, 0x20, 0x30, 0xAA};
  csa1_apply_cw_checksum(cw);
  ck_assert_uint_eq(cw[3], 0x06);
  ck_assert_uint_eq(cw[7], 0x60);
  /* free bytes untouched */
  ck_assert_uint_eq(cw[0], 0x01);
  ck_assert_uint_eq(cw[1], 0x02);
  ck_assert_uint_eq(cw[2], 0x03);
  ck_assert_uint_eq(cw[4], 0x10);
  ck_assert_uint_eq(cw[5], 0x20);
  ck_assert_uint_eq(cw[6], 0x30);
}
END_TEST

START_TEST(csa1_checksum_wraps_modulo_256) {
  unsigned char cw[8] = {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  csa1_apply_cw_checksum(cw);
  ck_assert_uint_eq(cw[3], (unsigned char)((0xFF + 0xFF + 0xFF) % 256));
  ck_assert_uint_eq(cw[7], (unsigned char)((0xFF + 0xFF + 0xFF) % 256));
}
END_TEST

static Suite *cas_group_suite(void) {
  Suite *s = suite_create("cas_group");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fallback_no_vendors_is_active);
  tcase_add_test(tc, fallback_all_alive_none_required_is_inactive);
  tcase_add_test(tc, fallback_one_alive_none_required_is_inactive);
  tcase_add_test(tc, fallback_zero_alive_none_required_is_active);
  tcase_add_test(tc, fallback_required_down_others_alive_is_active);
  tcase_add_test(tc, fallback_required_alive_others_down_is_inactive);
  tcase_add_test(tc, fallback_all_required_all_alive_is_inactive);
  tcase_add_test(tc, fallback_one_of_two_required_down_is_active);
  tcase_add_test(tc, csa1_checksum_matches_known_values);
  tcase_add_test(tc, csa1_checksum_wraps_modulo_256);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cas_group_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
