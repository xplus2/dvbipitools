/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/amf.h"

START_TEST(amf_skip_value_long_string_rejects_oversized_length) {
  /* AMF_T_LONG_STRING (0x0C) + 4-byte length claiming far more bytes than remain */
  unsigned char buf[9] = {0x0C, 0xFF, 0xFF, 0xFF, 0xFF, 'a', 'b', 'c', 'd'};
  const unsigned char *r = amf_skip_value(buf, buf + sizeof buf);
  ck_assert_ptr_null(r);
}
END_TEST

START_TEST(amf_skip_value_long_string_accepts_exact_fit) {
  unsigned char buf[9] = {0x0C, 0x00, 0x00, 0x00, 0x04, 'a', 'b', 'c', 'd'};
  const unsigned char *r = amf_skip_value(buf, buf + sizeof buf);
  ck_assert_ptr_eq(r, buf + sizeof buf);
}
END_TEST

START_TEST(amf_skip_value_long_string_rejects_truncated_header) {
  unsigned char buf[4] = {0x0C, 0x00, 0x00, 0x00};
  const unsigned char *r = amf_skip_value(buf, buf + sizeof buf);
  ck_assert_ptr_null(r);
}
END_TEST

static Suite *amf_suite(void) {
  Suite *s = suite_create("amf");
  TCase *tc = tcase_create("skip_value");
  tcase_add_test(tc, amf_skip_value_long_string_rejects_oversized_length);
  tcase_add_test(tc, amf_skip_value_long_string_accepts_exact_fit);
  tcase_add_test(tc, amf_skip_value_long_string_rejects_truncated_header);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(amf_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
