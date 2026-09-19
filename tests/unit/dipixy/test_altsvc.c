/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/altsvc.h"

START_TEST(off_by_default) {
  altsvc_set(0);
  ck_assert_ptr_eq(altsvc_value(), NULL);
  ck_assert_str_eq(altsvc_h1_line(1), "");
  ck_assert_str_eq(altsvc_h1_line(0), "");
}
END_TEST

START_TEST(enabled_announces_port) {
  altsvc_set(9443);
  ck_assert_str_eq(altsvc_value(), "h3=\":9443\"; ma=86400");
  ck_assert_str_eq(altsvc_h1_line(1), "Alt-Svc: h3=\":9443\"; ma=86400\r\n");
}
END_TEST

START_TEST(plain_connection_gets_no_line) {
  altsvc_set(443);
  ck_assert_str_eq(altsvc_h1_line(0), "");
}
END_TEST

START_TEST(max_port_fits) {
  altsvc_set(65535);
  ck_assert_str_eq(altsvc_value(), "h3=\":65535\"; ma=86400");
}
END_TEST

START_TEST(reset_disables_again) {
  altsvc_set(443);
  altsvc_set(0);
  ck_assert_ptr_eq(altsvc_value(), NULL);
  ck_assert_str_eq(altsvc_h1_line(1), "");
}
END_TEST

static Suite *altsvc_suite(void) {
  Suite *s = suite_create("dipixy_altsvc");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, off_by_default);
  tcase_add_test(tc, enabled_announces_port);
  tcase_add_test(tc, plain_connection_gets_no_line);
  tcase_add_test(tc, max_port_fits);
  tcase_add_test(tc, reset_disables_again);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(altsvc_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
