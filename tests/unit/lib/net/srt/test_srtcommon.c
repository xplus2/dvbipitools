/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <srt/srt.h>

#include "../../../run_helper.h"
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
  for (int i = 0; i < 2000 && !srtcommon_resolver_poll(r); i++) nanosleep(&(struct timespec){0, 1000000L}, NULL);
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
  for (int i = 0; i < 2000 && !srtcommon_resolver_poll(r); i++) nanosleep(&(struct timespec){0, 1000000L}, NULL);
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

START_TEST(startup_and_cleanup_are_refcounted) {
  srtcommon_cleanup();
  ck_assert_int_eq(srtcommon_startup(), 0);
  ck_assert_int_eq(srtcommon_startup(), 0);
  srtcommon_cleanup();
  srtcommon_cleanup();
  srtcommon_cleanup();
  ck_assert_int_eq(srtcommon_startup(), 0);
  srtcommon_cleanup();
}
END_TEST

START_TEST(logging_handler_routes_every_level) {
  SRTSOCKET s;
  srtcommon_opts_t o;
  ck_assert_int_eq(srtcommon_startup(), 0);
  srtcommon_open_logging(1);
  srtcommon_open_logging(0);
  s = srt_create_socket();
  memset(&o, 0, sizeof o);
  o.packetfilter = "not-a-filter";
  ck_assert_int_eq(srtcommon_apply_opts(s, &o, 0, SRTO_RCVTIMEO, 100), -1);
  srt_close(s);
  srtcommon_cleanup();
}
END_TEST

START_TEST(apply_opts_covers_every_option_and_failure) {
  static const struct {
    const char *pass;
    int pbkeylen;
    const char *streamid;
    const char *filter;
    unsigned latency;
    int rc;
  } t[] = {
      {"longenoughpassphrase", 0, NULL, NULL, 0, 0},
      {"longenoughpassphrase", 16, "sid", NULL, 120, 0},
      {"longenoughpassphrase", 32, NULL, NULL, 0, 0},
      {"", 0, "", "", 0, 0},
      {"short", 0, NULL, NULL, 0, -1},
      {"longenoughpassphrase", 7, NULL, NULL, 0, -1},
      {NULL, 0, NULL, "fec,cols:4,rows:4", 0, 0},
      {NULL, 0, NULL, "bogus-filter", 0, -1},
  };
  SRTSOCKET s;
  srtcommon_opts_t o;
  ck_assert_int_eq(srtcommon_startup(), 0);
  s = srt_create_socket();
  memset(&o, 0, sizeof o);
  o.passphrase = t[_i].pass;
  o.pbkeylen = t[_i].pbkeylen;
  o.streamid = t[_i].streamid;
  o.packetfilter = t[_i].filter;
  o.latency_ms = t[_i].latency;
  ck_assert_int_eq(srtcommon_apply_opts(s, &o, 0, SRTO_SNDTIMEO, 100), t[_i].rc);
  srt_close(s);
  srtcommon_cleanup();
}
END_TEST

START_TEST(apply_opts_fails_on_invalid_socket) {
  srtcommon_opts_t o;
  ck_assert_int_eq(srtcommon_startup(), 0);
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(srtcommon_apply_opts(SRT_INVALID_SOCK, &o, 0, SRTO_RCVTIMEO, 100), -1);
  ck_assert_int_eq(srtcommon_apply_opts(SRT_INVALID_SOCK, &o, 1, SRTO_RCVTIMEO, 100), -1);
  srtcommon_cleanup();
}
END_TEST

static int stop_never(const void *arg) {
  (void)arg;
  return 0;
}

static int stop_now(const void *arg) {
  (void)arg;
  return 1;
}

START_TEST(stoppable_resolve_forms) {
  struct sockaddr_storage ss;
  int len = 0;
  char longhost[400];
  ck_assert_int_eq(srtcommon_resolve_stoppable("127.0.0.1", 9000, &ss, &len, 2000, stop_never, NULL), 0);
  ck_assert_int_eq(((struct sockaddr_in *)&ss)->sin_family, AF_INET);
  ck_assert_int_eq(srtcommon_resolve_stoppable("127.0.0.1", 9000, &ss, &len, 0, stop_never, NULL), 0);
  (void)srtcommon_resolve_stoppable("127.0.0.1", 9000, &ss, &len, 2000, stop_now, NULL);
  memset(longhost, 'a', sizeof longhost - 1);
  longhost[sizeof longhost - 1] = '\0';
  ck_assert_int_eq(srtcommon_resolve_stoppable(longhost, 9000, &ss, &len, 100, stop_never, NULL), -1);
  ck_assert_int_eq(srtcommon_resolve_stoppable("bad host name!", 9000, &ss, &len, 2000, stop_never, NULL), -1);
}
END_TEST

START_TEST(resolver_rejects_bad_counts_and_long_hosts) {
  srtcommon_peer_t peers[SRTCOMMON_MAX_PEERS + 2];
  char longhost[400];
  srtcommon_resolver_t *r;
  struct sockaddr_storage ss;
  int len;
  memset(longhost, 'a', sizeof longhost - 1);
  longhost[sizeof longhost - 1] = '\0';
  for (int i = 0; i < SRTCOMMON_MAX_PEERS + 2; i++) {
    peers[i].host = "127.0.0.1";
    peers[i].port = (unsigned)(9000 + i);
  }
  ck_assert_ptr_null(srtcommon_resolver_start(peers, 0));
  ck_assert_ptr_null(srtcommon_resolver_start(peers, SRTCOMMON_MAX_PEERS + 2));
  peers[0].host = longhost;
  r = srtcommon_resolver_start(peers, 2);
  ck_assert_ptr_nonnull(r);
  for (int i = 0; i < 200 && !srtcommon_resolver_poll(r); i++) sleep_ms(10);
  ck_assert_int_eq(srtcommon_resolver_get(r, 0, &ss, &len), -1);
  ck_assert_int_eq(srtcommon_resolver_get(r, 1, &ss, &len), 0);
  srtcommon_resolver_free(r);
  srtcommon_resolver_free(NULL);
}
END_TEST

