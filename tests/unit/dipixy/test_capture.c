/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/net/multicast.h"

#include "dipixy/ts/capture/capture.h"
#include "lib/helper/sds_xml.h"

static void make_ts_packet(unsigned char pkt[188], unsigned char marker) {
  memset(pkt, 0xAB, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = marker;
  pkt[3] = 0x10;
}

static void make_rtp_header(unsigned char hdr[12]) {
  memset(hdr, 0, 12);
  hdr[0] = 0x80; /* version 2, no padding/extension, cc=0 */
  hdr[1] = 33;   /* MP2T payload type */
}

static size_t drain(capture_ctx_t *c, capture_reader_t *r, unsigned char *buf, size_t cap) {
  for (int i = 0; i < 40; i++) {
    size_t n;
    capture_service(c);
    n = capture_reader_read(r, buf, cap);
    if (n > 0)
      return n;
    usleep(5000);
  }
  return capture_reader_read(r, buf, cap);
}

START_TEST(dedup_shares_context_for_same_key) {
  capture_ctx_t *a = capture_open(AF_INET, "239.8.8.1", NULL, 15801, NULL, 0, NULL, NULL, NULL, 0, 0);
  capture_ctx_t *b = capture_open(AF_INET, "239.8.8.1", NULL, 15801, NULL, 0, NULL, NULL, NULL, 0, 0);
  capture_ctx_t *c = capture_open(AF_INET, "239.8.8.1", NULL, 15802, NULL, 0, NULL, NULL, NULL, 0, 0);

  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_eq(a, b);
  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_ne(a, c);

  capture_close(a);
  capture_close(b);
  capture_close(c);
}
END_TEST

START_TEST(two_readers_on_same_context_each_see_all_bytes) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.2", 15802, NULL, 1);
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.2", NULL, 15802, NULL, 0, NULL, NULL, NULL, 0, 0);
  capture_reader_t *r1, *r2;
  unsigned char dgram[188], out1[512], out2[512];

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);
  r1 = capture_reader_open(cap);
  r2 = capture_reader_open(cap);
  ck_assert_ptr_nonnull(r1);
  ck_assert_ptr_nonnull(r2);

  make_ts_packet(dgram, 9);
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);

  ck_assert_uint_eq(drain(cap, r1, out1, sizeof out1), sizeof dgram);
  ck_assert_int_eq(memcmp(out1, dgram, sizeof dgram), 0);
  /* r2 opened before the send, so it independently sees same bytes w/o another capture_service() pass (buffered) */
  ck_assert_uint_eq(capture_reader_read(r2, out2, sizeof out2), sizeof dgram);
  ck_assert_int_eq(memcmp(out2, dgram, sizeof dgram), 0);

  capture_reader_close(r1);
  capture_reader_close(r2);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

START_TEST(plain_udp_two_ts_packets_round_trip) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.3", 15803, NULL, 1);
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.3", NULL, 15803, NULL, 0, NULL, NULL, NULL, 0, 0);
  capture_reader_t *r = capture_reader_open(cap);
  unsigned char dgram[376], out[512];
  size_t n;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);
  ck_assert_ptr_nonnull(r);
  make_ts_packet(dgram, 1);
  make_ts_packet(dgram + 188, 2);
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);

  n = drain(cap, r, out, sizeof out);
  ck_assert_uint_eq(n, sizeof dgram);
  ck_assert_int_eq(memcmp(out, dgram, sizeof dgram), 0);
  ck_assert_uint_eq(capture_reader_dropped(r), 0u);

  capture_reader_close(r);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

