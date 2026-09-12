/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "dipixy/ts/lcevcselect.h"

START_TEST(missing_param_is_full) {
  lcevc_select_t sel;
  lcevc_select_parse_query("filter=101", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_FULL);
}
END_TEST

START_TEST(null_query_is_full) {
  lcevc_select_t sel;
  lcevc_select_parse_query(NULL, &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_FULL);
}
END_TEST

START_TEST(base_parses) {
  lcevc_select_t sel;
  lcevc_select_parse_query("lcevc=base", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_BASE);
}
END_TEST

START_TEST(full_parses) {
  lcevc_select_t sel;
  lcevc_select_parse_query("lcevc=full", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_FULL);
}
END_TEST

START_TEST(all_parses) {
  lcevc_select_t sel;
  lcevc_select_parse_query("lcevc=all", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_ALL);
}
END_TEST

START_TEST(single_digit_decimal_is_index) {
  lcevc_select_t sel;
  lcevc_select_parse_query("lcevc=2", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_N);
  ck_assert_uint_eq(sel.n, 2u);
  ck_assert_int_eq(sel.n_is_pid, 0);
}
END_TEST

START_TEST(multi_digit_decimal_is_pid) {
  lcevc_select_t sel;
  lcevc_select_parse_query("lcevc=101", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_N);
  ck_assert_uint_eq(sel.n, 101u);
  ck_assert_int_eq(sel.n_is_pid, 1);
}
END_TEST

START_TEST(hex_value_is_pid_even_single_digit) {
  lcevc_select_t sel;
  lcevc_select_parse_query("lcevc=0x5", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_N);
  ck_assert_uint_eq(sel.n, 5u);
  ck_assert_int_eq(sel.n_is_pid, 1);
}
END_TEST

START_TEST(unparsable_value_is_full) {
  lcevc_select_t sel;
  lcevc_select_parse_query("lcevc=xyz", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_FULL);
}
END_TEST

START_TEST(lcevc_among_other_params_parses) {
  lcevc_select_t sel;
  lcevc_select_parse_query("filter=101&lcevc=base&pmt=0x1000", &sel);
  ck_assert_int_eq(sel.mode, LCEVC_SEL_BASE);
}
END_TEST

START_TEST(resolve_base_is_none) {
  lcevc_select_t sel = {LCEVC_SEL_BASE, 0, 0};
  unsigned pids[2] = {101, 202};
  lcevc_resolved_t r = lcevc_select_resolve(&sel, pids, 2);
  ck_assert_int_eq(r.kind, LCEVC_RESOLVE_NONE);
}
END_TEST

START_TEST(resolve_full_is_all) {
  lcevc_select_t sel = {LCEVC_SEL_FULL, 0, 0};
  unsigned pids[2] = {101, 202};
  lcevc_resolved_t r = lcevc_select_resolve(&sel, pids, 2);
  ck_assert_int_eq(r.kind, LCEVC_RESOLVE_ALL);
}
END_TEST

START_TEST(resolve_all_is_all) {
  lcevc_select_t sel = {LCEVC_SEL_ALL, 0, 0};
  unsigned pids[2] = {101, 202};
  lcevc_resolved_t r = lcevc_select_resolve(&sel, pids, 2);
  ck_assert_int_eq(r.kind, LCEVC_RESOLVE_ALL);
}
END_TEST

START_TEST(resolve_n_by_index_picks_that_pid) {
  lcevc_select_t sel = {LCEVC_SEL_N, 1, 0};
  unsigned pids[2] = {101, 202};
  lcevc_resolved_t r = lcevc_select_resolve(&sel, pids, 2);
  ck_assert_int_eq(r.kind, LCEVC_RESOLVE_ONE);
  ck_assert_uint_eq(r.pid, 202u);
}
END_TEST

START_TEST(resolve_n_by_pid_picks_matching_entry) {
  lcevc_select_t sel = {LCEVC_SEL_N, 101, 1};
  unsigned pids[2] = {101, 202};
  lcevc_resolved_t r = lcevc_select_resolve(&sel, pids, 2);
  ck_assert_int_eq(r.kind, LCEVC_RESOLVE_ONE);
  ck_assert_uint_eq(r.pid, 101u);
}
END_TEST

