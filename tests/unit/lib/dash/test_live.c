/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "lib/dash/live.h"

typedef struct {
  int listen_fd;
  const char *const *responses;
  const size_t *response_lens;
  int n_responses;
} scripted_server_t;

static void *serve_scripted(void *arg) {
  const scripted_server_t *a = arg;
  for (int i = 0; i < a->n_responses; i++) {
    int cfd = accept(a->listen_fd, NULL, NULL);
    struct timeval tv = {2, 0};
    char buf[4096];
    size_t got = 0;
    if (cfd < 0) return NULL;
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    for (;;) {
      ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
      if (n <= 0) break;
      got += (size_t)n;
      if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) break;
    }
    send(cfd, a->responses[i], a->response_lens[i], 0);
    close(cfd);
  }
  return NULL;
}

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 4), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static void passthrough_cb(void *ctx, dash_live_t *h, const unsigned char *data, size_t len) {
  (void)ctx;
  dash_live_emit(h, data, len);
}

static int drive_until_len(dash_live_t *h, unsigned char *acc, size_t acc_cap, size_t *acc_len, size_t want, int max_iters) {
  for (int i = 0; i < max_iters && *acc_len < want; i++) {
    unsigned char tmp[512];
    struct pollfd pfd;
    ssize_t n;
    int fd = dash_live_poll_fd(h);
    if (fd >= 0) {
      pfd.fd = fd;
      pfd.events = dash_live_poll_events(h);
      pfd.revents = 0;
      poll(&pfd, 1, 100);
    } else {
      struct timespec ts = {0, 20000000};
      nanosleep(&ts, NULL);
    }
    n = dash_live_read(h, tmp, sizeof tmp, NULL);
    if (n < 0) return -1;
    if (n <= 0) continue;
    if (*acc_len + (size_t)n > acc_cap) return -1;
    memcpy(acc + *acc_len, tmp, (size_t)n);
    *acc_len += (size_t)n;
  }
  return *acc_len >= want ? 1 : 0;
}

START_TEST(dash_live_fetches_init_then_sequential_number_segments) {
  unsigned mpd_port, init_port, seg_port;
  int mpd_fd = make_listener(&mpd_port);
  int init_fd = make_listener(&init_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t mpd_th, init_th, seg_th;
  char mpd_body[1024];
  char mpd_uri[64];
  const char init_payload[] = "INIT-FTYP-MOOV";
  const char seg1_payload[] = "SEG0001-BYTES";
  const char seg2_payload[] = "SEG0002-BYTES";
  char init_resp[256], seg1_resp[256], seg2_resp[256];
  size_t init_resp_len, seg1_resp_len, seg2_resp_len;
  const char *mpd_responses[1];
  size_t mpd_lens[1];
  const char *init_responses[1];
  size_t init_lens[1];
  const char *seg_responses[2];
  size_t seg_lens[2];
  scripted_server_t mpd_srv, init_srv, seg_srv;
  http_url_t mpd_url;
  dash_live_t *h;
  unsigned char acc[4096];
  size_t acc_len = 0;
  size_t want_len = strlen(init_payload) + strlen(seg1_payload) + strlen(seg2_payload);

  init_resp_len = (size_t)snprintf(init_resp, sizeof init_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(init_payload), init_payload);
  seg1_resp_len = (size_t)snprintf(seg1_resp, sizeof seg1_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(seg1_payload), seg1_payload);
  seg2_resp_len = (size_t)snprintf(seg2_resp, sizeof seg2_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(seg2_payload), seg2_payload);

  /* duration=10/timescale=1000: 10ms segment cadence, fast enough here */
  snprintf(mpd_body, sizeof mpd_body,
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
    "<MPD type=\"static\">\n"
    "  <Period id=\"0\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\">\n"
    "      <Representation id=\"v\" bandwidth=\"3000000\">\n"
    "        <SegmentTemplate initialization=\"http://127.0.0.1:%u/init.mp4\" media=\"http://127.0.0.1:%u/seg$Number$.m4s\" timescale=\"1000\" duration=\"10\" startNumber=\"1\"/>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n",
    init_port, seg_port);

  mpd_responses[0] = mpd_body;
  mpd_lens[0] = strlen(mpd_body);
  mpd_srv.listen_fd = mpd_fd;
  mpd_srv.responses = mpd_responses;
  mpd_srv.response_lens = mpd_lens;
  mpd_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&mpd_th, NULL, serve_scripted, &mpd_srv), 0);

  init_responses[0] = init_resp;
  init_lens[0] = init_resp_len;
  init_srv.listen_fd = init_fd;
  init_srv.responses = init_responses;
  init_srv.response_lens = init_lens;
  init_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&init_th, NULL, serve_scripted, &init_srv), 0);

  seg_responses[0] = seg1_resp;
  seg_responses[1] = seg2_resp;
  seg_lens[0] = seg1_resp_len;
  seg_lens[1] = seg2_resp_len;
  seg_srv.listen_fd = seg_fd;
  seg_srv.responses = seg_responses;
  seg_srv.response_lens = seg_lens;
  seg_srv.n_responses = 2;
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  snprintf(mpd_uri, sizeof mpd_uri, "http://127.0.0.1:%u/live.mpd", mpd_port);
  ck_assert_int_eq(http_url_parse(mpd_uri, &mpd_url), 0);
  h = dash_live_new(&mpd_url, "test-agent", 0, 0, "test", 0, NULL, passthrough_cb, NULL);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_len(h, acc, sizeof acc, &acc_len, want_len, 300), 1);

  ck_assert_int_eq(memcmp(acc, init_payload, strlen(init_payload)), 0);
  ck_assert_int_eq(memcmp(acc + strlen(init_payload), seg1_payload, strlen(seg1_payload)), 0);
  ck_assert_int_eq(memcmp(acc + strlen(init_payload) + strlen(seg1_payload), seg2_payload, strlen(seg2_payload)), 0);

  dash_live_free(h);
  pthread_join(mpd_th, NULL);
  pthread_join(init_th, NULL);
  pthread_join(seg_th, NULL);
  close(mpd_fd);
  close(init_fd);
  close(seg_fd);
}
END_TEST