START_TEST(rtp_wrapped_packet_has_header_stripped) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.4", 15804, NULL, 1);
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.4", NULL, 15804, NULL, 1, NULL, NULL, NULL, 0, 0);
  capture_reader_t *r = capture_reader_open(cap);
  unsigned char dgram[12 + 188], ts[188], out[512];
  size_t n;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);
  ck_assert_ptr_nonnull(r);
  make_rtp_header(dgram);
  make_ts_packet(ts, 7);
  memcpy(dgram + 12, ts, 188);
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);

  n = drain(cap, r, out, sizeof out);
  ck_assert_uint_eq(n, 188u);
  ck_assert_int_eq(memcmp(out, ts, 188), 0);

  capture_reader_close(r);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

START_TEST(non_rtp_payload_dropped_when_rtp_expected) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.5", 15805, NULL, 1);
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.5", NULL, 15805, NULL, 1, NULL, NULL, NULL, 0, 0);
  capture_reader_t *r = capture_reader_open(cap);
  unsigned char junk[20], out[512];
  size_t n;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);
  ck_assert_ptr_nonnull(r);
  memset(junk, 0x00, sizeof junk); /* version bits 0, not RTP v2 */
  ck_assert_int_eq(mcast_send(send, junk, sizeof junk), (int)sizeof junk);

  n = drain(cap, r, out, sizeof out);
  ck_assert_uint_eq(n, 0u);

  capture_reader_close(r);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

typedef struct {
  int listen_fd;
  _Atomic int accepts;
} http_dedup_server_t;

static void http_send_ts_reply(int fd) {
  static const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n\r\n";

  send(fd, resp, sizeof resp - 1, MSG_NOSIGNAL);
  for (unsigned k = 0; k < 3; k++) {
    unsigned char pkt[188];

    make_ts_packet(pkt, (unsigned char)k);
    send(fd, pkt, sizeof pkt, MSG_NOSIGNAL);
  }
}

static void *http_dedup_server_thread(void *arg) {
  http_dedup_server_t *s = arg;
  for (;;) {
    int fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0)
      return NULL;
    s->accepts++;
    http_send_ts_reply(fd);
  }
}

START_TEST(http_static_dedup_shares_context_for_same_url) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof addr;
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  pthread_t tid;
  http_dedup_server_t srv;
  char url[64];
  capture_ctx_t *a, *b;

  ck_assert_int_ge(listen_fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(listen_fd, 4), 0);
  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen), 0);
  snprintf(url, sizeof url, "http://127.0.0.1:%u/stream", (unsigned)ntohs(addr.sin_port));

  srv.listen_fd = listen_fd;
  srv.accepts = 0;
  ck_assert_int_eq(pthread_create(&tid, NULL, http_dedup_server_thread, &srv), 0);

  a = capture_open_http_static(url, 0);
  b = capture_open_http_static(url, 0);

  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_eq(a, b);
  ck_assert_int_eq(srv.accepts, 1);

  capture_close(a);
  capture_close(b);
  pthread_cancel(tid);
  pthread_join(tid, NULL);
  close(listen_fd);
}
END_TEST

typedef struct {
  unsigned char pkt[188];
  atomic_int got;
} drain_thread_ctx_t;

static void drain_sink(void *user, const unsigned char *pkt) {
  drain_thread_ctx_t *d = user;
  memcpy(d->pkt, pkt, 188);
  atomic_store(&d->got, 1);
}

typedef struct {
  capture_ctx_t *cap;
  drain_thread_ctx_t *d;
} drain_args_t;

static void *drain_thread_fn(void *arg) {
  drain_args_t *a = arg;
  const struct timespec nap = {0, 1000000L};
  for (;;) {
    capture_drain(a->cap, drain_sink, a->d);
    nanosleep(&nap, NULL);
  }
  return NULL;
}

static void run_drain_for(capture_ctx_t *cap, drain_thread_ctx_t *d, int polls) {
  pthread_t tid;
  drain_args_t a;
  a.cap = cap;
  a.d = d;
  pthread_create(&tid, NULL, drain_thread_fn, &a);
  for (int i = 0; i < polls && !atomic_load(&d->got); i++)
    usleep(5000);
  pthread_cancel(tid);
  pthread_join(tid, NULL);
}

