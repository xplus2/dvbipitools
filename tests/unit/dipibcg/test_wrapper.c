/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipibcg/wrapper.h"

START_TEST(wrapper_build_uncompressed_round_trips) {
  const unsigned char container[] = "some container bytes, not really TVA data";
  unsigned char *wrapped, *out;
  size_t wrapped_len, out_len;

  ck_assert_int_eq(wrapper_build(container, sizeof container, 0, &wrapped, &wrapped_len), 0);
  ck_assert_uint_eq(wrapped_len, 1 + sizeof container);
  ck_assert_int_eq(wrapped[0], WRAPPER_METHOD_NONE);

  ck_assert_int_eq(wrapper_parse(wrapped, wrapped_len, &out, &out_len), 0);
  ck_assert_uint_eq(out_len, sizeof container);
  ck_assert_mem_eq(out, container, sizeof container);

  free(wrapped);
  free(out);
}
END_TEST

START_TEST(wrapper_build_compressed_round_trips) {
  unsigned char container[2000];
  unsigned char *wrapped, *out;
  size_t wrapped_len, out_len;
  size_t i;

  for (i = 0; i < sizeof container; i++)
    container[i] = (unsigned char)(i % 7); /* repetitive, compresses well */

  ck_assert_int_eq(wrapper_build(container, sizeof container, 1, &wrapped, &wrapped_len), 0);
  ck_assert_int_eq(wrapped[0], WRAPPER_METHOD_ZLIB);
  ck_assert_uint_lt(wrapped_len, sizeof container);

  ck_assert_int_eq(wrapper_parse(wrapped, wrapped_len, &out, &out_len), 0);
  ck_assert_uint_eq(out_len, sizeof container);
  ck_assert_mem_eq(out, container, sizeof container);

  free(wrapped);
  free(out);
}
END_TEST

START_TEST(wrapper_parse_rejects_empty_buffer) {
  unsigned char *out;
  size_t out_len;
  ck_assert_int_eq(wrapper_parse(NULL, 0, &out, &out_len), -1);
}
END_TEST

START_TEST(wrapper_parse_rejects_truncated_zlib_header) {
  unsigned char buf[3] = {WRAPPER_METHOD_ZLIB, 0, 0};
  unsigned char *out;
  size_t out_len;
  ck_assert_int_eq(wrapper_parse(buf, sizeof buf, &out, &out_len), -1);
}
END_TEST

START_TEST(wrapper_parse_rejects_corrupt_zlib_stream) {
  unsigned char buf[8] = {WRAPPER_METHOD_ZLIB, 0, 0, 10, 0xFF, 0xFF, 0xFF, 0xFF};
  unsigned char *out;
  size_t out_len;
  ck_assert_int_eq(wrapper_parse(buf, sizeof buf, &out, &out_len), -1);
}
END_TEST

START_TEST(wrapper_parse_rejects_reserved_method) {
  unsigned char buf[5] = {0x03, 'a', 'b', 'c', 'd'};
  unsigned char *out;
  size_t out_len;
  ck_assert_int_eq(wrapper_parse(buf, sizeof buf, &out, &out_len), -1);
}
END_TEST

static Suite *wrapper_suite(void) {
  Suite *s = suite_create("dipibcg_wrapper");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, wrapper_build_uncompressed_round_trips);
  tcase_add_test(tc, wrapper_build_compressed_round_trips);
  tcase_add_test(tc, wrapper_parse_rejects_empty_buffer);
  tcase_add_test(tc, wrapper_parse_rejects_truncated_zlib_header);
  tcase_add_test(tc, wrapper_parse_rejects_corrupt_zlib_stream);
  tcase_add_test(tc, wrapper_parse_rejects_reserved_method);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(wrapper_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
