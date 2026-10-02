/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <net/if.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "lib/demux/rtcp.h"
#include "lib/mux/rtx.h"
#include "lib/sys/signal.h"
#include "lib/fccret/ret_client.h"

static ret_client_t *open_client_at(unsigned nack_port, unsigned wait_ms) {
  ret_client_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.addr, "127.0.0.1");
  cfg.port = nack_port;
  cfg.mc_enabled = 0;
  cfg.rtx_pt = 99;
  cfg.wait_ms = wait_ms;
  return ret_client_open(&cfg);
}

static ret_client_t *open_client(unsigned wait_ms) {
  return open_client_at(9, wait_ms);
}

static rtp_hdr_t make_hdr(uint32_t ssrc, uint16_t seq) {
  rtp_hdr_t h;
  memset(&h, 0, sizeof h);
  h.ssrc = ssrc;
  h.seq = seq;
  return h;
}

/* ret_client_read() needs non-NULL mcast_t always. scratch port: no data,
   5ms timeout, returns 0. */
static mcast_t *open_scratch_main(void) {
  return mcast_open(AF_INET, "239.7.9.71", 0, NULL, 5);
}

static void on_alarm(int sig) {
  (void)sig;
}

static int read_quiet(ret_client_t *r, mcast_t *main_, unsigned char *buf, size_t cap) {
  struct sigaction sa;
  struct itimerval it;

  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_alarm;
  sigemptyset(&sa.sa_mask);
  ck_assert_int_eq(sigaction(SIGALRM, &sa, NULL), 0);
  memset(&it, 0, sizeof it);
  it.it_value.tv_usec = 20000;
  ck_assert_int_eq(setitimer(ITIMER_REAL, &it, NULL), 0);
  return (int)ret_client_read(r, main_, buf, cap);
}