typedef struct {
  char calls[4][32];
  size_t lens[4];
  int n_calls;
} cb_log_t;

static void logging_cb(void *ctx, dash_live_t *h, const unsigned char *data, size_t len) {
  cb_log_t *log = ctx;
  (void)h;
  if (log->n_calls < 4) {
    size_t n = len < sizeof log->calls[0] - 1 ? len : sizeof log->calls[0] - 1;
    memcpy(log->calls[log->n_calls], data, n);
    log->calls[log->n_calls][n] = '\0';
    log->lens[log->n_calls] = len;
    log->n_calls++;
  }
}

START_TEST(dash_live_init_segment_arrives_via_cb_before_media_segments) {
  unsigned mpd_port, init_port, seg_port;
  int mpd_fd = make_listener(&mpd_port);
  int init_fd = make_listener(&init_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t mpd_th, init_th, seg_th;
  char mpd_body[1024];
  char mpd_uri[64];
  const char init_payload[] = "INIT-FTYP-MOOV";
  const char seg1_payload[] = "SEG0001-BYTES";
  char init_resp[256], seg1_resp[256];
  size_t init_resp_len, seg1_resp_len;
  const char *mpd_responses[1];
  size_t mpd_lens[1];
  const char *init_responses[1];
  size_t init_lens[1];
  const char *seg_responses[1];
  size_t seg_lens[1];
  scripted_server_t mpd_srv, init_srv, seg_srv;
  http_url_t mpd_url;
  dash_live_t *h;
  cb_log_t log;
  unsigned char tmp[512];
  int fd, i;

  memset(&log, 0, sizeof log);
  init_resp_len = (size_t)snprintf(init_resp, sizeof init_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(init_payload), init_payload);
  seg1_resp_len = (size_t)snprintf(seg1_resp, sizeof seg1_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(seg1_payload), seg1_payload);

  snprintf(mpd_body, sizeof mpd_body,
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
    "<MPD type=\"static\">\n"
    "  <Period id=\"0\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\">\n"
    "      <Representation id=\"v\" bandwidth=\"3000000\">\n"
    "        <SegmentTemplate initialization=\"http://127.0.0.1:%u/init.mp4\" media=\"http://127.0.0.1:%u/seg$Number$.m4s\" timescale=\"1000\" duration=\"10\" startNumber=\"1\"/>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n",
    init_port, seg_port);

  mpd_responses[0] = mpd_body;
  mpd_lens[0] = strlen(mpd_body);
  mpd_srv.listen_fd = mpd_fd;
  mpd_srv.responses = mpd_responses;
  mpd_srv.response_lens = mpd_lens;
  mpd_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&mpd_th, NULL, serve_scripted, &mpd_srv), 0);

  init_responses[0] = init_resp;
  init_lens[0] = init_resp_len;
  init_srv.listen_fd = init_fd;
  init_srv.responses = init_responses;
  init_srv.response_lens = init_lens;
  init_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&init_th, NULL, serve_scripted, &init_srv), 0);

  seg_responses[0] = seg1_resp;
  seg_lens[0] = seg1_resp_len;
  seg_srv.listen_fd = seg_fd;
  seg_srv.responses = seg_responses;
  seg_srv.response_lens = seg_lens;
  seg_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  snprintf(mpd_uri, sizeof mpd_uri, "http://127.0.0.1:%u/live.mpd", mpd_port);
  ck_assert_int_eq(http_url_parse(mpd_uri, &mpd_url), 0);
  h = dash_live_new(&mpd_url, "test-agent", 0, 0, "test", 0, NULL, logging_cb, &log);
  ck_assert_ptr_nonnull(h);

  for (i = 0; i < 300 && log.n_calls < 2; i++) {
    struct pollfd pfd;
    ssize_t n;
    fd = dash_live_poll_fd(h);
    if (fd >= 0) {
      pfd.fd = fd;
      pfd.events = dash_live_poll_events(h);
      pfd.revents = 0;
      poll(&pfd, 1, 100);
    } else {
      struct timespec ts = {0, 20000000};
      nanosleep(&ts, NULL);
    }
    n = dash_live_read(h, tmp, sizeof tmp, NULL);
    (void)n;
  }

  ck_assert_int_ge(log.n_calls, 2);
  ck_assert_str_eq(log.calls[0], init_payload);
  ck_assert_str_eq(log.calls[1], seg1_payload);

  dash_live_free(h);
  pthread_join(mpd_th, NULL);
  pthread_join(init_th, NULL);
  pthread_join(seg_th, NULL);
  close(mpd_fd);
  close(init_fd);
  close(seg_fd);
}
END_TEST

