/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "dipixy/ts/pidfilter.h"

START_TEST(decimal_list_parses) {
  pid_filter_t f;
  pid_filter_parse("101,256,32", &f);
  ck_assert_int_eq(f.count, 3);
  ck_assert_uint_eq(f.pids[0], 32u);
  ck_assert_uint_eq(f.pids[1], 101u);
  ck_assert_uint_eq(f.pids[2], 256u);
}
END_TEST

START_TEST(hex_and_mixed_list_parses) {
  pid_filter_t f;
  pid_filter_parse("101,0x20,0X100", &f);
  ck_assert_int_eq(f.count, 3);
  ck_assert_uint_eq(f.pids[0], 0x20u);
  ck_assert_uint_eq(f.pids[1], 101u);
  ck_assert_uint_eq(f.pids[2], 0x100u);
}
END_TEST

START_TEST(space_separated_list_parses) {
  pid_filter_t f;
  pid_filter_parse("101 256 32", &f);
  ck_assert_int_eq(f.count, 3);
}
END_TEST

START_TEST(out_of_range_token_skipped) {
  pid_filter_t f;
  pid_filter_parse("101,9000,256", &f);
  ck_assert_int_eq(f.count, 2);
  ck_assert_uint_eq(f.pids[0], 101u);
  ck_assert_uint_eq(f.pids[1], 256u);
}
END_TEST

START_TEST(unparsable_token_skipped) {
  pid_filter_t f;
  pid_filter_parse("101,xyz,256", &f);
  ck_assert_int_eq(f.count, 2);
  ck_assert_uint_eq(f.pids[0], 101u);
  ck_assert_uint_eq(f.pids[1], 256u);
}
END_TEST

START_TEST(duplicates_deduped) {
  pid_filter_t f;
  pid_filter_parse("101,0x65,256", &f);
  ck_assert_int_eq(f.count, 2);
  ck_assert_uint_eq(f.pids[0], 101u);
  ck_assert_uint_eq(f.pids[1], 256u);
}
END_TEST

START_TEST(null_value_leaves_empty) {
  pid_filter_t f;
  pid_filter_parse(NULL, &f);
  ck_assert_int_eq(f.count, 0);
}
END_TEST

START_TEST(excludes_matches_present_pid_only) {
  pid_filter_t f;
  pid_filter_parse("101,0x20", &f);
  ck_assert_int_eq(pid_filter_excludes(&f, 101), 1);
  ck_assert_int_eq(pid_filter_excludes(&f, 0x20), 1);
  ck_assert_int_eq(pid_filter_excludes(&f, 42), 0);
}
END_TEST

START_TEST(equal_ignores_input_order) {
  pid_filter_t a, b;
  pid_filter_parse("101,0x20", &a);
  pid_filter_parse("32,101", &b);
  ck_assert_int_eq(pid_filter_equal(&a, &b), 1);
}
END_TEST

START_TEST(equal_rejects_different_sets) {
  pid_filter_t a, b;
  pid_filter_parse("101,0x20", &a);
  pid_filter_parse("101", &b);
  ck_assert_int_eq(pid_filter_equal(&a, &b), 0);
}
END_TEST

START_TEST(query_extracts_filter_param) {
  pid_filter_t f;
  pid_filter_parse_query("filter=101,0x20", &f);
  ck_assert_int_eq(f.count, 2);
}
END_TEST

START_TEST(query_stops_at_next_param) {
  pid_filter_t f;
  pid_filter_parse_query("a=1&filter=101&b=2", &f);
  ck_assert_int_eq(f.count, 1);
  ck_assert_uint_eq(f.pids[0], 101u);
}
END_TEST

START_TEST(query_without_filter_param_leaves_empty) {
  pid_filter_t f;
  pid_filter_parse_query("a=1&b=2", &f);
  ck_assert_int_eq(f.count, 0);
}
END_TEST

START_TEST(null_query_leaves_empty) {
  pid_filter_t f;
  pid_filter_parse_query(NULL, &f);
  ck_assert_int_eq(f.count, 0);
}
END_TEST

static Suite *pidfilter_suite(void) {
  Suite *s = suite_create("dipixy_pidfilter");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, decimal_list_parses);
  tcase_add_test(tc, hex_and_mixed_list_parses);
  tcase_add_test(tc, space_separated_list_parses);
  tcase_add_test(tc, out_of_range_token_skipped);
  tcase_add_test(tc, unparsable_token_skipped);
  tcase_add_test(tc, duplicates_deduped);
  tcase_add_test(tc, null_value_leaves_empty);
  tcase_add_test(tc, excludes_matches_present_pid_only);
  tcase_add_test(tc, equal_ignores_input_order);
  tcase_add_test(tc, equal_rejects_different_sets);
  tcase_add_test(tc, query_extracts_filter_param);
  tcase_add_test(tc, query_stops_at_next_param);
  tcase_add_test(tc, query_without_filter_param_leaves_empty);
  tcase_add_test(tc, null_query_leaves_empty);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pidfilter_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
