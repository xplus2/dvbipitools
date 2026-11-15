/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/helper/strutil.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
const char *__asan_default_options(void) { return "allocator_may_return_null=1"; }
#endif
#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
const char *__tsan_default_options(void) { return "allocator_may_return_null=1"; }
#endif

START_TEST(bufcpy_truncation_forms) {
  static const struct {
    const char *src;
    size_t cap;
    const char *want;
  } t[] = {
      {"hello", 0, NULL},
      {"hello", 1, ""},
      {"hello", 3, "he"},
      {"hello", 6, "hello"},
      {"hello", 100, "hello"},
      {"a\xC3\xA9", 3, "a"},
      {"a\xC3\xA9", 4, "a\xC3\xA9"},
      {"a\xE2\x82\xAC", 4, "a"},
      {"a\xE2\x82\xAC", 3, "a"},
      {"a\xF0\x9F\x98\x80", 5, "a"},
      {"a\xF0\x9F\x98\x80", 4, "a"},
      {"a\xF0\x9F\x98\x80", 3, "a"},
      {"a\x80\x80\x80\x80\x80", 5, "a\x80\x80\x80"},
      {"abc\x80\x80\x80\x80", 5, "ab"},
  };
  char out[32];
  size_t r;
  memset(out, 'X', sizeof out);
  r = bufcpy(out, t[_i].cap, t[_i].src);
  ck_assert_uint_eq(r, strlen(t[_i].src));
  if (t[_i].want) ck_assert_str_eq(out, t[_i].want);
  else ck_assert_int_eq(out[0], 'X');
}
END_TEST

START_TEST(number_to_string_forms) {
  char b[32];
  ck_assert_uint_eq(uint_to_str(b, 0), 1u);
  ck_assert_str_eq(b, "0");
  ck_assert_uint_eq(uint_to_str(b, 4294967295u), 10u);
  ck_assert_str_eq(b, "4294967295");
  ck_assert_uint_eq(uint_to_str_pad(b, 7, 4), 4u);
  ck_assert_str_eq(b, "0007");
  ck_assert_uint_eq(uint_to_str_pad(b, 12345, 3), 5u);
  ck_assert_str_eq(b, "12345");
  ck_assert_uint_eq(uint_to_str_pad(b, 0, 3), 3u);
  ck_assert_str_eq(b, "000");
  ck_assert_uint_eq(u64_to_dec(b, 0), 1u);
  ck_assert_str_eq(b, "0");
  ck_assert_uint_eq(u64_to_dec(b, UINT64_MAX), 20u);
  ck_assert_str_eq(b, "18446744073709551615");
}
END_TEST

START_TEST(digit_and_number_parsing) {
  unsigned long v = 0;
  char max[32];
  char over[32];
  ck_assert_int_eq(all_digits("123", 3), 1);
  ck_assert_int_eq(all_digits("12a", 3), 0);
  ck_assert_int_eq(all_digits("12a", 2), 1);
  ck_assert_int_eq(all_digits("", 0), 1);
  ck_assert_int_eq(parse_ulong("", 10, &v), -1);
  ck_assert_int_eq(parse_ulong("12x", 100, &v), -1);
  ck_assert_int_eq(parse_ulong("-1", 100, &v), -1);
  ck_assert_int_eq(parse_ulong("101", 100, &v), -1);
  ck_assert_int_eq(parse_ulong("100", 100, &v), 0);
  ck_assert_uint_eq(v, 100u);
  ck_assert_int_eq(parse_ulong("99999999999999999999999", ULONG_MAX, &v), -1);
  snprintf(max, sizeof max, "%lu", ULONG_MAX);
  memcpy(over, max, sizeof over);
  for (size_t i = strlen(over); i-- > 0;) {
    if (over[i] != '9') {
      over[i]++;
      break;
    }
    over[i] = '0';
  }
  ck_assert_int_eq(parse_ulong(max, ULONG_MAX, &v), 0);
  ck_assert_uint_eq(v, ULONG_MAX);
  ck_assert_int_eq(parse_ulong(over, ULONG_MAX, &v), -1);
}
END_TEST

