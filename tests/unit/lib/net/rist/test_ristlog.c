/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/net/rist/ristlog.h"

START_TEST(returns_non_null) {
  ck_assert_ptr_nonnull(ristlog_get(0));
}
END_TEST

START_TEST(is_a_singleton_across_calls) {
  struct rist_logging_settings *a = ristlog_get(0);
  struct rist_logging_settings *b = ristlog_get(1); /* first caller's verbose wins, same pointer regardless */

  ck_assert_ptr_eq(a, b);
}
END_TEST

static Suite *ristlog_suite(void) {
  Suite *s = suite_create("ristlog");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, returns_non_null);
  tcase_add_test(tc, is_a_singleton_across_calls);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ristlog_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