#define RIG_MAX 6
#define MPD_FMT                                                                                                                  \
  "<MPD %s>\n<Period id=\"0\">\n<AdaptationSet mimeType=\"video/mp4\">\n<Representation id=\"v\" bandwidth=\"3000000\">\n"      \
  "<SegmentTemplate %s/>\n</Representation>\n</AdaptationSet>\n</Period>\n</MPD>\n"
#define TMPL_FAST "initialization=\"init.mp4\" media=\"seg$Number$.m4s\" timescale=\"1000\" duration=\"10\" startNumber=\"1\""
#define TMPL_SLOW "initialization=\"init.mp4\" media=\"seg$Number$.m4s\""

typedef struct {
  int fd;
  unsigned port;
  pthread_t th;
  scripted_server_t srv;
  char buf[RIG_MAX][1536];
  const char *resp[RIG_MAX];
  size_t lens[RIG_MAX];
  int n;
  dash_live_t *h;
} rig_t;

static void rig_init(rig_t *r) {
  memset(r, 0, sizeof *r);
  r->fd = make_listener(&r->port);
}

static void rig_add_with(rig_t *r, const char *conn, const void *body, size_t len) {
  size_t hl;

  ck_assert_int_lt(r->n, RIG_MAX);
  hl = (size_t)snprintf(r->buf[r->n], sizeof r->buf[0], "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n%s\r\n", len, conn);
  ck_assert_uint_le(hl + len, sizeof r->buf[0]);
  memcpy(r->buf[r->n] + hl, body, len);
  r->resp[r->n] = r->buf[r->n];
  r->lens[r->n] = hl + len;
  r->n++;
}

static void rig_add(rig_t *r, const void *body, size_t len) { rig_add_with(r, "Connection: close\r\n", body, len); }

static void rig_add_text(rig_t *r, const char *text) { rig_add(r, text, strlen(text)); }

