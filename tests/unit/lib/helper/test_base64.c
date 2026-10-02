/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/base64.h"

typedef struct {
  const char *input;
  size_t len;
  const char *expect;
} b64_case_t;

static const b64_case_t b64_cases[] = {
    {"", 0, ""},
    {"f", 1, "Zg=="},
    {"fo", 2, "Zm8="},
    {"foo", 3, "Zm9v"},
    {"foob", 4, "Zm9vYg=="},
    {"fooba", 5, "Zm9vYmE="},
    {"foobar", 6, "Zm9vYmFy"},
    {"\xFB\xFF\xBF", 3, "+/+/"},
    {"\x00\x00\x00", 3, "AAAA"},
    {"\xFF", 1, "/w=="},
    {"a\0b", 3, "YQBi"},
};

START_TEST(base64_rfc4648_vectors) {
  const b64_case_t *c = &b64_cases[_i];
  size_t want = strlen(c->expect);
  char *out = malloc(base64_encoded_len(c->len) + 1);

  ck_assert_ptr_nonnull(out);
  ck_assert_uint_eq(base64_encoded_len(c->len), want);
  base64_encode(c->input, c->len, out);
  ck_assert_str_eq(out, c->expect);
  free(out);
}
END_TEST

START_TEST(base64_all_byte_values) {
  static const char expect[] =
      "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9Q"
      "UVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6Ch"
      "oqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy"
      "8/T19vf4+fr7/P3+/w==";
  unsigned char data[256];
  char out[sizeof expect];

  for (size_t i = 0; i < sizeof data; i++) data[i] = (unsigned char)i;
  ck_assert_uint_eq(base64_encoded_len(sizeof data), sizeof expect - 1);
  base64_encode(data, sizeof data, out);
  ck_assert_str_eq(out, expect);
}
END_TEST

static int b64_value(char c) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const char *p = strchr(alphabet, c);

  return (c && p) ? (int)(p - alphabet) : -1;
}

START_TEST(base64_round_trips_every_length_with_exact_buffers) {
  size_t len = (size_t)_i;
  unsigned char *data = malloc(len ? len : 1);
  char *out = malloc(base64_encoded_len(len) + 1);
  size_t olen;
  size_t pos = 0;

  ck_assert_ptr_nonnull(data);
  ck_assert_ptr_nonnull(out);
  for (size_t i = 0; i < len; i++) data[i] = (unsigned char)(i * 37 + 11);
  base64_encode(data, len, out);
  olen = strlen(out);
  ck_assert_uint_eq(olen, base64_encoded_len(len));
  ck_assert_uint_eq(olen % 4, 0u);

  for (size_t i = 0; i < olen; i += 4) {
    int v0 = b64_value(out[i]);
    int v1 = b64_value(out[i + 1]);
    int v2 = out[i + 2] == '=' ? 0 : b64_value(out[i + 2]);
    int v3 = out[i + 3] == '=' ? 0 : b64_value(out[i + 3]);
    unsigned long v;

    ck_assert_int_ge(v0, 0);
    ck_assert_int_ge(v1, 0);
    ck_assert_int_ge(v2, 0);
    ck_assert_int_ge(v3, 0);
    v = ((unsigned long)v0 << 18) | ((unsigned long)v1 << 12) | ((unsigned long)v2 << 6) | (unsigned long)v3;
    if (pos < len) ck_assert_uint_eq((unsigned char)(v >> 16), data[pos]);
    pos++;
    if (out[i + 2] != '=') {
      if (pos < len) ck_assert_uint_eq((unsigned char)(v >> 8), data[pos]);
      pos++;
    }
    if (out[i + 3] != '=') {
      if (pos < len) ck_assert_uint_eq((unsigned char)v, data[pos]);
      pos++;
    }
  }
  ck_assert_uint_eq(pos, len);
  free(data);
  free(out);
}
END_TEST

static Suite *base64_suite(void) {
  Suite *s = suite_create("base64");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, base64_rfc4648_vectors, 0, (int)(sizeof b64_cases / sizeof b64_cases[0]));
  tcase_add_test(tc, base64_all_byte_values);
  tcase_add_loop_test(tc, base64_round_trips_every_length_with_exact_buffers, 0, 70);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(base64_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