static void run_drain_until(capture_ctx_t *cap, drain_thread_ctx_t *d) { run_drain_for(cap, d, 60); }

START_TEST(ret_wired_capture_delivers_plain_packet) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.6", 15806, NULL, 1);
  sds_ret_t ret;
  capture_ctx_t *cap;
  unsigned char dgram[12 + 188], ts[188];
  drain_thread_ctx_t d = {{0}, 0};

  memset(&ret, 0, sizeof ret);
  strcpy(ret.addr, "127.0.0.1");
  ret.port = 15906; /* UDP connect() succeeds with no listener; NACKs just go nowhere */
  ret.rtx_pt = 99;

  cap = capture_open(AF_INET, "239.8.8.6", NULL, 15806, NULL, 1, &ret, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);

  make_rtp_header(dgram);
  dgram[3] = 1; /* seq 1 */
  make_ts_packet(ts, 3);
  memcpy(dgram + 12, ts, 188);
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);

  run_drain_until(cap, &d);
  ck_assert_int_eq(d.got, 1);
  ck_assert_int_eq(memcmp(d.pkt, ts, 188), 0);

  mcast_close(send);
  capture_close(cap);
}
END_TEST

START_TEST(fcc_wired_capture_delivers_plain_multicast_after_cutover) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.7", 15807, NULL, 1);
  sds_fcc_t fcc;
  capture_ctx_t *cap;
  unsigned char dgram[12 + 188], ts[188];
  drain_thread_ctx_t d = {{0}, 0};

  memset(&fcc, 0, sizeof fcc);
  strcpy(fcc.addr, "127.0.0.1");
  fcc.port = 15907; /* no real FCC server: burst never arrives, queued multicast comes out after the burst wait */
  fcc.rtx_pt = 99;

  cap = capture_open(AF_INET, "239.8.8.7", NULL, 15807, NULL, 1, NULL, &fcc, NULL, 0, 0);
  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);

  make_rtp_header(dgram);
  dgram[3] = 1;
  make_ts_packet(ts, 4);
  memcpy(dgram + 12, ts, 188);
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);

  run_drain_for(cap, &d, 400);
  ck_assert_int_eq(d.got, 1);
  ck_assert_int_eq(memcmp(d.pkt, ts, 188), 0);

  mcast_close(send);
  capture_close(cap);
}
END_TEST

START_TEST(http_static_rejects_an_invalid_url_and_an_unreachable_server) {
  ck_assert_ptr_null(capture_open_http_static("not a url", 0));
  ck_assert_ptr_null(capture_open_http_static("http://127.0.0.1:1/stream", 0));
}
END_TEST

START_TEST(srt_open_to_a_closed_port_fails) {
  ck_assert_ptr_null(capture_open_srt("127.0.0.1", 1));
}
END_TEST

START_TEST(rist_source_is_optional) {
  ck_assert_int_eq(capture_rist_init(NULL), 0);
  ck_assert_ptr_null(capture_rist_get());
  if (capture_rist_init("rist://@127.0.0.1:0") == 0) {
    capture_ctx_t *c = capture_rist_get();

    ck_assert_ptr_nonnull(c);
    capture_close(c);
  }
}
END_TEST

START_TEST(stdin_source_is_shared_once_initialised) {
  int pipefd[2];
  capture_ctx_t *c;

  ck_assert_ptr_null(capture_stdin_get());
  ck_assert_int_eq(pipe(pipefd), 0);
  ck_assert_int_ge(dup2(pipefd[0], STDIN_FILENO), 0);
  ck_assert_int_eq(capture_stdin_init(), 0);
  c = capture_stdin_get();
  ck_assert_ptr_nonnull(c);
  capture_close(c);
  close(pipefd[1]);
}
END_TEST