START_TEST(chomp_and_csv) {
  char a[] = "line\r\n";
  char b[] = "\n\n";
  char c[] = "";
  char d[] = "x";
  char csv[] = "a,b,,d";
  char one[] = "solo";
  char *f[3];
  chomp(a);
  ck_assert_str_eq(a, "line");
  chomp(b);
  ck_assert_str_eq(b, "");
  chomp(c);
  chomp(d);
  ck_assert_str_eq(d, "x");
  ck_assert_uint_eq(csv_split(csv, f, 3), 3u);
  ck_assert_str_eq(f[0], "a");
  ck_assert_str_eq(f[1], "b");
  ck_assert_str_eq(f[2], "");
  ck_assert_uint_eq(csv_split(one, f, 3), 1u);
  ck_assert_str_eq(f[0], "solo");
  ck_assert_uint_eq(csv_split(one, f, 0), 0u);
}
END_TEST

START_TEST(csv_split_quoting_and_trim) {
  char a[] = " Name, With , \"Quoted, \"\"x\"\"\" ,\"\",  ";
  char b[] = "\"unterminated, still one";
  char c[] = "a,b,c,d";
  char d[] = "mid\"quote,x";
  char *f[5];
  ck_assert_uint_eq(csv_split(a, f, 5), 5u);
  ck_assert_str_eq(f[0], "Name");
  ck_assert_str_eq(f[1], "With");
  ck_assert_str_eq(f[2], "Quoted, \"x\"");
  ck_assert_str_eq(f[3], "");
  ck_assert_str_eq(f[4], "");
  ck_assert_uint_eq(csv_split(b, f, 5), 1u);
  ck_assert_str_eq(f[0], "unterminated, still one");
  ck_assert_uint_eq(csv_split(c, f, 2), 2u);
  ck_assert_str_eq(f[1], "b");
  ck_assert_uint_eq(csv_split(d, f, 5), 2u);
  ck_assert_str_eq(f[0], "mid\"quote");
}
END_TEST

START_TEST(sbuf_truncation_and_forms) {
  char buf[8];
  sbuf_t b;
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add(&b, "abc");
  sbuf_add_n(&b, "defgh", 2);
  ck_assert_str_eq(buf, "abcde");
  ck_assert_int_eq(b.truncated, 0);
  sbuf_add(&b, "xyz123");
  ck_assert_str_eq(buf, "abcdexy");
  ck_assert_int_eq(b.truncated, 1);
  sbuf_add(&b, "q");
  ck_assert_str_eq(buf, "abcdexy");
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_u64(&b, 1234567);
  ck_assert_str_eq(buf, "1234567");
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_uint(&b, 42);
  sbuf_add_hex2(&b, 0xAB);
  sbuf_add_hex2(&b, 0x105);
  ck_assert_str_eq(buf, "42ab05");
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_hex(&b, 0);
  sbuf_add_hex(&b, 0xdeadbeef);
  ck_assert_int_eq(b.truncated, 1);
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_hex(&b, 0x1f);
  ck_assert_str_eq(buf, "1f");
}
END_TEST

START_TEST(sbuf_zero_capacity) {
  sbuf_t b;
  sbuf_init(&b, NULL, 0);
  sbuf_add(&b, "");
  ck_assert_int_eq(b.truncated, 0);
  sbuf_add(&b, "x");
  ck_assert_int_eq(b.truncated, 1);
  sbuf_init(&b, NULL, 0);
  sbuf_add_n(&b, "x", 0);
  ck_assert_int_eq(b.truncated, 0);
}
END_TEST