START_TEST(resolve_n_out_of_range_index_falls_back_to_all) {
  lcevc_select_t sel = {LCEVC_SEL_N, 5, 0};
  unsigned pids[2] = {101, 202};
  lcevc_resolved_t r = lcevc_select_resolve(&sel, pids, 2);
  ck_assert_int_eq(r.kind, LCEVC_RESOLVE_ALL);
}
END_TEST

START_TEST(resolve_n_unmatched_pid_falls_back_to_all) {
  lcevc_select_t sel = {LCEVC_SEL_N, 999, 1};
  unsigned pids[2] = {101, 202};
  lcevc_resolved_t r = lcevc_select_resolve(&sel, pids, 2);
  ck_assert_int_eq(r.kind, LCEVC_RESOLVE_ALL);
}
END_TEST

START_TEST(equal_matches_same_mode_n_and_form) {
  lcevc_select_t a = {LCEVC_SEL_N, 2, 0};
  lcevc_select_t b = {LCEVC_SEL_N, 2, 0};
  ck_assert_int_eq(lcevc_select_equal(&a, &b), 1);
}
END_TEST

START_TEST(equal_rejects_different_n) {
  lcevc_select_t a = {LCEVC_SEL_N, 2, 0};
  lcevc_select_t b = {LCEVC_SEL_N, 3, 0};
  ck_assert_int_eq(lcevc_select_equal(&a, &b), 0);
}
END_TEST

START_TEST(equal_rejects_same_n_different_form) {
  lcevc_select_t a = {LCEVC_SEL_N, 2, 0};
  lcevc_select_t b = {LCEVC_SEL_N, 2, 1};
  ck_assert_int_eq(lcevc_select_equal(&a, &b), 0);
}
END_TEST

START_TEST(equal_rejects_different_mode_even_with_same_n) {
  lcevc_select_t full_sel = {LCEVC_SEL_FULL, 0, 0};
  lcevc_select_t n_sel = {LCEVC_SEL_N, 0, 0};
  ck_assert_int_eq(lcevc_select_equal(&full_sel, &n_sel), 0);
}
END_TEST

START_TEST(equal_treats_all_as_distinct_from_full) {
  lcevc_select_t full_sel = {LCEVC_SEL_FULL, 0, 0};
  lcevc_select_t all_sel = {LCEVC_SEL_ALL, 0, 0};
  ck_assert_int_eq(lcevc_select_equal(&full_sel, &all_sel), 0);
}
END_TEST

static Suite *lcevcselect_suite(void) {
  Suite *s = suite_create("dipixy_lcevcselect");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, missing_param_is_full);
  tcase_add_test(tc, null_query_is_full);
  tcase_add_test(tc, base_parses);
  tcase_add_test(tc, full_parses);
  tcase_add_test(tc, all_parses);
  tcase_add_test(tc, single_digit_decimal_is_index);
  tcase_add_test(tc, multi_digit_decimal_is_pid);
  tcase_add_test(tc, hex_value_is_pid_even_single_digit);
  tcase_add_test(tc, unparsable_value_is_full);
  tcase_add_test(tc, lcevc_among_other_params_parses);
  tcase_add_test(tc, resolve_base_is_none);
  tcase_add_test(tc, resolve_full_is_all);
  tcase_add_test(tc, resolve_all_is_all);
  tcase_add_test(tc, resolve_n_by_index_picks_that_pid);
  tcase_add_test(tc, resolve_n_by_pid_picks_matching_entry);
  tcase_add_test(tc, resolve_n_out_of_range_index_falls_back_to_all);
  tcase_add_test(tc, resolve_n_unmatched_pid_falls_back_to_all);
  tcase_add_test(tc, equal_matches_same_mode_n_and_form);
  tcase_add_test(tc, equal_rejects_different_n);
  tcase_add_test(tc, equal_rejects_same_n_different_form);
  tcase_add_test(tc, equal_rejects_different_mode_even_with_same_n);
  tcase_add_test(tc, equal_treats_all_as_distinct_from_full);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(lcevcselect_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