START_TEST(reader_that_falls_behind_loses_the_oldest_bytes) {
  enum { DGRAM = 1316, SENDS = 6 };
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.5", 15805, NULL, 1);
  capture_ctx_t *cap;
  capture_reader_t *r;
  unsigned char dgram[DGRAM];
  unsigned char out[4 * DGRAM];
  size_t n;

  capture_set_ring_cap(2 * DGRAM);
  cap = capture_open(AF_INET, "239.8.8.5", NULL, 15805, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);
  r = capture_reader_open(cap);
  ck_assert_ptr_nonnull(r);

  memset(dgram, 0, sizeof dgram);
  for (int i = 0; i < SENDS; i++) {
    for (size_t k = 0; k < sizeof dgram; k += 188) make_ts_packet(dgram + k, (unsigned char)(i + 1));
    ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), DGRAM);
    usleep(2000);
  }
  for (int i = 0; i < 40; i++) {
    capture_service(cap);
    usleep(2000);
  }
  n = capture_reader_read(r, out, sizeof out);
  ck_assert_uint_eq(n, (size_t)(2 * DGRAM));
  ck_assert_uint_gt(capture_reader_dropped(r), 0u);
  ck_assert_uint_eq(out[2], (unsigned char)(SENDS - 1));
  ck_assert_uint_eq(out[DGRAM + 2], (unsigned char)SENDS);

  capture_reader_close(r);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

START_TEST(datagram_larger_than_the_ring_keeps_only_its_tail) {
  enum { DGRAM = 1316, RING = 564 };
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.6", 15806, NULL, 1);
  capture_ctx_t *cap;
  capture_reader_t *r;
  unsigned char dgram[DGRAM];
  unsigned char out[2 * DGRAM];

  capture_set_ring_cap(RING);
  cap = capture_open(AF_INET, "239.8.8.6", NULL, 15806, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);
  r = capture_reader_open(cap);
  for (size_t i = 0; i < sizeof dgram; i++) dgram[i] = (unsigned char)i;
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), DGRAM);

  ck_assert_uint_eq(drain(cap, r, out, sizeof out), (size_t)RING);
  ck_assert_int_eq(memcmp(out, dgram + DGRAM - RING, RING), 0);

  capture_reader_close(r);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

typedef struct {
  int listen_fd;
  size_t chunk;
  size_t total;
} chunked_server_t;

static void *chunked_stream_server_thread(void *arg) {
  const chunked_server_t *s = arg;
  static const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n\r\n";
  unsigned char body[188 * 4];
  int fd = accept(s->listen_fd, NULL, NULL);

  if (fd < 0) return NULL;
  for (size_t k = 0; k < sizeof body; k += 188) make_ts_packet(body + k, (unsigned char)(k / 188));
  send(fd, resp, sizeof resp - 1, MSG_NOSIGNAL);
  for (size_t off = 0; off < s->total; off += s->chunk) {
    size_t n = s->total - off < s->chunk ? s->total - off : s->chunk;

    send(fd, body + off, n, MSG_NOSIGNAL);
    usleep(20000);
  }
  usleep(500000);
  close(fd);
  return NULL;
}

typedef struct {
  unsigned count;
  unsigned char marks[8];
} sink_count_t;

static void count_sink(void *user, const unsigned char *pkt) {
  sink_count_t *c = user;

  if (c->count < sizeof c->marks) c->marks[c->count] = pkt[2];
  c->count++;
}

