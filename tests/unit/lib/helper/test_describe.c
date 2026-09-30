/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/describe.h"

START_TEST(describe_http_uri_brackets_ipv6_literal_host) {
  char buf[128];
  describe_http_uri(buf, sizeof buf, 0, "::1", 8080, "/path");
  ck_assert_str_eq(buf, "http://[::1]:8080/path");
}
END_TEST

START_TEST(describe_http_uri_leaves_ipv4_and_hostname_unbracketed) {
  char buf[128];
  describe_http_uri(buf, sizeof buf, 1, "192.0.2.1", 443, "/");
  ck_assert_str_eq(buf, "https://192.0.2.1:443/");

  describe_http_uri(buf, sizeof buf, 0, "example.invalid", 80, "/x");
  ck_assert_str_eq(buf, "http://example.invalid:80/x");
}
END_TEST

static Suite *describe_suite(void) {
  Suite *s = suite_create("describe");
  TCase *tc = tcase_create("http_uri");
  tcase_add_test(tc, describe_http_uri_brackets_ipv6_literal_host);
  tcase_add_test(tc, describe_http_uri_leaves_ipv4_and_hostname_unbracketed);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(describe_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
