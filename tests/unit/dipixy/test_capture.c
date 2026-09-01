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
    capture_service(c);
    usleep(5000);
  }
  return capture_reader_read(r, buf, cap);
}

START_TEST(dedup_shares_context_for_same_key) {
  capture_ctx_t *a = capture_open(AF_INET, "239.8.8.1", 15801, NULL, 0, NULL, NULL);
  capture_ctx_t *b = capture_open(AF_INET, "239.8.8.1", 15801, NULL, 0, NULL, NULL);
  capture_ctx_t *c = capture_open(AF_INET, "239.8.8.1", 15802, NULL, 0, NULL, NULL);

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
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.2", 15802, NULL, 0, NULL, NULL);
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
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.3", 15803, NULL, 0, NULL, NULL);
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
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.4", 15804, NULL, 1, NULL, NULL);
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
  capture_ctx_t *cap = capture_open(AF_INET, "239.8.8.5", 15805, NULL, 1, NULL, NULL);
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
  volatile int accepts;
} http_dedup_server_t;

static void *http_dedup_server_thread(void *arg) {
  http_dedup_server_t *s = arg;
  static const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n\r\n";
  for (;;) {
    int fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0)
      return NULL;
    s->accepts++;
    send(fd, resp, sizeof resp - 1, MSG_NOSIGNAL);
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
  capture_drain(a->cap, drain_sink, a->d);
  return NULL;
}

static void run_drain_until(capture_ctx_t *cap, drain_thread_ctx_t *d) {
  pthread_t tid;
  drain_args_t a;
  a.cap = cap;
  a.d = d;
  pthread_create(&tid, NULL, drain_thread_fn, &a);
  for (int i = 0; i < 60 && !atomic_load(&d->got); i++)
    usleep(5000);
  pthread_cancel(tid);
  pthread_join(tid, NULL);
}

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

  cap = capture_open(AF_INET, "239.8.8.6", 15806, NULL, 1, &ret, NULL);
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
  fcc.port = 15907; /* no real FCC server: burst never arrives, plain multicast still cuts over */
  fcc.rtx_pt = 99;

  cap = capture_open(AF_INET, "239.8.8.7", 15807, NULL, 1, NULL, &fcc);
  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(cap);

  make_rtp_header(dgram);
  dgram[3] = 1;
  make_ts_packet(ts, 4);
  memcpy(dgram + 12, ts, 188);
  ck_assert_int_eq(mcast_send(send, dgram, sizeof dgram), (int)sizeof dgram);

  run_drain_until(cap, &d);
  ck_assert_int_eq(d.got, 1);
  ck_assert_int_eq(memcmp(d.pkt, ts, 188), 0);

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
