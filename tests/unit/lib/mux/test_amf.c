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

START_TEST(amf_skip_value_object_with_nested_values) {
  ebuf_t b = {0};
  amf_object_start(&b);
  amf_object_key(&b, "n");
  amf_number(&b, 1.5);
  amf_object_key(&b, "s");
  amf_string(&b, "hi");
  amf_object_key(&b, "o");
  amf_object_start(&b);
  amf_object_key(&b, "b");
  amf_boolean(&b, 1);
  amf_object_end(&b);
  amf_object_end(&b);
  ck_assert_ptr_eq(amf_skip_value(b.p, b.p + b.len), b.p + b.len);
  ebuf_free(&b);
}
END_TEST

START_TEST(amf_skip_value_ecma_array) {
  ebuf_t b = {0};
  amf_ecma_array_start(&b, 2);
  amf_object_key(&b, "a");
  amf_null(&b);
  amf_object_key(&b, "b");
  amf_number(&b, 2.0);
  amf_ecma_array_end(&b);
  ck_assert_ptr_eq(amf_skip_value(b.p, b.p + b.len), b.p + b.len);
  ebuf_free(&b);
}
END_TEST

START_TEST(amf_skip_value_truncated_object_at_every_length) {
  ebuf_t b = {0};
  amf_object_start(&b);
  amf_object_key(&b, "k");
  amf_string(&b, "v");
  amf_object_end(&b);
  for (size_t n = 0; n < b.len; n++) ck_assert_ptr_null(amf_skip_value(b.p, b.p + n));
  ebuf_free(&b);
}
END_TEST

START_TEST(amf_skip_value_truncated_ecma_array_at_every_length) {
  ebuf_t b = {0};
  amf_ecma_array_start(&b, 1);
  amf_object_key(&b, "k");
  amf_boolean(&b, 0);
  amf_ecma_array_end(&b);
  for (size_t n = 0; n < b.len; n++) ck_assert_ptr_null(amf_skip_value(b.p, b.p + n));
  ebuf_free(&b);
}
END_TEST

START_TEST(amf_skip_value_object_without_terminator_marker) {
  unsigned char buf[3] = {0x03, 0x00, 0x00};
  ck_assert_ptr_null(amf_skip_value(buf, buf + sizeof buf));
}
END_TEST

START_TEST(amf_skip_value_object_with_unknown_member_type) {
  unsigned char buf[8] = {0x03, 0x00, 0x01, 'k', 0x7F, 0x00, 0x00, 0x09};
  ck_assert_ptr_null(amf_skip_value(buf, buf + sizeof buf));
}
END_TEST

static Suite *amf_suite(void) {
  Suite *s = suite_create("amf");
  TCase *tc = tcase_create("skip_value");
  tcase_add_test(tc, amf_skip_value_long_string_rejects_oversized_length);
  tcase_add_test(tc, amf_skip_value_long_string_accepts_exact_fit);
  tcase_add_test(tc, amf_skip_value_long_string_rejects_truncated_header);
  tcase_add_test(tc, amf_skip_value_object_with_nested_values);
  tcase_add_test(tc, amf_skip_value_ecma_array);
  tcase_add_test(tc, amf_skip_value_truncated_object_at_every_length);
  tcase_add_test(tc, amf_skip_value_truncated_ecma_array_at_every_length);
  tcase_add_test(tc, amf_skip_value_object_without_terminator_marker);
  tcase_add_test(tc, amf_skip_value_object_with_unknown_member_type);
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