static void rig_add_raw(rig_t *r, const char *text) {
  ck_assert_int_lt(r->n, RIG_MAX);
  r->resp[r->n] = text;
  r->lens[r->n] = strlen(text);
  r->n++;
}

static void rig_add_mpd_with(rig_t *r, const char *conn, const char *attrs, const char *tmpl) {
  char mpd[1024];

  snprintf(mpd, sizeof mpd, MPD_FMT, attrs, tmpl);
  rig_add_with(r, conn, mpd, strlen(mpd));
}

static void rig_add_mpd(rig_t *r, const char *attrs, const char *tmpl) { rig_add_mpd_with(r, "Connection: close\r\n", attrs, tmpl); }

static void rig_open(rig_t *r, unsigned as_index, const dash_insp_t *si) {
  char uri[64];
  http_url_t url;

  r->srv.listen_fd = r->fd;
  r->srv.responses = r->resp;
  r->srv.response_lens = r->lens;
  r->srv.n_responses = r->n;
  ck_assert_int_eq(pthread_create(&r->th, NULL, serve_scripted, &r->srv), 0);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/live.mpd", r->port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  r->h = dash_live_new(&url, "test-agent", 0, 0, "test", as_index, si, passthrough_cb, NULL);
  ck_assert_ptr_nonnull(r->h);
}

static void rig_close(rig_t *r) {
  dash_live_free(r->h);
  pthread_join(r->th, NULL);
  close(r->fd);
}

static ssize_t pump_once(dash_live_t *h, unsigned char *tmp, size_t cap, net_err_reason_t *reason) {
  struct pollfd pfd;
  int fd = dash_live_poll_fd(h);

  if (fd >= 0) {
    pfd.fd = fd;
    pfd.events = dash_live_poll_events(h);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
  } else {
    struct timespec ts = {0, 20000000};
    nanosleep(&ts, NULL);
  }
  return dash_live_read(h, tmp, cap, reason);
}

static int pump_until_error(dash_live_t *h, int max_iters, net_err_reason_t *reason) {
  for (int i = 0; i < max_iters; i++) {
    unsigned char tmp[64];
    if (pump_once(h, tmp, sizeof tmp, reason) < 0) return 1;
  }
  return 0;
}

static void pump_one_fetch(dash_live_t *h) {
  int seen = 0;

  for (int i = 0; i < 300; i++) {
    unsigned char tmp[1024];
    ck_assert_int_ge(pump_once(h, tmp, sizeof tmp, NULL), 0);
    if (dash_live_poll_fd(h) >= 0) seen = 1;
    else if (seen) return;
  }
  ck_abort_msg("fetch did not finish");
}

static void fill_null_packets(unsigned char *out, size_t pkts) {
  memset(out, 0xFF, pkts * 188);
  for (size_t i = 0; i < pkts; i++) {
    out[i * 188 + 0] = 0x47;
    out[i * 188 + 1] = 0x1F;
    out[i * 188 + 2] = 0xFF;
    out[i * 188 + 3] = 0x10;
  }
}

START_TEST(dash_live_emit_drops_when_buffer_full) {
  static unsigned char big[2 * 1024 * 1024 + 64];
  http_url_t url;
  dash_live_t *h;

  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/live.mpd", &url), 0);
  h = dash_live_new(&url, NULL, 0, 0, NULL, 0, NULL, passthrough_cb, NULL);
  ck_assert_ptr_nonnull(h);
  dash_live_emit(h, big, sizeof big);
  dash_live_emit(h, big, 16);
  ck_assert_int_eq(dash_live_has_buffered(h), 1);
  ck_assert_int_eq(dash_live_poll_fd(h), -1);
  ck_assert_int_eq(dash_live_poll_events(h), 0);
  dash_live_free(h);
}
END_TEST

static void run_idle_case(rig_t *r) {
  unsigned char tmp[64];
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_open(r, 0, NULL);
  ck_assert_int_eq(dash_live_has_buffered(r->h), 1);
  ck_assert_int_eq(dash_live_read(r->h, tmp, sizeof tmp, &reason), 0);
  ck_assert_int_ge(dash_live_poll_fd(r->h), 0);
  ck_assert_int_ne(dash_live_poll_events(r->h), 0);
  ck_assert_int_eq(dash_live_has_buffered(r->h), 0);
  ck_assert_int_eq(pump_until_error(r->h, 15, &reason), 0);
  ck_assert_int_eq(dash_live_poll_fd(r->h), -1);
  ck_assert_int_eq(dash_live_has_buffered(r->h), 0);
  rig_close(r);
}

