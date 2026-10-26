/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../log_capture.h"
#include "../run_helper.h"
#include "../metrics_sink.h"
#include "dipirist/bridge.h"
#include "lib/sys/signal.h"

START_TEST(profile_of_maps_simple) {
  ck_assert_int_eq(profile_of(RIST_PROF_SIMPLE), RIST_PROFILE_SIMPLE);
}
END_TEST

START_TEST(profile_of_maps_main) {
  ck_assert_int_eq(profile_of(RIST_PROF_MAIN), RIST_PROFILE_MAIN);
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
#ifdef DVBIPITOOLS_RIST_HAVE_AVG_BUFFER_TIME
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
#ifdef DVBIPITOOLS_RIST_HAVE_AVG_BUFFER_TIME
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

START_TEST(receiver_idle_push_sends_commons_without_flow) {
  sink_t ms;
  seen_t seen;
  uint64_t v = 0;

  sink_open(&ms, METRICS_COMPONENT_RIST, "rist1", 5.0);
  bridge_receiver_idle_push(&ms.mx, 100.0);
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_HEADEND_INFO, &v), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, &v), 0);
  sink_close(&ms);
}
END_TEST

START_TEST(receiver_idle_push_yields_while_flow_stats_fresh) {
  sink_t ms;
  seen_t seen;
  struct rist_stats *st = new_stats(RIST_STATS_RECEIVER_FLOW);

  sink_open(&ms, METRICS_COMPONENT_RIST, "rist1", 5.0);
  ck_assert_int_eq(bridge_receiver_stats_cb(&ms.mx, st), 0);
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  bridge_receiver_idle_push(&ms.mx, mono_seconds() + 10.0);
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  bridge_receiver_idle_push(&ms.mx, mono_seconds() + 1.0);
  ck_assert_int_eq(sink_read(&ms, &seen), 0);
  sink_close(&ms);
}
END_TEST

typedef struct {
  unsigned ms;
} stopper_t;

static void *stopper_main(void *arg) {
  const stopper_t *st = arg;
  sleep_ms(st->ms);
  signal_request_stop();
  return NULL;
}

typedef struct {
  int listen_fd;
  int conns;
  int body_mode;
} http_feed_t;

static void *http_feed_main(void *arg) {
  const http_feed_t *f = arg;
  unsigned char ts[188 * 20];
  memset(ts, 0xAB, sizeof ts);
  for (size_t i = 0; i < 20; i++) {
    ts[i * 188] = 0x47;
    ts[i * 188 + 1] = 0x01;
    ts[i * 188 + 2] = 0x00;
    ts[i * 188 + 3] = 0x10;
  }
  for (int c = 0; c < f->conns; c++) {
    struct pollfd pfd = {.fd = f->listen_fd, .events = POLLIN, .revents = 0};
    char req[1024];
    char head[128];
    int cfd;
    if (poll(&pfd, 1, 8000) <= 0) break;
    cfd = accept(f->listen_fd, NULL, NULL);
    if (cfd < 0) break;
    (void)recv(cfd, req, sizeof req, 0);
    if (f->body_mode == 1) {
      (void)send(cfd, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, MSG_NOSIGNAL);
    } else {
      int hl = snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", sizeof ts);
      (void)send(cfd, head, (size_t)hl, MSG_NOSIGNAL);
      (void)send(cfd, ts, sizeof ts, MSG_NOSIGNAL);
    }
    close(cfd);
  }
  return NULL;
}

static int http_listener(unsigned *port) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(listen(fd, 4), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  *port = ntohs(a.sin_port);
  return fd;
}

static void sender_cfg_http(config_t *cfg, unsigned http_port) {
  char url[96];
  memset(cfg, 0, sizeof *cfg);
  snprintf(url, sizeof url, "http://127.0.0.1:%u/stream.ts", http_port);
  cfg->in.nonrist.kind = PLAIN_EP_HTTP;
  ck_assert_int_eq(http_url_parse(url, &cfg->in.nonrist.http), 0);
  rist_endpoint(&cfg->out, "rist://127.0.0.1:9");
  cfg->n_in = 1;
  cfg->n_out = 1;
}

START_TEST(sender_with_http_source_reconnects_after_the_stream_ends) {
  static const int modes[] = {0, 1};
  config_t cfg;
  metrics_exporter_t mx;
  http_feed_t feed;
  stopper_t stop = {3500};
  pthread_t th_feed;
  pthread_t th_stop;
  unsigned port;
  int rc;
  feed.listen_fd = http_listener(&port);
  feed.conns = 3;
  feed.body_mode = modes[_i];
  memset(&mx, 0, sizeof mx);
  sender_cfg_http(&cfg, port);
  cfg.pace = _i == 0;
  ck_assert_int_eq(pthread_create(&th_feed, NULL, http_feed_main, &feed), 0);
  ck_assert_int_eq(pthread_create(&th_stop, NULL, stopper_main, &stop), 0);
  rc = bridge_run(&cfg, &mx);
  pthread_join(th_stop, NULL);
  pthread_join(th_feed, NULL);
  close(feed.listen_fd);
  ck_assert_int_ge(rc, 0);
}
END_TEST

START_TEST(receiver_without_traffic_idles_until_stopped) {
  config_t cfg;
  metrics_exporter_t mx;
  stopper_t stop = {1500};
  pthread_t th_stop;
  unsigned port;
  int blocker = bind_odd_udp_port(&port);
  char uri[64];
  int rc;
  close(blocker);
  memset(&cfg, 0, sizeof cfg);
  memset(&mx, 0, sizeof mx);
  snprintf(uri, sizeof uri, "rist://@127.0.0.1:%u", port - 1);
  rist_endpoint(&cfg.in, uri);
  file_endpoint(&cfg.out, "/dev/null");
  cfg.n_in = 1;
  cfg.n_out = 1;
  ck_assert_int_eq(pthread_create(&th_stop, NULL, stopper_main, &stop), 0);
  rc = bridge_run(&cfg, &mx);
  pthread_join(th_stop, NULL);
  ck_assert_int_eq(rc, 0);
}
END_TEST

static Suite *bridge_suite(void) {
  Suite *s = suite_create("dipirist_bridge");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, profile_of_maps_simple);
  tcase_add_test(tc, profile_of_maps_main);
  tcase_add_test(tc, receiver_stats_cb_pushes_flow_counters_once_per_interval);
  tcase_add_test(tc, receiver_stats_cb_ignores_other_stats_types);
  tcase_add_test(tc, receiver_idle_push_sends_commons_without_flow);
  tcase_add_test(tc, receiver_idle_push_yields_while_flow_stats_fresh);
  tcase_add_loop_test(tc, bridge_run_fails_cleanly_when_setup_cannot_complete, 0, (int)(sizeof bridge_fail_cases / sizeof bridge_fail_cases[0]));
  tcase_set_timeout(tc, 30);
  tcase_add_loop_test(tc, sender_with_http_source_reconnects_after_the_stream_ends, 0, 2);
  tcase_add_test(tc, receiver_without_traffic_idles_until_stopped);
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
