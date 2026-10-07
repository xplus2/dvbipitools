/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../log_capture.h"
#include "../metrics_sink.h"
#include "dipirist/bridge.h"
#include "dipirist/version.h"

START_TEST(profile_of_maps_simple) {
  ck_assert_int_eq(profile_of(RIST_PROF_SIMPLE), RIST_PROFILE_SIMPLE);
}
END_TEST

START_TEST(profile_of_maps_main) {
  ck_assert_int_eq(profile_of(RIST_PROF_MAIN), RIST_PROFILE_MAIN);
}
END_TEST

START_TEST(tssrc_cfg_file_with_path) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;
  strcpy(s.file_path, "/tmp/fixture.ts");

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_FILE);
  ck_assert_str_eq(tc.file_path, "/tmp/fixture.ts");
  ck_assert_str_eq(tc.user_agent, TOOL_NAME "/" TOOL_VERSION);
}
END_TEST

START_TEST(tssrc_cfg_file_empty_path_is_stdin) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_STDIN);
}
END_TEST

START_TEST(tssrc_cfg_http_carries_tls_and_insecure) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_HTTP;
  s.http.tls = 1;

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 1, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_HTTP);
  ck_assert_int_eq(tc.http.tls, 1);
  ck_assert_int_eq(tc.insecure_tls, 1);
}
END_TEST

START_TEST(tssrc_cfg_rtp_ipv4_carries_family_group_port_iface) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_RTP;
  s.family = AF_INET;
  strcpy(s.group, "239.1.1.1");
  s.port = 5000;

  plain_endpoint_to_tssrc_cfg(&s, "eth0", TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_RTP);
  ck_assert_int_eq(tc.family, AF_INET);
  ck_assert_str_eq(tc.group, "239.1.1.1");
  ck_assert_uint_eq(tc.port, 5000u);
  ck_assert_str_eq(tc.iface, "eth0");
}
END_TEST

START_TEST(tssrc_cfg_rtp_ipv6_carries_family) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_RTP;
  s.family = AF_INET6;
  strcpy(s.group, "ff3e::1");
  s.port = 8700;

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.family, AF_INET6);
  ck_assert_str_eq(tc.group, "ff3e::1");
}
END_TEST

START_TEST(tssrc_cfg_udp_kind) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_UDP;

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_UDP);
}
END_TEST

START_TEST(tssink_cfg_file_with_path) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;
  strcpy(s.file_path, "/tmp/out.ts");

  plain_endpoint_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_FILE);
  ck_assert_str_eq(tk.file_path, "/tmp/out.ts");
}
END_TEST

START_TEST(tssink_cfg_file_empty_path_is_stdout) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;

  plain_endpoint_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_STDOUT);
}
END_TEST

START_TEST(tssink_cfg_rtp_ipv6_carries_family_group_port_iface) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_RTP;
  s.family = AF_INET6;
  strcpy(s.group, "ff3e::1");
  s.port = 8700;

  plain_endpoint_to_tssink_cfg(&s, "eth1", &tk);
  ck_assert_int_eq(tk.kind, TSSINK_RTP);
  ck_assert_int_eq(tk.family, AF_INET6);
  ck_assert_str_eq(tk.group, "ff3e::1");
  ck_assert_uint_eq(tk.port, 8700u);
  ck_assert_str_eq(tk.iface, "eth1");
}
END_TEST

START_TEST(tssink_cfg_udp_kind) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_UDP;

  plain_endpoint_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_UDP);
}
END_TEST

typedef enum {
  BR_RECV_BAD_PEER,
  BR_RECV_DATA_PORT_BUSY,
  BR_RECV_RTCP_PORT_BUSY,
  BR_RECV_SINK_FAILS,
  BR_SEND_SOURCE_FAILS,
  BR_SEND_BAD_PEER
} bridge_fail_t;

typedef struct {
  bridge_fail_t kind;
  const char *msg;
} bridge_fail_case_t;

static const bridge_fail_case_t bridge_fail_cases[] = {
  {BR_RECV_BAD_PEER, "invalid peer url"},
  {BR_RECV_DATA_PORT_BUSY, "already in use"},
  {BR_RECV_RTCP_PORT_BUSY, "RTCP of"},
  {BR_RECV_SINK_FAILS, NULL},
  {BR_SEND_SOURCE_FAILS, NULL},
  {BR_SEND_BAD_PEER, NULL},
};

static void file_endpoint(endpoint_t *e, const char *path) {
  memset(e, 0, sizeof *e);
  e->nonrist.kind = PLAIN_EP_FILE;
  snprintf(e->nonrist.file_path, sizeof e->nonrist.file_path, "%s", path);
}

static void rist_endpoint(endpoint_t *e, const char *uri) {
  memset(e, 0, sizeof *e);
  e->is_rist = 1;
  e->n_rist = 1;
  snprintf(e->rist_uri[0], sizeof e->rist_uri[0], "%s", uri);
}