START_TEST(dash_live_stays_idle_on_not_modified) {
  rig_t r;

  rig_init(&r);
  rig_add_raw(&r, "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n");
  run_idle_case(&r);
}
END_TEST

START_TEST(dash_live_stays_idle_on_empty_mpd) {
  rig_t r;

  rig_init(&r);
  rig_add(&r, "", 0);
  run_idle_case(&r);
}
END_TEST

static void run_format_case(const char *mpd, unsigned as_index) {
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_text(&r, mpd);
  rig_open(&r, as_index, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 50, &reason), 1);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  rig_close(&r);
}

START_TEST(dash_live_mpd_without_mpd_tag_is_format_error) { run_format_case("not xml at all\n", 0); }
END_TEST

START_TEST(dash_live_mpd_without_periods_is_format_error) { run_format_case("<MPD type=\"static\">\n</MPD>\n", 0); }
END_TEST

START_TEST(dash_live_adaptation_set_index_out_of_range_is_format_error) {
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_mpd(&r, "type=\"static\"", TMPL_FAST);
  rig_open(&r, 1, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 50, &reason), 1);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  rig_close(&r);
}
END_TEST

START_TEST(dash_live_adaptation_set_without_representation_is_format_error) {
  run_format_case("<MPD type=\"static\">\n<Period id=\"0\">\n<AdaptationSet mimeType=\"video/mp4\">\n</AdaptationSet>\n</Period>\n</MPD>\n", 0);
}
END_TEST

START_TEST(dash_live_uses_default_cadence_and_start_number) {
  rig_t r;
  unsigned char acc[256];
  size_t len = 0;

  rig_init(&r);
  rig_add_mpd(&r, "type=\"static\"", TMPL_SLOW);
  rig_add_text(&r, "INITDATA");
  rig_add_text(&r, "SEGONE");
  rig_open(&r, 0, NULL);
  ck_assert_int_eq(drive_until_len(r.h, acc, sizeof acc, &len, 14, 300), 1);
  ck_assert_mem_eq(acc, "INITDATASEGONE", 14);
  rig_close(&r);
}
END_TEST

START_TEST(dash_live_inspects_media_segments) {
  rig_t r;
  unsigned char seg[188 * 4];
  unsigned char acc[2048];
  size_t len = 0;
  tsinspect_t *insp = NULL;
  dash_insp_t si = {&insp, METRICS_INSPECT_TS_BASIC, NULL, 0};

  fill_null_packets(seg, 4);
  rig_init(&r);
  rig_add_mpd(&r, "type=\"static\"", TMPL_SLOW);
  rig_add_text(&r, "INITDATA");
  rig_add(&r, seg, sizeof seg);
  rig_open(&r, 0, &si);
  ck_assert_int_eq(drive_until_len(r.h, acc, sizeof acc, &len, 8 + sizeof seg, 300), 1);
  for (int i = 0; i < 3; i++) {
    unsigned char tmp[64];
    ck_assert_int_ge(pump_once(r.h, tmp, sizeof tmp, NULL), 0);
  }
  ck_assert_ptr_nonnull(insp);
  ck_assert_uint_eq(tsinspect_counters(insp)->packets, 4);
  rig_close(&r);
  tsinspect_free(insp);
}
END_TEST

START_TEST(dash_live_repolls_mpd_and_refetches_changed_init) {
  rig_t r;
  unsigned char acc[256];
  size_t len = 0;
  char mpd2[1024];

  snprintf(mpd2, sizeof mpd2, MPD_FMT, "type=\"dynamic\" minimumUpdatePeriod=\"PT0.3S\"", "initialization=\"init2.mp4\" media=\"seg$Number$.m4s\"");
  rig_init(&r);
  rig_add_mpd_with(&r, "ETag: \"v1\"\r\nConnection: close\r\n", "type=\"dynamic\" minimumUpdatePeriod=\"PT0.3S\"", TMPL_SLOW);
  rig_add_text(&r, "INIT1");
  rig_add_text(&r, "SEG1");
  rig_add_text(&r, mpd2);
  rig_add_text(&r, "INIT2");
  rig_add_text(&r, "SEG2");
  rig_open(&r, 0, NULL);
  ck_assert_int_eq(drive_until_len(r.h, acc, sizeof acc, &len, 18, 400), 1);
  ck_assert_mem_eq(acc, "INIT1SEG1INIT2SEG2", 18);
  rig_close(&r);
}
END_TEST

