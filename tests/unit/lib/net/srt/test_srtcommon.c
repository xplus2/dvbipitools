/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include <srt/srt.h>

#include "lib/net/srt/srtcommon.h"

START_TEST(resolve_ipv4) {
  struct sockaddr_storage ss;
  struct sockaddr_in *a = (struct sockaddr_in *)&ss;
  struct in_addr want;
  int len;

  ck_assert_int_eq(srtcommon_resolve("127.0.0.1", 9000, &ss, &len), 0);
  ck_assert_int_eq(a->sin_family, AF_INET);
  ck_assert_uint_eq(ntohs(a->sin_port), 9000u);
  inet_pton(AF_INET, "127.0.0.1", &want);
  ck_assert_int_eq(memcmp(&a->sin_addr, &want, sizeof want), 0);
}
END_TEST

START_TEST(resolve_ipv6) {
  struct sockaddr_storage ss;
  struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
  struct in6_addr want;
  int len;

  ck_assert_int_eq(srtcommon_resolve("::1", 9000, &ss, &len), 0);
  ck_assert_int_eq(a->sin6_family, AF_INET6);
  ck_assert_uint_eq(ntohs(a->sin6_port), 9000u);
  inet_pton(AF_INET6, "::1", &want);
  ck_assert_int_eq(memcmp(&a->sin6_addr, &want, sizeof want), 0);
}
END_TEST

START_TEST(resolve_bad_host_fails) {
  struct sockaddr_storage ss;
  int len;

  ck_assert_int_ne(srtcommon_resolve("this.is.not.a.valid.host.example.invalid", 9000, &ss, &len), 0);
}
END_TEST

START_TEST(apply_opts_sets_latency_and_streamid) {
  SRTSOCKET s;
  srtcommon_opts_t o;
  int lat = 0, optlen = sizeof lat;
  char sid[64];
  int sidlen = sizeof sid;

  ck_assert_int_eq(srt_startup(), 0);
  s = srt_create_socket();
  ck_assert_int_ne(s, SRT_INVALID_SOCK);

  memset(&o, 0, sizeof o);
  o.streamid = "test-stream";
  o.latency_ms = 250;

  ck_assert_int_eq(srtcommon_apply_opts(s, &o, 0, SRTO_RCVTIMEO, 200), 0);
  ck_assert_int_eq(srt_getsockopt(s, 0, SRTO_LATENCY, &lat, &optlen), 0);
  ck_assert_int_eq(lat, 250);
  ck_assert_int_eq(srt_getsockopt(s, 0, SRTO_STREAMID, sid, &sidlen), 0);
  ck_assert_str_eq(sid, "test-stream");

  srt_close(s);
  srt_cleanup();
}
END_TEST

START_TEST(apply_opts_group_skips_transtype) {
  SRTSOCKET g;
  srtcommon_opts_t o;

  ck_assert_int_eq(srt_startup(), 0);
  g = srt_create_group(SRT_GTYPE_BROADCAST);
  if (g == SRT_INVALID_SOCK) {
    /* this libsrt build lacks ENABLE_BONDING: --group-mode is unsupported here too, see args.c */
    srt_cleanup();
    return;
  }

  memset(&o, 0, sizeof o);
  ck_assert_int_eq(srtcommon_apply_opts(g, &o, 1, SRTO_SNDTIMEO, 1000), 0);

  srt_close(g);
  srt_cleanup();
}
END_TEST

static Suite *srtcommon_suite(void) {
  Suite *s = suite_create("srtcommon");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, resolve_ipv4);
  tcase_add_test(tc, resolve_ipv6);
  tcase_add_test(tc, resolve_bad_host_fails);
  tcase_add_test(tc, apply_opts_sets_latency_and_streamid);
  tcase_add_test(tc, apply_opts_group_skips_transtype);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(srtcommon_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