START_TEST(http_stream_reassembles_packets_split_across_reads) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof addr;
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  pthread_t tid;
  chunked_server_t srv;
  sink_count_t got;
  char url[64];
  capture_ctx_t *cap;

  ck_assert_int_ge(listen_fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(listen_fd, 1), 0);
  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen), 0);
  snprintf(url, sizeof url, "http://127.0.0.1:%u/stream", (unsigned)ntohs(addr.sin_port));
  srv.listen_fd = listen_fd;
  srv.chunk = 100;
  srv.total = 188 * 3 + 50;
  ck_assert_int_eq(pthread_create(&tid, NULL, chunked_stream_server_thread, &srv), 0);

  memset(&got, 0, sizeof got);
  cap = capture_open_http_static(url, 0);
  ck_assert_ptr_nonnull(cap);
  for (int i = 0; i < 100 && got.count < 3; i++) {
    capture_drain(cap, count_sink, &got);
    usleep(10000);
  }
  ck_assert_uint_eq(got.count, 3u);
  ck_assert_uint_eq(got.marks[0], 0u);
  ck_assert_uint_eq(got.marks[1], 1u);
  ck_assert_uint_eq(got.marks[2], 2u);

  capture_close(cap);
  pthread_join(tid, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(rtp_source_drain_unwraps_and_skips_non_rtp_datagrams) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.7", 15807, NULL, 1);
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.7", NULL, 15807, NULL, 1, NULL, NULL, NULL, 0, 0);
  unsigned char junk[188];
  unsigned char rtp[12 + 188];
  sink_count_t got;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);
  make_ts_packet(junk, 1);
  ck_assert_int_eq(mcast_send(send, junk, sizeof junk), (int)sizeof junk);
  make_rtp_header(rtp);
  make_ts_packet(rtp + 12, 5);
  ck_assert_int_eq(mcast_send(send, rtp, sizeof rtp), (int)sizeof rtp);

  memset(&got, 0, sizeof got);
  for (int i = 0; i < 40 && got.count < 1; i++) {
    capture_drain(cap, count_sink, &got);
    usleep(5000);
  }
  ck_assert_uint_eq(got.count, 1u);
  ck_assert_uint_eq(got.marks[0], 5u);

  mcast_close(send);
  capture_close(cap);
}
END_TEST

static _Atomic int g_woken_tid = -1;

static void wake_cb(int tid) { atomic_store(&g_woken_tid, tid); }

static int wait_job(const capture_open_job_t *job) {
  for (int i = 0; i < 3000; i++) {
    if (capture_open_job_done(job)) return 1;
    nanosleep(&(const struct timespec){0, 1000000L}, NULL);
  }
  return 0;
}

START_TEST(async_open_unavailable_before_lifecycle_start) {
  ck_assert_ptr_null(capture_open_async(0, AF_INET, "239.8.8.30", NULL, 15830, NULL, 0, NULL, NULL, NULL, 0, 0));
  ck_assert_ptr_null(capture_open_srt_async(0, "127.0.0.1", 15830));
  ck_assert_ptr_null(capture_try_ref(AF_INET, "239.8.8.30", NULL, 15830, NULL));
}
END_TEST

