/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "dipixy/ts/pmtselect.h"

START_TEST(decimal_pmt_parses) {
  ck_assert_uint_eq(pmt_select_parse_query("pmt=4096"), 4096u);
}
END_TEST

START_TEST(hex_pmt_parses) {
  ck_assert_uint_eq(pmt_select_parse_query("pmt=0x1000"), 0x1000u);
}
END_TEST

START_TEST(pmt_among_other_params_parses) {
  ck_assert_uint_eq(pmt_select_parse_query("filter=101&pmt=0x1000&x=1"), 0x1000u);
}
END_TEST

START_TEST(out_of_range_pmt_is_auto) {
  ck_assert_uint_eq(pmt_select_parse_query("pmt=9000"), 0u);
}
END_TEST

START_TEST(unparsable_pmt_is_auto) {
  ck_assert_uint_eq(pmt_select_parse_query("pmt=xyz"), 0u);
}
END_TEST

START_TEST(literal_zero_is_auto) {
  ck_assert_uint_eq(pmt_select_parse_query("pmt=0"), 0u);
}
END_TEST

START_TEST(missing_pmt_param_is_auto) {
  ck_assert_uint_eq(pmt_select_parse_query("filter=101"), 0u);
}
END_TEST

START_TEST(null_query_is_auto) {
  ck_assert_uint_eq(pmt_select_parse_query(NULL), 0u);
}
END_TEST

static Suite *pmtselect_suite(void) {
  Suite *s = suite_create("dipixy_pmtselect");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, decimal_pmt_parses);
  tcase_add_test(tc, hex_pmt_parses);
  tcase_add_test(tc, pmt_among_other_params_parses);
  tcase_add_test(tc, out_of_range_pmt_is_auto);
  tcase_add_test(tc, unparsable_pmt_is_auto);
  tcase_add_test(tc, literal_zero_is_auto);
  tcase_add_test(tc, missing_pmt_param_is_auto);
  tcase_add_test(tc, null_query_is_auto);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pmtselect_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
