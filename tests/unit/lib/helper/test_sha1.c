/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/sha1.h"

static void to_hex(const uint8_t digest[20], char out[41]) {
  static const char digits[] = "0123456789abcdef";

  for (size_t i = 0; i < 20; i++) {
    out[i * 2] = digits[digest[i] >> 4];
    out[i * 2 + 1] = digits[digest[i] & 0x0F];
  }
  out[40] = '\0';
}

typedef struct {
  const char *input;
  const char *hex;
} text_case_t;

static const text_case_t text_cases[] = {
    {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
    {"The quick brown fox jumps over the lazy dog", "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
    {"0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567"
     "0123456701234567012345670123456701234567012345670123456701234567",
     "dea356a2cddd90c7a7ecedc5ebb563934f460452"},
};

START_TEST(sha1_known_text_vectors) {
  const text_case_t *c = &text_cases[_i];
  uint8_t digest[20];
  char hex[41];

  sha1(c->input, strlen(c->input), digest);
  to_hex(digest, hex);
  ck_assert_str_eq(hex, c->hex);
}
END_TEST

typedef struct {
  size_t len;
  const char *hex;
} pattern_case_t;

static const pattern_case_t pattern_cases[] = {
    {0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    {1, "9842926af7ca0a8cca12604f945414f07b01e13d"},
    {54, "0617d0e2db51b03d5d09c217b3fe0643ed6d152f"},
    {55, "ddf57317ef34bfee3b6df83d359098930eb278bc"},
    {56, "a0d492bb0fc889d0eca3bc137066ab6f4f74f369"},
    {57, "11a02dcf95859677a62e75024067c22b165d890f"},
    {63, "c55856749bef509bdfe6bfebfc7bf4e793e82132"},
    {64, "bede92be29c3874e1b54ddc77988d606fc857a8e"},
    {65, "b05a80522b053d6dc7e0a517d0e70212c7dad11f"},
    {119, "504e27376a6e0f0dba8295b85cb25dc4dfa17d23"},
    {120, "82134b02fb3f702491be9bed581eeab59334acb2"},
    {121, "f004a54c50aa3705ccc2f03f21016ca6a25755cb"},
    {127, "34d5e582029e9b9b85b2febe31da3db7cdabaaea"},
    {128, "a09133e6730ffe899efb70204cb5646cd5dc24ee"},
    {129, "808aea332ce367541d37adae7f94e59c5c1a934e"},
    {1000, "4231a8a50a10fa9758db8ec71fdef855b751048a"},
};

START_TEST(sha1_padding_boundaries) {
  const pattern_case_t *c = &pattern_cases[_i];
  uint8_t *data = malloc(c->len ? c->len : 1);
  uint8_t digest[20];
  char hex[41];

  ck_assert_ptr_nonnull(data);
  for (size_t i = 0; i < c->len; i++) data[i] = (uint8_t)(i * 7 + 3);
  sha1(data, c->len, digest);
  to_hex(digest, hex);
  ck_assert_msg(strcmp(hex, c->hex) == 0, "length %zu: %s", c->len, hex);
  free(data);
}
END_TEST

START_TEST(sha1_one_million_a) {
  size_t len = 1000000;
  uint8_t *data = malloc(len);
  uint8_t digest[20];
  char hex[41];

  ck_assert_ptr_nonnull(data);
  memset(data, 'a', len);
  sha1(data, len, digest);
  to_hex(digest, hex);
  ck_assert_str_eq(hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
  free(data);
}
END_TEST

START_TEST(sha1_unaligned_input_matches_aligned) {
  uint8_t backing[200];
  uint8_t aligned[100];
  uint8_t a[20];
  uint8_t b[20];

  for (size_t i = 0; i < sizeof aligned; i++) aligned[i] = (uint8_t)(i * 11 + 1);
  memcpy(backing + 1, aligned, sizeof aligned);
  sha1(aligned, sizeof aligned, a);
  sha1(backing + 1, sizeof aligned, b);
  ck_assert_mem_eq(a, b, sizeof a);
}
END_TEST

static Suite *sha1_suite(void) {
  Suite *s = suite_create("sha1");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, sha1_known_text_vectors, 0, (int)(sizeof text_cases / sizeof text_cases[0]));
  tcase_add_loop_test(tc, sha1_padding_boundaries, 0, (int)(sizeof pattern_cases / sizeof pattern_cases[0]));
  tcase_add_test(tc, sha1_one_million_a);
  tcase_add_test(tc, sha1_unaligned_input_matches_aligned);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(sha1_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