START_TEST(async_open_completes_wakes_and_dedups) {
  capture_open_job_t *job;
  capture_ctx_t *ctx;
  capture_ctx_t *again;

  capture_pump_set_thread_count(1);
  ck_assert_int_eq(capture_lifecycle_start(wake_cb), 0);
  job = capture_open_async(7, AF_INET, "239.8.8.31", NULL, 15831, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(job);
  ck_assert(wait_job(job));
  ck_assert_int_eq(atomic_load(&g_woken_tid), 7);
  ctx = capture_open_job_take(job);
  ck_assert_ptr_nonnull(ctx);
  again = capture_try_ref(AF_INET, "239.8.8.31", NULL, 15831, NULL);
  ck_assert_ptr_eq(ctx, again);
  ck_assert_int_eq(capture_active_count(), 1);
  capture_close(again);
  capture_close(ctx);
  ck_assert_int_eq(capture_active_count(), 0);
  capture_lifecycle_stop();
}
END_TEST

START_TEST(close_returns_without_waiting_for_pump_quiescence) {
  capture_ctx_t *ctx;

  capture_pump_set_thread_count(1);
  ck_assert_int_eq(capture_lifecycle_start(wake_cb), 0);
  ctx = capture_open(AF_INET, "239.8.8.32", NULL, 15832, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(ctx);
  capture_close(ctx); /* no pump thread runs: a blocking close never returns */
  ck_assert_int_eq(capture_active_count(), 0);
  capture_lifecycle_stop();
}
END_TEST

START_TEST(cancelled_job_closes_its_result) {
  capture_open_job_t *job;

  capture_pump_set_thread_count(1);
  ck_assert_int_eq(capture_lifecycle_start(wake_cb), 0);
  job = capture_open_async(0, AF_INET, "239.8.8.33", NULL, 15833, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(job);
  capture_open_job_cancel(job);
  for (int i = 0; i < 3000 && capture_active_count() > 0; i++) nanosleep(&(const struct timespec){0, 1000000L}, NULL);
  ck_assert_int_eq(capture_active_count(), 0);
  capture_lifecycle_stop();
}
END_TEST

START_TEST(async_http_open_failure_is_remembered_and_refused_for_a_while) {
  capture_open_job_t *job;
  char url[96];
  char longurl[2200];
  snprintf(url, sizeof url, "http://127.0.0.1:1/never.ts");
  memset(longurl, 'u', sizeof longurl - 1);
  longurl[sizeof longurl - 1] = '\0';
  capture_pump_set_thread_count(1);
  ck_assert_int_eq(capture_lifecycle_start(wake_cb), 0);
  ck_assert_ptr_null(capture_open_http_async(0, longurl, 0));
  job = capture_open_http_async(5, url, 0);
  ck_assert_ptr_nonnull(job);
  ck_assert(wait_job(job));
  ck_assert_ptr_null(capture_open_job_take(job));
  ck_assert_ptr_null(capture_open_http_async(5, url, 0));
  capture_lifecycle_stop();
}
END_TEST

static void *http_slow_server_thread(void *arg) {
  http_dedup_server_t *s = arg;
  int fd = accept(s->listen_fd, NULL, NULL);

  if (fd < 0) return NULL;
  s->accepts++;
  nanosleep(&(const struct timespec){0, 300000000L}, NULL);
  http_send_ts_reply(fd);
  nanosleep(&(const struct timespec){1, 0}, NULL);
  close(fd);
  return NULL;
}

START_TEST(async_open_cancelled_mid_flight_releases_the_opened_context) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof addr;
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  pthread_t tid;
  http_dedup_server_t srv;
  capture_open_job_t *job;
  char url[64];

  ck_assert_int_ge(listen_fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(listen_fd, 4), 0);
  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen), 0);
  snprintf(url, sizeof url, "http://127.0.0.1:%u/slow", (unsigned)ntohs(addr.sin_port));
  srv.listen_fd = listen_fd;
  srv.accepts = 0;
  ck_assert_int_eq(pthread_create(&tid, NULL, http_slow_server_thread, &srv), 0);

  capture_pump_set_thread_count(1);
  ck_assert_int_eq(capture_lifecycle_start(wake_cb), 0);
  job = capture_open_http_async(5, url, 0);
  ck_assert_ptr_nonnull(job);
  nanosleep(&(const struct timespec){0, 100000000L}, NULL);
  capture_open_job_cancel(job);
  for (int i = 0; i < 3000 && !(srv.accepts == 1 && capture_active_count() == 0); i++) nanosleep(&(const struct timespec){0, 1000000L}, NULL);
  ck_assert_int_eq(srv.accepts, 1);
  ck_assert_int_eq(capture_active_count(), 0);
  capture_lifecycle_stop();
  pthread_join(tid, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(al_fec_repair_stream_that_cannot_open_is_dropped_but_capture_works) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.41", 15841, NULL, 1);
  sds_fec_t fec;
  capture_ctx_t *cap;
  capture_reader_t *r;
  unsigned char dgram[12 + 188], out[512];
  memset(&fec, 0, sizeof fec);
  snprintf(fec.addr, sizeof fec.addr, "not-a-group");
  fec.port = 15842;
  cap = capture_open(AF_INET, "239.8.8.41", NULL, 15841, NULL, 1, NULL, NULL, &fec, 5, 2);
  ck_assert_ptr_nonnull(cap);
  r = capture_reader_open(cap);
  make_rtp_header(dgram);
  make_ts_packet(dgram + 12, 3);
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);
  ck_assert_uint_eq(drain(cap, r, out, sizeof out), 188u);
  capture_reader_close(r);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

START_TEST(al_fec_decoder_passes_in_order_source_packets) {
  mcast_t *send = mcast_open_send(AF_INET, "239.8.8.42", 15843, NULL, 1);
  sds_fec_t fec;
  capture_ctx_t *cap;
  capture_reader_t *r;
  unsigned char dgram[12 + 188], out[2048];
  size_t total = 0;
  memset(&fec, 0, sizeof fec);
  snprintf(fec.addr, sizeof fec.addr, "239.8.8.42");
  fec.port = 15845;
  cap = capture_open(AF_INET, "239.8.8.42", NULL, 15843, NULL, 1, NULL, NULL, &fec, 5, 2);
  ck_assert_ptr_nonnull(cap);
  r = capture_reader_open(cap);
  for (unsigned i = 0; i < 12; i++) {
    make_rtp_header(dgram);
    dgram[2] = (unsigned char)(i >> 8);
    dgram[3] = (unsigned char)i;
    make_ts_packet(dgram + 12, (unsigned char)(10 + i));
    ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);
    usleep(2000);
  }
  for (int i = 0; i < 100 && total < 188; i++) {
    size_t n = drain(cap, r, out + total, sizeof out - total);
    total += n;
    if (!n) usleep(5000);
  }
  ck_assert_uint_ge(total, 188u);
  capture_reader_close(r);
  mcast_close(send);
  capture_close(cap);
}
END_TEST