START_TEST(dash_live_has_buffered_tracks_phase) {
  rig_t r;
  unsigned char tmp[64];
  struct timespec ts = {1, 200000000};

  rig_init(&r);
  rig_add_mpd(&r, "type=\"dynamic\" minimumUpdatePeriod=\"PT1S\"", TMPL_SLOW);
  rig_add_text(&r, "INIT");
  rig_add_text(&r, "SEG");
  rig_open(&r, 0, NULL);
  ck_assert_int_eq(dash_live_has_buffered(r.h), 1);
  ck_assert_int_eq(dash_live_read(r.h, tmp, sizeof tmp, NULL), 0);
  ck_assert_int_eq(dash_live_has_buffered(r.h), 0);
  pump_one_fetch(r.h);
  ck_assert_int_eq(dash_live_has_buffered(r.h), 1);
  pump_one_fetch(r.h);
  ck_assert_int_eq(dash_live_has_buffered(r.h), 1);
  pump_one_fetch(r.h);
  ck_assert_int_eq(dash_live_has_buffered(r.h), 0);
  nanosleep(&ts, NULL);
  ck_assert_int_eq(dash_live_has_buffered(r.h), 1);
  rig_close(&r);
}
END_TEST

static void run_bad_url_case(const char *tmpl, int serve_init) {
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_mpd_with(&r, serve_init ? "Connection: close\r\n" : "", "type=\"static\"", tmpl);
  if (serve_init) rig_add_with(&r, "", "INITDATA", 8);
  rig_open(&r, 0, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 100, &reason), 1);
  ck_assert_int_eq(reason, NET_ERR_OTHER);
  rig_close(&r);
}

START_TEST(dash_live_fails_on_unparsable_init_url) {
  run_bad_url_case("initialization=\"http://127.0.0.1:99999/init.mp4\" media=\"seg$Number$.m4s\"", 0);
}
END_TEST

START_TEST(dash_live_fails_on_unparsable_segment_url) {
  run_bad_url_case("initialization=\"init.mp4\" media=\"http://127.0.0.1:99999/seg$Number$.m4s\"", 1);
}
END_TEST

START_TEST(dash_live_reports_init_fetch_error) {
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_mpd(&r, "type=\"static\"", TMPL_SLOW);
  rig_add_raw(&r, "");
  rig_open(&r, 0, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 100, &reason), 1);
  ck_assert_int_ne(reason, NET_ERR_COUNT);
  rig_close(&r);
}
END_TEST

static Suite *dash_live_suite(void) {
  Suite *s = suite_create("dash_live");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, dash_live_fetches_init_then_sequential_number_segments);
  tcase_add_test(tc, dash_live_init_segment_arrives_via_cb_before_media_segments);
  tcase_add_test(tc, dash_live_emit_drops_when_buffer_full);
  tcase_add_test(tc, dash_live_stays_idle_on_not_modified);
  tcase_add_test(tc, dash_live_stays_idle_on_empty_mpd);
  tcase_add_test(tc, dash_live_mpd_without_mpd_tag_is_format_error);
  tcase_add_test(tc, dash_live_mpd_without_periods_is_format_error);
  tcase_add_test(tc, dash_live_adaptation_set_index_out_of_range_is_format_error);
  tcase_add_test(tc, dash_live_adaptation_set_without_representation_is_format_error);
  tcase_add_test(tc, dash_live_uses_default_cadence_and_start_number);
  tcase_add_test(tc, dash_live_inspects_media_segments);
  tcase_add_test(tc, dash_live_repolls_mpd_and_refetches_changed_init);
  tcase_add_test(tc, dash_live_has_buffered_tracks_phase);
  tcase_add_test(tc, dash_live_fails_on_unparsable_init_url);
  tcase_add_test(tc, dash_live_fails_on_unparsable_segment_url);
  tcase_add_test(tc, dash_live_reports_init_fetch_error);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dash_live_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
