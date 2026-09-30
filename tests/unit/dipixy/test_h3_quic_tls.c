/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <unistd.h>

#include "dipixy/http3/http3.h"
#include "dipixy/http3/http3_int.h"

/* h3_create_udp_sock/sock6 only null-check g_h3_ssl_ctx, never dereference it:
   a non-NULL sentinel is enough to reach the code under test without a real TLS setup */
static int h3_ssl_ctx_sentinel;
static void setup(void) { g_h3_ssl_ctx = (SSL_CTX *)(void *)&h3_ssl_ctx_sentinel; }
static void teardown(void) { g_h3_ssl_ctx = NULL; }

START_TEST(create_udp_sock_rejects_malformed_ipv4_host) {
  ck_assert_int_lt(h3_create_udp_sock(0, "not-an-ip"), 0);
}
END_TEST

START_TEST(create_udp_sock_binds_valid_ipv4_host) {
  int fd = h3_create_udp_sock(0, "127.0.0.1");
  ck_assert_int_ge(fd, 0);
  close(fd);
}
END_TEST

START_TEST(create_udp_sock6_rejects_malformed_ipv6_host) {
  ck_assert_int_lt(h3_create_udp_sock6(0, "not-an-ip"), 0);
}
END_TEST

START_TEST(create_udp_sock6_binds_valid_ipv6_host) {
  int fd = h3_create_udp_sock6(0, "::1");
  ck_assert_int_ge(fd, 0);
  close(fd);
}
END_TEST

START_TEST(create_udp_sock6_binds_wildcard_for_empty_host) {
  int fd = h3_create_udp_sock6(0, NULL);
  ck_assert_int_ge(fd, 0);
  close(fd);
}
END_TEST

static Suite *h3_quic_tls_suite(void) {
  Suite *s = suite_create("dipixy_h3_quic_tls");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, create_udp_sock_rejects_malformed_ipv4_host);
  tcase_add_test(tc, create_udp_sock_binds_valid_ipv4_host);
  tcase_add_test(tc, create_udp_sock6_rejects_malformed_ipv6_host);
  tcase_add_test(tc, create_udp_sock6_binds_valid_ipv6_host);
  tcase_add_test(tc, create_udp_sock6_binds_wildcard_for_empty_host);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(h3_quic_tls_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