START_TEST(build_group_config_resolves_each_peer) {
  srtcommon_peer_t peers[2] = {{"127.0.0.1", 9100}, {"127.0.0.1", 9101}};
  srtcommon_peer_t bad[2] = {{"127.0.0.1", 9100}, {"bad host name!", 9101}};
  SRT_SOCKGROUPCONFIG gc[2];
  ck_assert_int_eq(srtcommon_startup(), 0);
  ck_assert_int_eq(srtcommon_build_group_config(peers, 2, gc), 0);
  ck_assert_int_eq(srtcommon_build_group_config(bad, 2, gc), -1);
  srtcommon_cleanup();
}
END_TEST

START_TEST(group_missing_treats_dead_states_as_not_live) {
  static const struct {
    SRT_SOCKSTATUS st;
    SRT_MEMBERSTATUS ms;
    int missing;
  } t[] = {
      {SRTS_CONNECTED, SRT_GST_RUNNING, 0},
      {SRTS_CONNECTED, SRT_GST_BROKEN, 1},
      {SRTS_BROKEN, SRT_GST_RUNNING, 1},
      {SRTS_CLOSING, SRT_GST_RUNNING, 1},
      {SRTS_CLOSED, SRT_GST_RUNNING, 1},
      {SRTS_NONEXIST, SRT_GST_RUNNING, 1},
  };
  struct sockaddr_storage addrs[1];
  SRT_SOCKGROUPDATA data[1];
  int missing[1];
  set_peer(&addrs[0], "10.0.0.1", 5000);
  set_member(&data[0], "10.0.0.1", 5000, t[_i].st, t[_i].ms);
  ck_assert_int_eq(srtcommon_group_missing(addrs, 1, data, 1, missing), t[_i].missing);
}
END_TEST

START_TEST(group_missing_compares_ipv6_and_foreign_families) {
  struct sockaddr_storage addrs[2];
  SRT_SOCKGROUPDATA data[2];
  int missing[2];
  struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&addrs[0];
  memset(addrs, 0, sizeof addrs);
  a6->sin6_family = AF_INET6;
  a6->sin6_port = htons(7000);
  inet_pton(AF_INET6, "::1", &a6->sin6_addr);
  addrs[1].ss_family = AF_UNIX;
  memset(data, 0, sizeof data);
  memcpy(&data[0].peeraddr, a6, sizeof *a6);
  data[0].sockstate = SRTS_CONNECTED;
  data[0].memberstate = SRT_GST_RUNNING;
  memcpy(&data[1].peeraddr, &addrs[1], sizeof addrs[1]);
  data[1].sockstate = SRTS_CONNECTED;
  ck_assert_int_eq(srtcommon_group_missing(addrs, 2, data, 2, missing), 1);
  ck_assert_int_eq(missing[0], 0);
  ck_assert_int_eq(missing[1], 1);
  a6->sin6_port = htons(7001);
  ck_assert_int_eq(srtcommon_group_missing(addrs, 1, data, 1, missing), 1);
  inet_pton(AF_INET6, "::2", &a6->sin6_addr);
  a6->sin6_port = htons(7000);
  ck_assert_int_eq(srtcommon_group_missing(addrs, 1, data, 1, missing), 1);
}
END_TEST

START_TEST(group_revive_on_invalid_or_unsupported_group_is_quiet) {
  srtcommon_peer_t peers[2] = {{"127.0.0.1", 9200}, {"127.0.0.1", 9201}};
  srtcommon_resolver_t *res = NULL;
  SRTSOCKET g;
  ck_assert_int_eq(srtcommon_startup(), 0);
  ck_assert_int_eq(srtcommon_group_revive(SRT_INVALID_SOCK, peers, 2, &res), 0);
  g = srt_create_group(SRT_GTYPE_BROADCAST);
  if (g != SRT_INVALID_SOCK) {
    for (int i = 0; i < 50 && (i == 0 || res); i++) {
      (void)srtcommon_group_revive(g, peers, 2, &res);
      sleep_ms(20);
    }
    srt_close(g);
  }
  srtcommon_resolver_free(res);
  srtcommon_cleanup();
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
  tcase_add_test(tc, startup_and_cleanup_are_refcounted);
  tcase_add_test(tc, logging_handler_routes_every_level);
  tcase_add_loop_test(tc, apply_opts_covers_every_option_and_failure, 0, 8);
  tcase_add_test(tc, apply_opts_fails_on_invalid_socket);
  tcase_add_test(tc, stoppable_resolve_forms);
  tcase_add_test(tc, resolver_rejects_bad_counts_and_long_hosts);
  tcase_add_test(tc, build_group_config_resolves_each_peer);
  tcase_add_loop_test(tc, group_missing_treats_dead_states_as_not_live, 0, 6);
  tcase_add_test(tc, group_missing_compares_ipv6_and_foreign_families);
  tcase_add_test(tc, group_revive_on_invalid_or_unsupported_group_is_quiet);
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
