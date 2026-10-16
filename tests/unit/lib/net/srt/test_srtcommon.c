/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

  ck_assert_int_ne(srtcommon_resolve("", 9000, &ss, &len), 0);
}
END_TEST

START_TEST(resolver_settles_numeric_hosts) {
  srtcommon_peer_t pe[2] = {{"127.0.0.1", 9000}, {"::1", 9001}};
  srtcommon_resolver_t *r = srtcommon_resolver_start(pe, 2);
  struct sockaddr_storage ss;
  int len;

  ck_assert_ptr_nonnull(r);
  for (int i = 0; i < 2000 && !srtcommon_resolver_poll(r); i++) usleep(1000);
  ck_assert_int_eq(srtcommon_resolver_poll(r), 1);
  ck_assert_int_eq(srtcommon_resolver_get(r, 0, &ss, &len), 0);
  ck_assert_int_eq(ss.ss_family, AF_INET);
  ck_assert_int_eq(srtcommon_resolver_get(r, 1, &ss, &len), 0);
  ck_assert_int_eq(ss.ss_family, AF_INET6);
  srtcommon_resolver_free(r);
}
END_TEST

START_TEST(resolver_reports_failed_host) {
  srtcommon_peer_t pe[2] = {{"127.0.0.1", 9000}, {"", 9001}};
  srtcommon_resolver_t *r = srtcommon_resolver_start(pe, 2);
  struct sockaddr_storage ss;
  int len;

  ck_assert_ptr_nonnull(r);
  for (int i = 0; i < 2000 && !srtcommon_resolver_poll(r); i++) usleep(1000);
  ck_assert_int_eq(srtcommon_resolver_poll(r), 1);
  ck_assert_int_eq(srtcommon_resolver_get(r, 0, &ss, &len), 0);
  ck_assert_int_ne(srtcommon_resolver_get(r, 1, &ss, &len), 0);
  srtcommon_resolver_free(r);
}
END_TEST

START_TEST(resolver_free_abandons_pending) {
  srtcommon_peer_t pe[1] = {{"127.0.0.1", 9000}};
  srtcommon_resolver_t *r = srtcommon_resolver_start(pe, 1);

  ck_assert_ptr_nonnull(r);
  srtcommon_resolver_free(r);
  srtcommon_resolver_free(NULL);
}
END_TEST

START_TEST(apply_opts_sets_latency_and_streamid) {
  SRTSOCKET s;
  srtcommon_opts_t o;
  int lat = 0;
  int optlen = sizeof lat;
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

static void set_peer(struct sockaddr_storage *ss, const char *ip, unsigned port) {
  struct sockaddr_in *a = (struct sockaddr_in *)ss;

  memset(ss, 0, sizeof *ss);
  a->sin_family = AF_INET;
  a->sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, ip, &a->sin_addr);
}

static void set_member(SRT_SOCKGROUPDATA *m, const char *ip, unsigned port, SRT_SOCKSTATUS st, SRT_MEMBERSTATUS ms) {
  memset(m, 0, sizeof *m);
  set_peer(&m->peeraddr, ip, port);
  m->sockstate = st;
  m->memberstate = ms;
}

START_TEST(group_missing_reports_peers_without_live_member) {
  struct sockaddr_storage addrs[3];
  SRT_SOCKGROUPDATA data[2];
  int missing[3];

  set_peer(&addrs[0], "127.0.0.1", 9000);
  set_peer(&addrs[1], "127.0.0.1", 9001);
  set_peer(&addrs[2], "127.0.0.2", 9000);
  set_member(&data[0], "127.0.0.1", 9000, SRTS_CONNECTED, SRT_GST_RUNNING);
  set_member(&data[1], "127.0.0.1", 9001, SRTS_BROKEN, SRT_GST_BROKEN);
  ck_assert_int_eq(srtcommon_group_missing(addrs, 3, data, 2, missing), 2);
  ck_assert_int_eq(missing[0], 0);
  ck_assert_int_eq(missing[1], 1);
  ck_assert_int_eq(missing[2], 1);
}
END_TEST

START_TEST(group_missing_counts_connecting_as_live) {
  struct sockaddr_storage addrs[1];
  SRT_SOCKGROUPDATA data[1];
  int missing[1];

  set_peer(&addrs[0], "127.0.0.1", 9000);
  set_member(&data[0], "127.0.0.1", 9000, SRTS_CONNECTING, SRT_GST_PENDING);
  ck_assert_int_eq(srtcommon_group_missing(addrs, 1, data, 1, missing), 0);
}
END_TEST

START_TEST(group_missing_skips_unresolved_peers) {
  struct sockaddr_storage addrs[1];
  int missing[1];

  memset(addrs, 0, sizeof addrs);
  ck_assert_int_eq(srtcommon_group_missing(addrs, 1, NULL, 0, missing), 0);
  ck_assert_int_eq(missing[0], 0);
}
END_TEST

START_TEST(group_missing_requires_matching_port) {
  struct sockaddr_storage addrs[1];
  SRT_SOCKGROUPDATA data[1];
  int missing[1];

  set_peer(&addrs[0], "127.0.0.1", 9000);
  set_member(&data[0], "127.0.0.1", 9999, SRTS_CONNECTED, SRT_GST_RUNNING);
  ck_assert_int_eq(srtcommon_group_missing(addrs, 1, data, 1, missing), 1);
}
END_TEST

static Suite *srtcommon_suite(void) {
  Suite *s = suite_create("srtcommon");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, resolve_ipv4);
  tcase_add_test(tc, resolve_ipv6);
  tcase_add_test(tc, resolve_bad_host_fails);
  tcase_add_test(tc, resolver_settles_numeric_hosts);
  tcase_add_test(tc, resolver_reports_failed_host);
  tcase_add_test(tc, resolver_free_abandons_pending);
  tcase_add_test(tc, apply_opts_sets_latency_and_streamid);
  tcase_add_test(tc, apply_opts_group_skips_transtype);
  tcase_add_test(tc, group_missing_reports_peers_without_live_member);
  tcase_add_test(tc, group_missing_counts_connecting_as_live);
  tcase_add_test(tc, group_missing_skips_unresolved_peers);
  tcase_add_test(tc, group_missing_requires_matching_port);
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
