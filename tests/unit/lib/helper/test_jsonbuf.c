/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/jsonbuf.h"

START_TEST(raw_and_str_accumulate_including_embedded_nul) {
  jbuf_t j;

  memset(&j, 0, sizeof j);
  jbuf_str(&j, "ab");
  jbuf_raw(&j, "c\0d", 3);
  jbuf_str(&j, "e");
  ck_assert_uint_eq(j.len, 6u);
  ck_assert_mem_eq(j.buf, "abc\0de", 7);
  ck_assert_int_eq(j.failed, 0);
  free(j.buf);
}
END_TEST

typedef struct {
  unsigned long long value;
  const char *expect;
} u64_case_t;

static const u64_case_t u64_cases[] = {
    {0, "0"},
    {9, "9"},
    {10, "10"},
    {4294967295ULL, "4294967295"},
    {4294967296ULL, "4294967296"},
    {18446744073709551615ULL, "18446744073709551615"},
};

START_TEST(u64_formats_decimal) {
  const u64_case_t *c = &u64_cases[_i];
  jbuf_t j;

  memset(&j, 0, sizeof j);
  jbuf_u64(&j, c->value);
  ck_assert_str_eq(j.buf, c->expect);
  free(j.buf);
}
END_TEST

typedef struct {
  long long value;
  const char *expect;
} i64_case_t;

static const i64_case_t i64_cases[] = {
    {0, "0"},
    {1, "1"},
    {-1, "-1"},
    {-10, "-10"},
    {LLONG_MAX, "9223372036854775807"},
    {LLONG_MIN, "-9223372036854775808"},
};

START_TEST(i64_formats_signed_decimal) {
  const i64_case_t *c = &i64_cases[_i];
  jbuf_t j;

  memset(&j, 0, sizeof j);
  jbuf_i64(&j, c->value);
  ck_assert_str_eq(j.buf, c->expect);
  free(j.buf);
}
END_TEST

typedef struct {
  double value;
  const char *expect;
} fixed_case_t;

static const fixed_case_t fixed_cases[] = {
    {0.0, "0.000"},
    {1.5, "1.500"},
    {0.0004, "0.000"},
    {0.0006, "0.001"},
    {0.999, "0.999"},
    {123.456, "123.456"},
    {1000000.25, "1000000.250"},
};

START_TEST(fixed3_rounds_to_three_decimals) {
  const fixed_case_t *c = &fixed_cases[_i];
  jbuf_t j;

  memset(&j, 0, sizeof j);
  jbuf_fixed3(&j, c->value);
  ck_assert_str_eq(j.buf, c->expect);
  free(j.buf);
}
END_TEST

typedef struct {
  const char *in;
  const char *expect;
} string_case_t;

static const string_case_t string_cases[] = {
    {"", "\"\""},
    {"plain", "\"plain\""},
    {"a\"b", "\"a\\\"b\""},
    {"a\\b", "\"a\\\\b\""},
    {"l1\nl2", "\"l1\\nl2\""},
    {"\r\t", "\"\\r\\t\""},
    {"\x01", "\"\\u0001\""},
    {"bell\x07", "\"bell\\u0007\""},
    {"\x1f", "\"\\u001f\""},
    {"a/b", "\"a/b\""},
    {"\x7f", "\"\x7f\""},
    {"\xc3\xa9", "\"\xc3\xa9\""},
    {"x\"\n\"y", "\"x\\\"\\n\\\"y\""},
    {"\"", "\"\\\"\""},
};

START_TEST(json_string_escapes_and_quotes) {
  const string_case_t *c = &string_cases[_i];
  jbuf_t j;

  memset(&j, 0, sizeof j);
  jbuf_json_string(&j, c->in);
  ck_assert_str_eq(j.buf, c->expect);
  free(j.buf);
}
END_TEST

START_TEST(key_and_fmt_build_members) {
  jbuf_t j;

  memset(&j, 0, sizeof j);
  jbuf_key(&j, "k");
  jbuf_fmt(&j, "%s=%d", "n", 5);
  jbuf_key(&j, "a\"b");
  ck_assert_str_eq(j.buf, "\"k\":n=5\"a\\\"b\":");
  free(j.buf);
}
END_TEST

START_TEST(reset_clears_content_and_failure_and_allows_reuse) {
  jbuf_t j;
  char big[5000];

  memset(&j, 0, sizeof j);
  memset(big, 'x', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  jbuf_str(&j, big);
  ck_assert_uint_eq(j.len, sizeof big - 1);

  j.failed = 1;
  jbuf_str(&j, "ignored");
  ck_assert_uint_eq(j.len, sizeof big - 1);

  jbuf_reset(&j);
  ck_assert_uint_eq(j.len, 0u);
  ck_assert_int_eq(j.failed, 0);
  jbuf_str(&j, "again");
  ck_assert_str_eq(j.buf, "again");
  jbuf_reset(&j);
  jbuf_reset(&j);
  ck_assert_uint_eq(j.len, 0u);
  free(j.buf);
}
END_TEST

START_TEST(reset_on_untouched_buffer_is_safe) {
  jbuf_t j;

  memset(&j, 0, sizeof j);
  jbuf_reset(&j);
  ck_assert_uint_eq(j.len, 0u);
  ck_assert_ptr_null(j.buf);
}
END_TEST

START_TEST(large_output_stays_intact) {
  jbuf_t j;

  memset(&j, 0, sizeof j);
  for (int i = 0; i < 20000; i++) jbuf_str(&j, "abcd");
  ck_assert_uint_eq(j.len, 80000u);
  ck_assert_uint_eq(strlen(j.buf), 80000u);
  ck_assert_mem_eq(j.buf + 79996, "abcd", 5);
  ck_assert_int_eq(j.failed, 0);
  free(j.buf);
}
END_TEST

static Suite *jsonbuf_suite(void) {
  Suite *s = suite_create("jsonbuf");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, raw_and_str_accumulate_including_embedded_nul);
  tcase_add_loop_test(tc, u64_formats_decimal, 0, (int)(sizeof u64_cases / sizeof u64_cases[0]));
  tcase_add_loop_test(tc, i64_formats_signed_decimal, 0, (int)(sizeof i64_cases / sizeof i64_cases[0]));
  tcase_add_loop_test(tc, fixed3_rounds_to_three_decimals, 0, (int)(sizeof fixed_cases / sizeof fixed_cases[0]));
  tcase_add_loop_test(tc, json_string_escapes_and_quotes, 0, (int)(sizeof string_cases / sizeof string_cases[0]));
  tcase_add_test(tc, key_and_fmt_build_members);
  tcase_add_test(tc, reset_clears_content_and_failure_and_allows_reuse);
  tcase_add_test(tc, reset_on_untouched_buffer_is_safe);
  tcase_add_test(tc, large_output_stays_intact);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(jsonbuf_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
