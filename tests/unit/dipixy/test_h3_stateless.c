/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include <ngtcp2/ngtcp2_crypto.h>

#include "dipixy/http3/http3_stateless.h"

#define SEC 1000000000ULL

static struct sockaddr_in addr_of(const char *ip, unsigned port) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  ck_assert_int_eq(inet_pton(AF_INET, ip, &a.sin_addr), 1);
  return a;
}

static ngtcp2_cid cid_of(uint8_t seed, size_t len) {
  ngtcp2_cid c;
  memset(&c, 0, sizeof c);
  c.datalen = len;
  for (size_t i = 0; i < len; i++) c.data[i] = (uint8_t)(seed + i);
  return c;
}

static void setup(void) {
  ck_assert_int_eq(h3_stateless_init(), 0);
}

START_TEST(retry_token_round_trip) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  ngtcp2_cid retry_scid = cid_of(0x10, 16), odcid = cid_of(0x40, 8), got;
  uint8_t tok[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  ngtcp2_ssize n = h3_token_retry_make(tok, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, &odcid, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_eq(tok[0], NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY2);
  ck_assert_int_eq(h3_token_retry_check(&got, tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, 6 * SEC), 0);
  ck_assert_uint_eq(got.datalen, odcid.datalen);
  ck_assert_int_eq(memcmp(got.data, odcid.data, odcid.datalen), 0);
}
END_TEST

START_TEST(retry_token_rejects_other_address) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  struct sockaddr_in other = addr_of("192.0.2.8", 4433);
  ngtcp2_cid retry_scid = cid_of(0x10, 16);
  ngtcp2_cid odcid = cid_of(0x40, 8);
  ngtcp2_cid got;
  uint8_t tok[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  ngtcp2_ssize n = h3_token_retry_make(tok, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, &odcid, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_ne(h3_token_retry_check(&got, tok, (size_t)n, (struct sockaddr *)&other, sizeof other, NGTCP2_PROTO_VER_V1, &retry_scid, 6 * SEC), 0);
}
END_TEST

START_TEST(retry_token_rejects_other_dcid) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  ngtcp2_cid retry_scid = cid_of(0x10, 16);
  ngtcp2_cid odcid = cid_of(0x40, 8);
  ngtcp2_cid wrong = cid_of(0x11, 16);
  ngtcp2_cid got;
  uint8_t tok[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  ngtcp2_ssize n = h3_token_retry_make(tok, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, &odcid, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_ne(h3_token_retry_check(&got, tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &wrong, 6 * SEC), 0);
}
END_TEST

START_TEST(retry_token_expires) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  ngtcp2_cid retry_scid = cid_of(0x10, 16);
  ngtcp2_cid odcid = cid_of(0x40, 8);
  ngtcp2_cid got;
  uint8_t tok[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  ngtcp2_ssize n = h3_token_retry_make(tok, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, &odcid, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_eq(h3_token_retry_check(&got, tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, 14 * SEC), 0);
  ck_assert_int_ne(h3_token_retry_check(&got, tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, 16 * SEC), 0);
}
END_TEST

START_TEST(retry_token_rejects_tampering) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  ngtcp2_cid retry_scid = cid_of(0x10, 16);
  ngtcp2_cid odcid = cid_of(0x40, 8);
  ngtcp2_cid got;
  uint8_t tok[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  ngtcp2_ssize n = h3_token_retry_make(tok, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, &odcid, 5 * SEC);
  ck_assert_int_gt(n, 0);
  tok[n / 2] ^= 0x01;
  ck_assert_int_ne(h3_token_retry_check(&got, tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, 6 * SEC), 0);
}
END_TEST

START_TEST(new_token_round_trip) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  uint8_t tok[NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN];
  ngtcp2_ssize n = h3_token_new_make(tok, (struct sockaddr *)&peer, sizeof peer, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_eq(tok[0], NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR);
  ck_assert_int_eq(h3_token_new_check(tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, 600 * SEC), 0);
}
END_TEST

START_TEST(new_token_rejects_other_address) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  struct sockaddr_in other = addr_of("198.51.100.1", 4433);
  uint8_t tok[NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN];
  ngtcp2_ssize n = h3_token_new_make(tok, (struct sockaddr *)&peer, sizeof peer, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_ne(h3_token_new_check(tok, (size_t)n, (struct sockaddr *)&other, sizeof other, 6 * SEC), 0);
}
END_TEST

START_TEST(new_token_expires) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  uint8_t tok[NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN];
  ngtcp2_ssize n = h3_token_new_make(tok, (struct sockaddr *)&peer, sizeof peer, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_ne(h3_token_new_check(tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, 5 * SEC + 7200 * SEC), 0);
}
END_TEST

START_TEST(new_token_rejects_tampering) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  uint8_t tok[NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN];
  ngtcp2_ssize n = h3_token_new_make(tok, (struct sockaddr *)&peer, sizeof peer, 5 * SEC);
  ck_assert_int_gt(n, 0);
  tok[n - 1] ^= 0x80;
  ck_assert_int_ne(h3_token_new_check(tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, 6 * SEC), 0);
}
END_TEST

START_TEST(retry_token_is_not_a_new_token) {
  struct sockaddr_in peer = addr_of("192.0.2.7", 4433);
  ngtcp2_cid retry_scid = cid_of(0x10, 16);
  ngtcp2_cid odcid = cid_of(0x40, 8);
  uint8_t tok[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  ngtcp2_ssize n = h3_token_retry_make(tok, (struct sockaddr *)&peer, sizeof peer, NGTCP2_PROTO_VER_V1, &retry_scid, &odcid, 5 * SEC);
  ck_assert_int_gt(n, 0);
  ck_assert_int_ne(h3_token_new_check(tok, (size_t)n, (struct sockaddr *)&peer, sizeof peer, 6 * SEC), 0);
}
END_TEST

START_TEST(reset_token_is_stable_per_cid) {
  ngtcp2_cid a = cid_of(0x20, 16);
  ngtcp2_cid b = cid_of(0x21, 16);
  uint8_t t1[16];
  uint8_t t2[16];
  uint8_t t3[16];
  ck_assert_int_eq(h3_reset_token(t1, &a), 0);
  ck_assert_int_eq(h3_reset_token(t2, &a), 0);
  ck_assert_int_eq(h3_reset_token(t3, &b), 0);
  ck_assert_int_eq(memcmp(t1, t2, sizeof t1), 0);
  ck_assert_int_ne(memcmp(t1, t3, sizeof t1), 0);
}
END_TEST

START_TEST(retry_policy_by_mode) {
  ck_assert_int_eq(h3_retry_needed(H3_RETRY_OFF, 1000, 256), 0);
  ck_assert_int_eq(h3_retry_needed(H3_RETRY_ALWAYS, 0, 256), 1);
  ck_assert_int_eq(h3_retry_needed(H3_RETRY_AUTO, 0, 256), 0);
  ck_assert_int_eq(h3_retry_needed(H3_RETRY_AUTO, 127, 256), 0);
  ck_assert_int_eq(h3_retry_needed(H3_RETRY_AUTO, 128, 256), 1);
  ck_assert_int_eq(h3_retry_needed(H3_RETRY_AUTO, 256, 256), 1);
}
END_TEST

static Suite *h3_stateless_suite(void) {
  Suite *s = suite_create("dipixy_h3_stateless");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, setup, NULL);
  tcase_add_test(tc, retry_token_round_trip);
  tcase_add_test(tc, retry_token_rejects_other_address);
  tcase_add_test(tc, retry_token_rejects_other_dcid);
  tcase_add_test(tc, retry_token_expires);
  tcase_add_test(tc, retry_token_rejects_tampering);
  tcase_add_test(tc, new_token_round_trip);
  tcase_add_test(tc, new_token_rejects_other_address);
  tcase_add_test(tc, new_token_expires);
  tcase_add_test(tc, new_token_rejects_tampering);
  tcase_add_test(tc, retry_token_is_not_a_new_token);
  tcase_add_test(tc, reset_token_is_stable_per_cid);
  tcase_add_test(tc, retry_policy_by_mode);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(h3_stateless_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