START_TEST(in_order_packets_pass_straight_through) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xAAAA, 10);
  ret_client_on_original(r, &h, (const unsigned char *)"pkt10", 5, now);
  h = make_hdr(0xAAAA, 11);
  ret_client_on_original(r, &h, (const unsigned char *)"pkt11", 5, now);

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "pkt10", 5);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "pkt11", 5);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(stale_duplicate_is_dropped) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xAAAA, 10);
  ret_client_on_original(r, &h, (const unsigned char *)"pkt10", 5, now);
  ret_client_read(r, main_, buf, sizeof buf); /* consume it, expected_seq now 11 */

  h = make_hdr(0xAAAA, 10); /* replay, same seq */
  ret_client_on_original(r, &h, (const unsigned char *)"stale", 5, mono_seconds());

  ck_assert_int_eq(read_quiet(r, main_, buf, sizeof buf), 0); /* nothing queued */

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(gap_repaired_by_rtx_packet_flushes_in_order) {
  ret_client_t *r = open_client(50); /* 50ms hold budget: plenty of headroom for this test */
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64];
  unsigned char rtx[128];
  _Atomic uint16_t rxc_seq = 0;
  size_t rtxlen;
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xBBBB, 5);
  ret_client_on_original(r, &h, (const unsigned char *)"seq5", 4, now); /* expected_seq -> 6 */
  h = make_hdr(0xBBBB, 7); /* seq 6 missing */
  ret_client_on_original(r, &h, (const unsigned char *)"seq7", 4, now);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4); /* seq5, released immediately */
  ck_assert_mem_eq(buf, "seq5", 4);
  ck_assert_int_eq(read_quiet(r, main_, buf, sizeof buf), 0); /* seq7 held, gap pending */

  rtxlen = rtx_build(&rxc_seq, 0xBBBB, 99, 90000, 6, (const unsigned char *)"seq6", 4, rtx, sizeof rtx);
  ck_assert_uint_gt(rtxlen, 0u);
  ret_client_on_repair(r, rtx, rtxlen, mono_seconds());

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "seq6", 4);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "seq7", 4);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(gap_not_repaired_in_time_drops_the_lost_seq_only) {
  ret_client_t *r = open_client(10); /* 10ms hold budget */
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  struct timespec wait = {0, 30 * 1000 * 1000}; /* 30ms: past 10ms budget */
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xCCCC, 20);
  ret_client_on_original(r, &h, (const unsigned char *)"seq20", 5, now);
  h = make_hdr(0xCCCC, 22); /* seq 21 missing */
  ret_client_on_original(r, &h, (const unsigned char *)"seq22", 5, now);
  ret_client_read(r, main_, buf, sizeof buf); /* seq20 */

  nanosleep(&wait, NULL); /* hold deadline elapses */

  /* ret_client_flush_ready() drops timed-out seq21 before flushing rest of queue */
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "seq22", 5); /* seq21 lost, not corrupted */
  ck_assert_int_eq(read_quiet(r, main_, buf, sizeof buf), 0);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(ssrc_change_resets_tracking_and_abandons_pending_gap) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0x1111, 1);
  ret_client_on_original(r, &h, (const unsigned char *)"a1", 2, now);
  h = make_hdr(0x1111, 3); /* seq 2 missing: gap opens on ssrc 0x1111, a3 held pending its repair */
  ret_client_on_original(r, &h, (const unsigned char *)"a3", 2, now);
  ret_client_read(r, main_, buf, sizeof buf); /* a1 */
  ck_assert_int_eq(read_quiet(r, main_, buf, sizeof buf), 0); /* a3 held, gap pending */

  h = make_hdr(0x2222, 50); /* new ssrc: gap forced closed, tracking restarts at 50 */
  ret_client_on_original(r, &h, (const unsigned char *)"b50", 3, mono_seconds());

  /* abandon_gap() flushes a3 (received), drops seq2 (never received) */
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 2);
  ck_assert_mem_eq(buf, "a3", 2);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 3);
  ck_assert_mem_eq(buf, "b50", 3);
  ck_assert_int_eq(read_quiet(r, main_, buf, sizeof buf), 0);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(gap_too_large_resyncs_without_holding) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xDDDD, 1);
  ret_client_on_original(r, &h, (const unsigned char *)"d1", 2, now);
  ret_client_read(r, main_, buf, sizeof buf);

  h = make_hdr(0xDDDD, 1000); /* far beyond RET_GAP_MAX: resync immediately, no hold */
  ret_client_on_original(r, &h, (const unsigned char *)"d1000", 5, mono_seconds());

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "d1000", 5);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(sequence_wraps_from_65535_to_0_without_gap) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xEEEE, 65535);
  ret_client_on_original(r, &h, (const unsigned char *)"last", 4, now);
  h = make_hdr(0xEEEE, 0);
  ret_client_on_original(r, &h, (const unsigned char *)"wrap", 4, mono_seconds());

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "last", 4);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "wrap", 4);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

static int g_nack_count;
static rtcp_nack_t g_last_nack;

static void nack_cb(const rtcp_nack_t *n, void *user) {
  (void)user;
  g_nack_count++;
  g_last_nack = *n;
}