static Suite *capture_suite(void) {
  Suite *s = suite_create("dipixy_capture");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, dedup_shares_context_for_same_key);
  tcase_add_test(tc, two_readers_on_same_context_each_see_all_bytes);
  tcase_add_test(tc, plain_udp_two_ts_packets_round_trip);
  tcase_add_test(tc, rtp_wrapped_packet_has_header_stripped);
  tcase_add_test(tc, non_rtp_payload_dropped_when_rtp_expected);
  tcase_add_test(tc, http_static_dedup_shares_context_for_same_url);
  tcase_add_test(tc, ret_wired_capture_delivers_plain_packet);
  tcase_add_test(tc, fcc_wired_capture_delivers_plain_multicast_after_cutover);
  tcase_add_test(tc, http_static_rejects_an_invalid_url_and_an_unreachable_server);
  tcase_add_test(tc, srt_open_to_a_closed_port_fails);
  tcase_add_test(tc, rist_source_is_optional);
  tcase_add_test(tc, stdin_source_is_shared_once_initialised);
  tcase_add_test(tc, reader_that_falls_behind_loses_the_oldest_bytes);
  tcase_add_test(tc, datagram_larger_than_the_ring_keeps_only_its_tail);
  tcase_add_test(tc, http_stream_reassembles_packets_split_across_reads);
  tcase_add_test(tc, rtp_source_drain_unwraps_and_skips_non_rtp_datagrams);
  tcase_add_test(tc, al_fec_repair_stream_that_cannot_open_is_dropped_but_capture_works);
  tcase_add_test(tc, al_fec_decoder_passes_in_order_source_packets);
  tcase_add_test(tc, async_open_unavailable_before_lifecycle_start);
  tcase_add_test(tc, async_open_completes_wakes_and_dedups);
  tcase_add_test(tc, close_returns_without_waiting_for_pump_quiescence);
  tcase_add_test(tc, cancelled_job_closes_its_result);
  tcase_add_test(tc, async_http_open_failure_is_remembered_and_refused_for_a_while);
  tcase_add_test(tc, async_open_cancelled_mid_flight_releases_the_opened_context);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(capture_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
