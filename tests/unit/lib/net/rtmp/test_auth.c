/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "lib/mux/amf.h"
#include "lib/net/rtmp/auth.h"
#include "lib/net/rtmp/chunk.h"
#include "lib/net/rtmp/handshake.h"
#include "lib/net/rtmp/rtmp.h"

#define RTMP_TYPE_INVOKE 20

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

typedef struct {
  unsigned char buf[16384];
  size_t len;
  int ready;
  char error[64];
} capture_t;

static void cap_write(void *ctx, const unsigned char *data, size_t len) {
  capture_t *c = ctx;
  ck_assert_uint_le(c->len + len, sizeof c->buf);
  memcpy(c->buf + c->len, data, len);
  c->len += len;
}

static void cap_ready(void *ctx) { ((capture_t *)ctx)->ready = 1; }

static void cap_error(void *ctx, const char *msg) {
  capture_t *c = ctx;
  snprintf(c->error, sizeof c->error, "%s", msg);
}

static size_t build_needauth(unsigned char *out) {
  ebuf_t b;
  rtmp_chunk_header_t h;
  size_t n;

  memset(&b, 0, sizeof b);
  amf_string(&b, "_error");
  amf_number(&b, 1);
  amf_null(&b);
  amf_object_start(&b);
  amf_object_key(&b, "description");
  amf_string(&b, "?reason=needauth&user=bob&salt=abcd1234&opaque=deadbeef");
  amf_object_end(&b);
  h.fmt = RTMP_CHUNK_FMT_0;
  h.cid = 3;
  h.timestamp = 0;
  h.length = (uint32_t)b.len;
  h.type = RTMP_TYPE_INVOKE;
  h.stream_id = 0;
  n = rtmp_chunk_basic_header_write(out, h.fmt, h.cid);
  n += rtmp_chunk_message_header_write(out + n, &h);
  memcpy(out + n, b.p, b.len);
  n += b.len;
  ebuf_free(&b);
  return n;
}

static rtmp_t *new_client(capture_t *cap, const char *auth_query) {
  rtmp_cfg_t cfg;

  memset(cap, 0, sizeof *cap);
  memset(&cfg, 0, sizeof cfg);
  cfg.app = "live";
  cfg.tcurl = "rtmp://host/live";
  cfg.stream_name = "key123";
  cfg.user = "bob";
  cfg.password = "hunter2";
  cfg.auth_query = auth_query;
  cfg.write_cb = cap_write;
  cfg.ready_cb = cap_ready;
  cfg.error_cb = cap_error;
  cfg.cb_ctx = cap;
  return rtmp_new(&cfg);
}

static void handshake(rtmp_t *r) {
  unsigned char s[1 + 2 * RTMP_HANDSHAKE_SIZE];

  rtmp_start(r);
  s[0] = RTMP_VERSION;
  memset(s + 1, 0x42, sizeof s - 1);
  ck_assert_int_eq(rtmp_feed(r, s, sizeof s), 0);
}

START_TEST(rtmp_needauth_yields_carryable_query) {
  capture_t cap;
  char q[400];
  char want[40];
  char c2[40];
  const char *p;
  unsigned char msg[512];
  size_t mlen;
  rtmp_t *r = new_client(&cap, NULL);

  ck_assert_ptr_nonnull(r);
  handshake(r);
  ck_assert_int_eq(rtmp_auth_carry(r, q, sizeof q), 0);
  mlen = build_needauth(msg);
  ck_assert_int_eq(rtmp_feed(r, msg, mlen), 0);
  ck_assert_str_eq(cap.error, "auth challenge");
  ck_assert_int_eq(rtmp_auth_carry(r, q, sizeof q), 1);
  ck_assert_ptr_nonnull(strstr(q, "?authmod=adobe&user=bob&challenge="));
  ck_assert_ptr_nonnull(strstr(q, "&opaque=deadbeef"));
  p = strstr(q, "&challenge=") + 11;
  memcpy(c2, p, 16);
  c2[16] = '\0';
  expected_response("bob", "hunter2", "abcd1234", "deadbeef", c2, want);
  p = strstr(q, "&response=");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(strncmp(p + 10, want, 24), 0);
  ck_assert_int_eq(p[10 + 24], '&');
  ck_assert_int_eq(rtmp_auth_carry(r, q, 10), 0);
  rtmp_free(r);
}
END_TEST

START_TEST(rtmp_carried_query_is_sent_and_not_retried) {
  capture_t cap;
  unsigned char msg[512];
  size_t mlen;
  rtmp_t *r = new_client(&cap, "?authmod=adobe&user=bob&challenge=0123456789abcdef&response=XXXXXXXXXXXXXXXXXXXXXXXX&opaque=deadbeef");
  char q[400];

  ck_assert_ptr_nonnull(r);
  handshake(r);
  /* connect spans several 128B chunks: match short pieces */
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "challenge=", 10));
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "opaque=", 7));
  mlen = build_needauth(msg);
  ck_assert_int_eq(rtmp_feed(r, msg, mlen), 0);
  ck_assert_str_eq(cap.error, "_error");
  ck_assert_int_eq(rtmp_auth_carry(r, q, sizeof q), 0);
  rtmp_free(r);
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
  tcase_add_test(tc, rtmp_needauth_yields_carryable_query);
  tcase_add_test(tc, rtmp_carried_query_is_sent_and_not_retried);
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