START_TEST(dstrbuf_growth_and_numbers) {
  dstrbuf_t sb;
  char *big = malloc(20000);
  ck_assert_ptr_nonnull(big);
  memset(big, 'z', 19999);
  big[19999] = '\0';
  dstrbuf_init(&sb);
  ck_assert_ptr_nonnull(sb.buf);
  dstrbuf_add(&sb, "ab");
  dstrbuf_add_n(&sb, "cdef", 2);
  dstrbuf_add_u64(&sb, 7);
  dstrbuf_add_i64(&sb, -9);
  dstrbuf_add_i64(&sb, 5);
  dstrbuf_add_i64(&sb, INT64_MIN);
  ck_assert_str_eq(sb.buf, "abcd7-95-9223372036854775808");
  dstrbuf_add(&sb, big);
  ck_assert_uint_eq(sb.len, strlen("abcd7-95-9223372036854775808") + 19999);
  ck_assert_uint_ge(sb.cap, sb.len + 1);
  free(sb.buf);
  free(big);
}
END_TEST

START_TEST(dstrbuf_appendf_growth) {
  dstrbuf_t sb;
  char *big = malloc(30000);
  ck_assert_ptr_nonnull(big);
  memset(big, 'y', 29999);
  big[29999] = '\0';
  dstrbuf_init(&sb);
  dstrbuf_appendf(&sb, "%s-%d", "x", 5);
  ck_assert_str_eq(sb.buf, "x-5");
  dstrbuf_appendf(&sb, "%s", big);
  ck_assert_uint_eq(sb.len, 3u + 29999u);
  dstrbuf_appendf(&sb, "%s", "tail");
  ck_assert_uint_eq(sb.len, 3u + 29999u + 4u);
  ck_assert_int_eq(sb.buf[sb.len], 0);
  free(sb.buf);
  free(big);
}
END_TEST

START_TEST(dstrbuf_null_buffer_is_inert) {
  dstrbuf_t sb;
  memset(&sb, 0, sizeof sb);
  dstrbuf_add(&sb, "abc");
  dstrbuf_add_u64(&sb, 1);
  dstrbuf_add_i64(&sb, -1);
  dstrbuf_appendf(&sb, "%d", 1);
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.len, 0u);
}
END_TEST

START_TEST(dstrbuf_survives_allocation_failures) {
  dstrbuf_t sb;
  char big[20000];
  memset(big, 'y', sizeof big - 1);
  big[sizeof big - 1] = 0;
  alloc_fail_at(0);
  dstrbuf_init(&sb);
  alloc_fail_end();
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.cap, 0);
  dstrbuf_add_n(&sb, "x", 1);
  dstrbuf_appendf(&sb, "%d", 1);

  dstrbuf_init(&sb);
  ck_assert_ptr_nonnull(sb.buf);
  alloc_fail_at(0);
  dstrbuf_add(&sb, big);
  alloc_fail_end();
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.len, 0);
  dstrbuf_add(&sb, "z");

  dstrbuf_init(&sb);
  alloc_fail_at(0);
  dstrbuf_appendf(&sb, "%s", big);
  alloc_fail_end();
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.cap, 0);
  dstrbuf_appendf(&sb, "%d", 5);
}
END_TEST

static Suite *strutil_suite(void) {
  Suite *s = suite_create("strutil");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, bufcpy_truncation_forms, 0, 14);
  tcase_add_test(tc, number_to_string_forms);
  tcase_add_test(tc, digit_and_number_parsing);
  tcase_add_test(tc, chomp_and_csv);
  tcase_add_test(tc, csv_split_quoting_and_trim);
  tcase_add_test(tc, sbuf_truncation_and_forms);
  tcase_add_test(tc, sbuf_zero_capacity);
  tcase_add_test(tc, dstrbuf_growth_and_numbers);
  tcase_add_test(tc, dstrbuf_appendf_growth);
  tcase_add_test(tc, dstrbuf_null_buffer_is_inert);
  tcase_add_test(tc, dstrbuf_survives_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(strutil_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
