/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "dipixy/reactor/reactor.h"

START_TEST(positive_spec_is_itself) {
  ck_assert_int_eq(reactor_resolve_workers(4, 8), 4);
}
END_TEST

START_TEST(zero_spec_is_zero) {
  ck_assert_int_eq(reactor_resolve_workers(0, 8), 0);
}
END_TEST

START_TEST(negative_spec_multiplies_by_ncpu) {
  ck_assert_int_eq(reactor_resolve_workers(-2, 4), 8);
  ck_assert_int_eq(reactor_resolve_workers(-1, 4), 4);
  ck_assert_int_eq(reactor_resolve_workers(-3, 2), 6);
}
END_TEST

START_TEST(negative_spec_clamps_ncpu_to_at_least_one) {
  ck_assert_int_eq(reactor_resolve_workers(-2, 0), 2);
  ck_assert_int_eq(reactor_resolve_workers(-2, -5), 2);
}
END_TEST

static Suite *reactor_suite(void) {
  Suite *s = suite_create("dipixy_reactor");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, positive_spec_is_itself);
  tcase_add_test(tc, zero_spec_is_zero);
  tcase_add_test(tc, negative_spec_multiplies_by_ncpu);
  tcase_add_test(tc, negative_spec_clamps_ncpu_to_at_least_one);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(reactor_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
