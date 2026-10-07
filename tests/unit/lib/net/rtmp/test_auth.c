/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "lib/net/rtmp/auth.h"

static void md5_b64_ref(const char *s, char *out) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned char b64[32];
  unsigned int dlen = 0;

  ck_assert(EVP_Digest(s, strlen(s), digest, &dlen, EVP_md5(), NULL));
  ck_assert_uint_eq(dlen, 16);
  EVP_EncodeBlock(b64, digest, (int)dlen);
  strcpy(out, (char *)b64);
}

static void expected_response(const char *user, const char *password, const char *salt, const char *mid, const char *challenge2, char *out) {
  char buf[512];
  char hash1[32];

  snprintf(buf, sizeof buf, "%s%s%s", user, salt, password);
  md5_b64_ref(buf, hash1);
  snprintf(buf, sizeof buf, "%s%s%s", hash1, mid, challenge2);
  md5_b64_ref(buf, out);
}

START_TEST(adobe_md5_reference_vector) {
  char hash[32];

  md5_b64_ref("", hash);
  ck_assert_str_eq(hash, "1B2M2Y8AsgTpgAmY7PhCfg==");
  md5_b64_ref("abc", hash);
  ck_assert_str_eq(hash, "kAFQmDzST7DWlj99KOF/cg==");
}
END_TEST

START_TEST(adobe_response_matches_reference_with_opaque) {
  char c2[40];
  char resp[40];
  char want[40];

  ck_assert_int_eq(rtmp_auth_adobe_response("user", "pw", "salt", "opq", NULL, c2, sizeof c2, resp, sizeof resp), 0);
  ck_assert_uint_eq(strlen(c2), 16);
  for (size_t i = 0; i < 16; i++) ck_assert(isxdigit((unsigned char)c2[i]) && !isupper((unsigned char)c2[i]));
  expected_response("user", "pw", "salt", "opq", c2, want);
  ck_assert_str_eq(resp, want);
}
END_TEST

START_TEST(adobe_response_uses_server_challenge_when_no_opaque) {
  char c2[40];
  char resp[40];
  char want[40];

  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", NULL, "chal", c2, sizeof c2, resp, sizeof resp), 0);
  expected_response("u", "p", "s", "chal", c2, want);
  ck_assert_str_eq(resp, want);
}
END_TEST

START_TEST(adobe_response_prefers_opaque_over_server_challenge) {
  char c2[40];
  char resp[40];
  char want[40];

  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "opq", "chal", c2, sizeof c2, resp, sizeof resp), 0);
  expected_response("u", "p", "s", "opq", c2, want);
  ck_assert_str_eq(resp, want);
}
END_TEST

START_TEST(adobe_response_challenge2_is_random) {
  char c2a[40];
  char c2b[40];
  char resp[40];

  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "o", NULL, c2a, sizeof c2a, resp, sizeof resp), 0);
  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "o", NULL, c2b, sizeof c2b, resp, sizeof resp), 0);
  ck_assert_str_ne(c2a, c2b);
}
END_TEST

START_TEST(adobe_response_without_opaque_and_challenge_fails) {
  char c2[40];
  char resp[40];

  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", NULL, NULL, c2, sizeof c2, resp, sizeof resp), -1);
}
END_TEST

START_TEST(adobe_response_oversized_credentials_fail) {
  char big[600];
  char c2[40];
  char resp[40];

  memset(big, 'a', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  ck_assert_int_eq(rtmp_auth_adobe_response(big, "p", "s", "o", NULL, c2, sizeof c2, resp, sizeof resp), -1);
}
END_TEST

START_TEST(adobe_response_challenge2_buffer_too_small) {
  char c2[16];
  char resp[40];

  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "o", NULL, c2, sizeof c2, resp, sizeof resp), -1);
  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "o", NULL, c2, 0, resp, sizeof resp), -1);
}
END_TEST

START_TEST(adobe_response_response_buffer_too_small) {
  char c2[40];
  char resp[24];

  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "o", NULL, c2, sizeof c2, resp, sizeof resp), -1);
  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "o", NULL, c2, sizeof c2, resp, 0), -1);
}
END_TEST

START_TEST(adobe_response_response_buffer_exact_fit) {
  char c2[40];
  char resp[25];

  ck_assert_int_eq(rtmp_auth_adobe_response("u", "p", "s", "o", NULL, c2, sizeof c2, resp, sizeof resp), 0);
  ck_assert_uint_eq(strlen(resp), 24);
}
END_TEST

static Suite *auth_suite(void) {
  Suite *s = suite_create("rtmp_auth");
  TCase *tc = tcase_create("adobe");
  tcase_add_test(tc, adobe_md5_reference_vector);
  tcase_add_test(tc, adobe_response_matches_reference_with_opaque);
  tcase_add_test(tc, adobe_response_uses_server_challenge_when_no_opaque);
  tcase_add_test(tc, adobe_response_prefers_opaque_over_server_challenge);
  tcase_add_test(tc, adobe_response_challenge2_is_random);
  tcase_add_test(tc, adobe_response_without_opaque_and_challenge_fails);
  tcase_add_test(tc, adobe_response_oversized_credentials_fail);
  tcase_add_test(tc, adobe_response_challenge2_buffer_too_small);
  tcase_add_test(tc, adobe_response_response_buffer_too_small);
  tcase_add_test(tc, adobe_response_response_buffer_exact_fit);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(auth_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