static int bind_odd_udp_port(unsigned *port_out) {
  for (int tries = 0; tries < 100; tries++) {
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ck_assert_int_ge(fd, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
    ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
    if (ntohs(a.sin_port) & 1u) {
      *port_out = ntohs(a.sin_port);
      return fd;
    }
    close(fd);
  }
  ck_abort_msg("no odd udp port");
  return -1;
}

START_TEST(bridge_run_fails_cleanly_when_setup_cannot_complete) {
  const bridge_fail_case_t *c = &bridge_fail_cases[_i];
  config_t cfg;
  metrics_exporter_t mx;
  char msg[4096];
  char uri[64];
  unsigned port = 0;
  int blocker = -1;
  int rc;

  memset(&cfg, 0, sizeof cfg);
  memset(&mx, 0, sizeof mx);
  switch (c->kind) {
    case BR_RECV_DATA_PORT_BUSY:
    case BR_RECV_RTCP_PORT_BUSY:
      blocker = bind_odd_udp_port(&port);
      snprintf(uri, sizeof uri, "rist://@127.0.0.1:%u", c->kind == BR_RECV_DATA_PORT_BUSY ? port : port - 1);
      rist_endpoint(&cfg.in, uri);
      file_endpoint(&cfg.out, "/dev/null");
      break;
    case BR_RECV_BAD_PEER:
      rist_endpoint(&cfg.in, "");
      file_endpoint(&cfg.out, "/dev/null");
      break;
    case BR_RECV_SINK_FAILS:
      rist_endpoint(&cfg.in, "rist://@127.0.0.1:0");
      file_endpoint(&cfg.out, "/nonexistent-dir/out.ts");
      break;
    case BR_SEND_SOURCE_FAILS:
      file_endpoint(&cfg.in, "/nonexistent-dir/in.ts");
      rist_endpoint(&cfg.out, "rist://127.0.0.1:9");
      break;
    case BR_SEND_BAD_PEER:
      file_endpoint(&cfg.in, "/dev/null");
      rist_endpoint(&cfg.out, "");
      break;
  }
  cfg.n_in = 1;
  cfg.n_out = 1;
  log_capture_begin();
  rc = bridge_run(&cfg, &mx);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 1);
  if (c->msg) ck_assert_msg(strstr(msg, c->msg) != NULL, "log=[%s]", msg);
  if (blocker >= 0) close(blocker);
}
END_TEST

static struct rist_stats *new_stats(enum rist_stats_type type) {
  struct rist_stats *st = calloc(1, sizeof *st);

  ck_assert_ptr_nonnull(st);
  st->stats_type = type;
  return st;
}

START_TEST(receiver_stats_cb_pushes_flow_counters_once_per_interval) {
  sink_t ms;
  seen_t seen;
  struct rist_stats *st = new_stats(RIST_STATS_RECEIVER_FLOW);
  struct rist_stats *again = new_stats(RIST_STATS_RECEIVER_FLOW);
  uint64_t v = 0;

  st->stats.receiver_flow.received = 1000;
  st->stats.receiver_flow.missing = 7;
  st->stats.receiver_flow.recovered = 5;
  st->stats.receiver_flow.lost = 2;
  st->stats.receiver_flow.rtt = 33;
#ifdef DIPIRIST_HAVE_AVG_BUFFER_TIME
  st->stats.receiver_flow.avg_buffer_time = 150;
#endif
  again->stats.receiver_flow.received = 2000;
  sink_open(&ms, METRICS_COMPONENT_RIST, "rist1", 5.0);
  ck_assert_int_eq(bridge_receiver_stats_cb(&ms.mx, st), 0);
  ck_assert_int_eq(bridge_receiver_stats_cb(&ms.mx, again), 0);
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1000u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 7u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 5u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_LOST_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 2u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, &v), 1);
  ck_assert_uint_eq(v, 33u);
#ifdef DIPIRIST_HAVE_AVG_BUFFER_TIME
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_BUFFER_MILLISECONDS, &v), 1);
  ck_assert_uint_eq(v, 150u);
#endif
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1000u);
  sink_close(&ms);
}
END_TEST

START_TEST(receiver_stats_cb_ignores_other_stats_types) {
  sink_t ms;
  seen_t seen;
  struct rist_stats *st = new_stats(RIST_STATS_SENDER_PEER);

  sink_open(&ms, METRICS_COMPONENT_RIST, "rist1", 5.0);
  ck_assert_int_eq(bridge_receiver_stats_cb(&ms.mx, st), 0);
  ck_assert_int_eq(sink_read(&ms, &seen), 0);
  sink_close(&ms);
}
END_TEST

static Suite *bridge_suite(void) {
  Suite *s = suite_create("dipirist_bridge");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, profile_of_maps_simple);
  tcase_add_test(tc, profile_of_maps_main);
  tcase_add_test(tc, tssrc_cfg_file_with_path);
  tcase_add_test(tc, tssrc_cfg_file_empty_path_is_stdin);
  tcase_add_test(tc, tssrc_cfg_http_carries_tls_and_insecure);
  tcase_add_test(tc, tssrc_cfg_rtp_ipv4_carries_family_group_port_iface);
  tcase_add_test(tc, tssrc_cfg_rtp_ipv6_carries_family);
  tcase_add_test(tc, tssrc_cfg_udp_kind);
  tcase_add_test(tc, tssink_cfg_file_with_path);
  tcase_add_test(tc, tssink_cfg_file_empty_path_is_stdout);
  tcase_add_test(tc, tssink_cfg_rtp_ipv6_carries_family_group_port_iface);
  tcase_add_test(tc, tssink_cfg_udp_kind);
  tcase_add_test(tc, receiver_stats_cb_pushes_flow_counters_once_per_interval);
  tcase_add_test(tc, receiver_stats_cb_ignores_other_stats_types);
  tcase_add_loop_test(tc, bridge_run_fails_cleanly_when_setup_cannot_complete, 0, (int)(sizeof bridge_fail_cases / sizeof bridge_fail_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bridge_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