START_TEST(a_gap_sends_a_real_nack_on_the_wire) {
  int listener = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr;
  ret_client_t *r;
  mcast_t *main_ = open_scratch_main();
  unsigned char rbuf[256];
  rtp_hdr_t h;
  ssize_t n;
  double now;
  socklen_t alen = sizeof addr;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ck_assert_int_ge(listener, 0);
  ck_assert_int_eq(bind(listener, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(listener, (struct sockaddr *)&addr, &alen), 0);
  ck_assert_ptr_nonnull(main_);

  r = open_client_at(ntohs(addr.sin_port), 50);
  ck_assert_ptr_nonnull(r);

  now = mono_seconds();
  h = make_hdr(0xF0F0, 100);
  ret_client_on_original(r, &h, (const unsigned char *)"x", 1, now);
  h = make_hdr(0xF0F0, 103); /* 2 missing: 101, 102 */
  ret_client_on_original(r, &h, (const unsigned char *)"y", 1, now);

  n = recv(listener, rbuf, sizeof rbuf, 0);
  ck_assert_int_gt(n, 0);

  g_nack_count = 0;
  rtcp_parse(rbuf, (size_t)n, &(rtcp_cbs_t){.nack_cb = nack_cb});
  ck_assert_int_eq(g_nack_count, 1);
  ck_assert_uint_eq(g_last_nack.entry_count, 1u);
  ck_assert_uint_eq(g_last_nack.entry[0].pid, 101u);
  ck_assert_uint_eq(g_last_nack.entry[0].blp, 0x0001u); /* bit0 set: pid+1 (102) also missing */

  ret_client_close(r);
  mcast_close(main_);
  close(listener);
}
END_TEST

#define HOLD_CAP 1500
#define GAP_MAX 32
#define OUTQ_SLOTS (GAP_MAX + 1)
#define OUTQ_CAP 65536
static void feed(ret_client_t *r, uint32_t ssrc, uint16_t seq, size_t len, unsigned char fill) {
  static unsigned char payload[OUTQ_CAP + 16];
  rtp_hdr_t h = make_hdr(ssrc, seq);

  memset(payload, fill, len);
  ret_client_on_original(r, &h, payload, len, mono_seconds());
}

static void expect_pop(ret_client_t *r, mcast_t *main_, size_t len, unsigned char fill) {
  unsigned char buf[OUTQ_CAP];
  ssize_t n = ret_client_read(r, main_, buf, sizeof buf);

  ck_assert_int_eq(n, (int)len);
  ck_assert_uint_eq(buf[0], fill);
  ck_assert_uint_eq(buf[len - 1], fill);
}

static size_t build_repair(uint32_t ssrc, uint16_t osn, size_t len, unsigned char fill, unsigned char *out, size_t cap) {
  static unsigned char payload[HOLD_CAP + 64];
  _Atomic uint16_t seq = 0;

  memset(payload, fill, len);
  return rtx_build(&seq, ssrc, 99, 90000, osn, payload, len, out, cap);
}

START_TEST(oversized_hold_payload_ends_gap_logs_once_and_rearms) {
  ret_client_t *r = open_client(5000);
  mcast_t *main_ = open_scratch_main();
  char log[LOG_CAPTURE_BUF];

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  log_capture_begin();
  feed(r, 0x51, 10, 4, 10);
  feed(r, 0x51, 12, 4, 12);
  feed(r, 0x51, 13, HOLD_CAP + 1, 13);
  feed(r, 0x51, 15, 4, 15);
  feed(r, 0x51, 16, HOLD_CAP + 1, 16);
  feed(r, 0x51, 18, 4, 18);
  feed(r, 0x51, 19, 4, 19);
  feed(r, 0x51, 20, HOLD_CAP + 1, 20);
  log_capture_end(log, sizeof log);

  ck_assert_int_eq(log_count_of(log, "exceeds hold cap"), 2);
  expect_pop(r, main_, 4, 10);
  expect_pop(r, main_, 4, 12);
  expect_pop(r, main_, HOLD_CAP + 1, 13);
  expect_pop(r, main_, 4, 15);
  expect_pop(r, main_, HOLD_CAP + 1, 16);
  expect_pop(r, main_, 4, 18);
  expect_pop(r, main_, 4, 19);
  expect_pop(r, main_, HOLD_CAP + 1, 20);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(gap_window_grows_to_the_cap_then_flushes) {
  ret_client_t *r = open_client(5000);
  mcast_t *main_ = open_scratch_main();
  unsigned seq;

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  feed(r, 0x52, 100, 2, 100);
  expect_pop(r, main_, 2, 100);
  for (seq = 102; seq < 102 + GAP_MAX - 1; seq++)
    feed(r, 0x52, (uint16_t)seq, 2, (unsigned char)seq);

  feed(r, 0x52, 102 + GAP_MAX - 1, 2, (unsigned char)(102 + GAP_MAX - 1));
  for (seq = 102; seq < 102 + GAP_MAX; seq++)
    expect_pop(r, main_, 2, (unsigned char)seq);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(disjoint_packet_abandons_gap_and_opens_a_new_one) {
  ret_client_t *r = open_client(5000);
  mcast_t *main_ = open_scratch_main();
  char log[LOG_CAPTURE_BUF];

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  feed(r, 0x53, 10, 2, 10);
  expect_pop(r, main_, 2, 10);
  log_capture_begin();
  feed(r, 0x53, 12, 2, 12);
  feed(r, 0x53, 20, 2, 20);
  log_capture_end(log, sizeof log);

  ck_assert_int_eq(log_count_of(log, "seq 11 lost, gap abandoned"), 1);
  expect_pop(r, main_, 2, 12);
  ret_client_flush_ready(r, mono_seconds() + 10.0);
  expect_pop(r, main_, 2, 20);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(stale_and_duplicate_packets_during_a_gap_are_ignored) {
  ret_client_t *r = open_client(5000);
  mcast_t *main_ = open_scratch_main();

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  feed(r, 0x54, 10, 2, 10);
  expect_pop(r, main_, 2, 10);
  feed(r, 0x54, 12, 2, 0x12);
  feed(r, 0x54, 9, 2, 0x99);
  feed(r, 0x54, 12, 2, 0xEE);
  feed(r, 0x54, 11, 2, 0x11);
  expect_pop(r, main_, 2, 0x11);
  expect_pop(r, main_, 2, 0x12);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(oversized_repair_payload_is_dropped_logged_once_and_rearmed) {
  ret_client_t *r = open_client(5000);
  mcast_t *main_ = open_scratch_main();
  unsigned char pkt[HOLD_CAP + 128];
  size_t n;
  char log[LOG_CAPTURE_BUF];

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  feed(r, 0x55, 10, 2, 10);
  expect_pop(r, main_, 2, 10);
  feed(r, 0x55, 14, 2, 14);
  log_capture_begin();
  n = build_repair(0x55, 11, HOLD_CAP + 1, 0x11, pkt, sizeof pkt);
  ck_assert_uint_gt(n, 0u);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  n = build_repair(0x55, 12, HOLD_CAP + 1, 0x12, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  n = build_repair(0x55, 11, 8, 0x21, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  n = build_repair(0x55, 12, HOLD_CAP + 1, 0x12, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  log_capture_end(log, sizeof log);

  ck_assert_int_eq(log_count_of(log, "repair payload"), 2);
  expect_pop(r, main_, 8, 0x21);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(repair_for_other_ssrc_stale_osn_or_filled_slot_is_ignored) {
  ret_client_t *r = open_client(5000);
  mcast_t *main_ = open_scratch_main();
  unsigned char pkt[64];
  size_t n;

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  ret_client_on_repair(r, (const unsigned char *)"x", 1, mono_seconds());
  feed(r, 0x56, 10, 2, 10);
  expect_pop(r, main_, 2, 10);
  ret_client_on_repair(r, (const unsigned char *)"x", 1, mono_seconds());
  feed(r, 0x56, 12, 2, 0x12);
  n = build_repair(0x57, 11, 2, 0x31, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  n = build_repair(0x56, 9, 2, 0x32, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  n = build_repair(0x56, 40, 2, 0x33, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  n = build_repair(0x56, 12, 2, 0x34, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  n = build_repair(0x56, 11, 2, 0x35, pkt, sizeof pkt);
  ret_client_on_repair(r, pkt, n, mono_seconds());
  expect_pop(r, main_, 2, 0x35);
  expect_pop(r, main_, 2, 0x12);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(outq_full_backstop_drops_and_logs) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  char log[LOG_CAPTURE_BUF];
  unsigned i;

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  log_capture_begin();
  for (i = 0; i < OUTQ_SLOTS + 1; i++)
    feed(r, 0x58, (uint16_t)(1000 + i), 2, (unsigned char)i);
  log_capture_end(log, sizeof log);

  ck_assert_int_eq(log_count_of(log, "outq full"), 1);
  for (i = 0; i < OUTQ_SLOTS; i++)
    expect_pop(r, main_, 2, (unsigned char)i);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(queued_payload_over_outq_cap_is_clipped) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  static unsigned char buf[OUTQ_CAP + 16];

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  feed(r, 0x59, 1, OUTQ_CAP + 8, 0x5A);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), OUTQ_CAP);
  feed(r, 0x59, 2, 100, 0x5B);
  ck_assert_int_eq(ret_client_read(r, main_, buf, 10), 10);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(read_waits_for_gap_deadline_then_releases_held_packet) {
  ret_client_t *r = open_client(20);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[16];
  double t0;
  double dt;
  int calls = 0;

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  feed(r, 0x5C, 10, 2, 10);
  expect_pop(r, main_, 2, 10);
  feed(r, 0x5C, 12, 2, 12);
  t0 = mono_seconds();
  for (;;) {
    ssize_t n = ret_client_read(r, main_, buf, sizeof buf);
    calls++;
    if (n > 0) {
      ck_assert_int_eq(n, 2);
      ck_assert_uint_eq(buf[0], 12u);
      break;
    }
    ck_assert_int_eq(n, 0);
    ck_assert_double_lt(mono_seconds() - t0, 5.0);
  }
  dt = mono_seconds() - t0;
  ck_assert_double_ge(dt, 0.015);
  ck_assert_int_le(calls, 5);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(read_interrupted_by_signal_returns_zero) {
  ret_client_t *r = open_client(50);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[16];

  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);
  ck_assert_int_eq(read_quiet(r, main_, buf, sizeof buf), 0);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(unicast_repair_arrives_through_read) {
  int server = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr;
  struct sockaddr_in client;
  socklen_t alen = sizeof addr;
  socklen_t clen = sizeof client;
  unsigned char nack[128];
  unsigned char pkt[64];
  unsigned char buf[64];
  ret_client_t *r;
  mcast_t *main_ = open_scratch_main();
  size_t n;
  int i;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ck_assert_int_ge(server, 0);
  ck_assert_int_eq(bind(server, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(server, (struct sockaddr *)&addr, &alen), 0);
  ck_assert_ptr_nonnull(main_);
  r = open_client_at(ntohs(addr.sin_port), 2000);
  ck_assert_ptr_nonnull(r);

  feed(r, 0x5D, 10, 2, 10);
  expect_pop(r, main_, 2, 10);
  feed(r, 0x5D, 12, 2, 12);
  ck_assert_int_gt(recvfrom(server, nack, sizeof nack, 0, (struct sockaddr *)&client, &clen), 0);
  n = build_repair(0x5D, 11, 6, 0x66, pkt, sizeof pkt);
  ck_assert_int_eq(sendto(server, pkt, n, 0, (struct sockaddr *)&client, clen), (int)n);

  for (i = 0; i < 20; i++) {
    ssize_t got = ret_client_read(r, main_, buf, sizeof buf);
    if (got > 0) {
      ck_assert_int_eq(got, 6);
      ck_assert_uint_eq(buf[0], 0x66u);
      break;
    }
  }
  ck_assert_int_lt(i, 20);
  expect_pop(r, main_, 2, 12);
  ret_client_close(r);
  mcast_close(main_);
  close(server);
}
END_TEST

START_TEST(open_fails_on_bad_server_address_and_family_mismatch) {
  ret_client_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.addr, "not-an-address");
  cfg.port = 9;
  ck_assert_ptr_null(ret_client_open(&cfg));
  strcpy(cfg.addr, "::1");
  ck_assert_ptr_null(ret_client_open(&cfg));
  cfg.family = -1;
  ck_assert_ptr_null(ret_client_open(&cfg));
}
END_TEST

START_TEST(open_with_unusable_mc_session_fails_cleanly) {
  ret_client_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.addr, "127.0.0.1");
  cfg.port = 9;
  cfg.mc_enabled = 1;
  cfg.source_family = AF_INET;
  strcpy(cfg.source_group, "not-a-group");
  cfg.source_port = 5000;
  ck_assert_ptr_null(ret_client_open(&cfg));
  strcpy(cfg.source_group, "239.7.9.72");
  cfg.iface_in = "nonexistent-if0";
  ck_assert_ptr_null(ret_client_open(&cfg));
}
END_TEST

static int enter_private_multicast_netns(void) {
  struct ifreq ifr;
  int fd;
  int rc;

  if (unshare(CLONE_NEWUSER | CLONE_NEWNET) < 0)
    return -1;
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return -1;
  memset(&ifr, 0, sizeof ifr);
  snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "lo");
  ifr.ifr_flags = IFF_UP | IFF_RUNNING | IFF_LOOPBACK | IFF_MULTICAST;
  rc = ioctl(fd, SIOCSIFFLAGS, &ifr);
  close(fd);
  return rc;
}

static unsigned probe_free_udp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

START_TEST(multicast_repair_session_delivers_repair_over_ssm) {
  ret_client_cfg_t cfg;
  ret_client_t *r;
  mcast_t *main_;
  struct sockaddr_in txsrc;
  struct sockaddr_in dst;
  struct in_addr oif;
  int tx;
  const char *group = "239.7.9.73";
  unsigned port;
  unsigned char pkt[64];
  unsigned char buf[64];
  size_t n;
  int i;

  if (enter_private_multicast_netns() < 0) {
    fprintf(stderr, "private network namespace unavailable, MC repair test not run\n");
    return;
  }
  port = probe_free_udp_port();
  main_ = mcast_open(AF_INET, "239.7.9.71", 0, "lo", 5);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.addr, "127.0.0.1");
  cfg.port = 9;
  cfg.mc_enabled = 1;
  cfg.rtx_pt = 99;
  cfg.wait_ms = 2000;
  cfg.source_family = AF_INET;
  snprintf(cfg.source_group, sizeof cfg.source_group, "%s", group);
  cfg.source_port = port;
  cfg.iface_in = "lo";
  ck_assert_ptr_nonnull(main_);
  r = ret_client_open(&cfg);
  ck_assert_ptr_nonnull(r);
  tx = socket(AF_INET, SOCK_DGRAM, 0);
  ck_assert_int_ge(tx, 0);
  memset(&txsrc, 0, sizeof txsrc);
  txsrc.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &txsrc.sin_addr);
  ck_assert_int_eq(bind(tx, (struct sockaddr *)&txsrc, sizeof txsrc), 0);
  oif = txsrc.sin_addr;
  ck_assert_int_eq(setsockopt(tx, IPPROTO_IP, IP_MULTICAST_IF, &oif, sizeof oif), 0);
  dst = txsrc;
  dst.sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, group, &dst.sin_addr);

  feed(r, 0x5E, 10, 2, 10);
  expect_pop(r, main_, 2, 10);
  feed(r, 0x5E, 12, 2, 12);
  n = build_repair(0x5E, 11, 5, 0x77, pkt, sizeof pkt);
  ck_assert_int_eq(sendto(tx, pkt, n, 0, (struct sockaddr *)&dst, sizeof dst), (int)n);

  for (i = 0; i < 20; i++) {
    ssize_t got = ret_client_read(r, main_, buf, sizeof buf);
    if (got > 0) {
      ck_assert_int_eq(got, 5);
      ck_assert_uint_eq(buf[0], 0x77u);
      break;
    }
  }
  ck_assert_int_lt(i, 20);
  expect_pop(r, main_, 2, 12);
  close(tx);
  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

static Suite *ret_client_suite(void) {
  Suite *s = suite_create("dipirec_ret_client");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, in_order_packets_pass_straight_through);
  tcase_add_test(tc, stale_duplicate_is_dropped);
  tcase_add_test(tc, gap_repaired_by_rtx_packet_flushes_in_order);
  tcase_add_test(tc, gap_not_repaired_in_time_drops_the_lost_seq_only);
  tcase_add_test(tc, ssrc_change_resets_tracking_and_abandons_pending_gap);
  tcase_add_test(tc, gap_too_large_resyncs_without_holding);
  tcase_add_test(tc, sequence_wraps_from_65535_to_0_without_gap);
  tcase_add_test(tc, a_gap_sends_a_real_nack_on_the_wire);
  tcase_add_test(tc, oversized_hold_payload_ends_gap_logs_once_and_rearms);
  tcase_add_test(tc, gap_window_grows_to_the_cap_then_flushes);
  tcase_add_test(tc, disjoint_packet_abandons_gap_and_opens_a_new_one);
  tcase_add_test(tc, stale_and_duplicate_packets_during_a_gap_are_ignored);
  tcase_add_test(tc, oversized_repair_payload_is_dropped_logged_once_and_rearmed);
  tcase_add_test(tc, repair_for_other_ssrc_stale_osn_or_filled_slot_is_ignored);
  tcase_add_test(tc, outq_full_backstop_drops_and_logs);
  tcase_add_test(tc, queued_payload_over_outq_cap_is_clipped);
  tcase_add_test(tc, read_waits_for_gap_deadline_then_releases_held_packet);
  tcase_add_test(tc, read_interrupted_by_signal_returns_zero);
  tcase_add_test(tc, unicast_repair_arrives_through_read);
  tcase_add_test(tc, open_fails_on_bad_server_address_and_family_mismatch);
  tcase_add_test(tc, open_with_unusable_mc_session_fails_cleanly);
  tcase_add_test(tc, multicast_repair_session_delivers_repair_over_ssm);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ret_client_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
