/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/rtcp.h"
#include "lib/mux/rtx.h"
#include "lib/helper/signal.h"
#include "lib/fccret/ret_client.h"

static ret_client_t *open_client(unsigned nack_port, unsigned wait_ms) {
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

static rtp_hdr_t make_hdr(uint32_t ssrc, uint16_t seq) {
  rtp_hdr_t h;
  memset(&h, 0, sizeof h);
  h.ssrc = ssrc;
  h.seq = seq;
  return h;
}

/* ret_client_read() needs non-NULL mcast_t always. scratch port: no data,
   5ms timeout, returns 0. */
static mcast_t *open_scratch_main(unsigned port) {
  return mcast_open(AF_INET, "239.7.9.71", port, NULL, 5);
}

/* timestamps fed to on_original/on_repair must be real mono_seconds(): deadline
   checks use real time internally. */

START_TEST(in_order_packets_pass_straight_through) {
  ret_client_t *r = open_client(15401, 50);
  mcast_t *main_ = open_scratch_main(15501);
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xAAAA, 10);
  on_original(r, &h, (const unsigned char *)"pkt10", 5, now);
  h = make_hdr(0xAAAA, 11);
  on_original(r, &h, (const unsigned char *)"pkt11", 5, now);

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "pkt10", 5);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "pkt11", 5);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(stale_duplicate_is_dropped) {
  ret_client_t *r = open_client(15402, 50);
  mcast_t *main_ = open_scratch_main(15502);
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xAAAA, 10);
  on_original(r, &h, (const unsigned char *)"pkt10", 5, now);
  ret_client_read(r, main_, buf, sizeof buf); /* consume it, expected_seq now 11 */

  h = make_hdr(0xAAAA, 10); /* replay, same seq */
  on_original(r, &h, (const unsigned char *)"stale", 5, mono_seconds());

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 0); /* nothing queued */

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(gap_repaired_by_rtx_packet_flushes_in_order) {
  ret_client_t *r = open_client(15403, 50); /* 50ms hold budget: plenty of headroom for this test */
  mcast_t *main_ = open_scratch_main(15503);
  unsigned char buf[64];
  unsigned char rtx[128];
  _Atomic uint16_t rxc_seq = 0;
  size_t rtxlen;
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xBBBB, 5);
  on_original(r, &h, (const unsigned char *)"seq5", 4, now); /* expected_seq -> 6 */
  h = make_hdr(0xBBBB, 7); /* seq 6 missing */
  on_original(r, &h, (const unsigned char *)"seq7", 4, now);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4); /* seq5, released immediately */
  ck_assert_mem_eq(buf, "seq5", 4);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 0); /* seq7 held, gap pending */

  rtxlen = rtx_build(&rxc_seq, 0xBBBB, 99, 90000, 6, (const unsigned char *)"seq6", 4, rtx, sizeof rtx);
  ck_assert_uint_gt(rtxlen, 0u);
  on_repair(r, rtx, rtxlen, mono_seconds());

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "seq6", 4);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "seq7", 4);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(gap_not_repaired_in_time_drops_the_lost_seq_only) {
  ret_client_t *r = open_client(15404, 10); /* 10ms hold budget */
  mcast_t *main_ = open_scratch_main(15504);
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  struct timespec wait = {0, 30 * 1000 * 1000}; /* 30ms: past 10ms budget */
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xCCCC, 20);
  on_original(r, &h, (const unsigned char *)"seq20", 5, now);
  h = make_hdr(0xCCCC, 22); /* seq 21 missing */
  on_original(r, &h, (const unsigned char *)"seq22", 5, now);
  ret_client_read(r, main_, buf, sizeof buf); /* seq20 */

  nanosleep(&wait, NULL); /* hold deadline elapses */

  /* flush_ready() drops timed-out seq21 before flushing rest of queue */
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "seq22", 5); /* seq21 lost, not corrupted */
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 0);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(ssrc_change_resets_tracking_and_abandons_pending_gap) {
  ret_client_t *r = open_client(15405, 50);
  mcast_t *main_ = open_scratch_main(15505);
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0x1111, 1);
  on_original(r, &h, (const unsigned char *)"a1", 2, now);
  h = make_hdr(0x1111, 3); /* seq 2 missing: gap opens on ssrc 0x1111, a3 held pending its repair */
  on_original(r, &h, (const unsigned char *)"a3", 2, now);
  ret_client_read(r, main_, buf, sizeof buf); /* a1 */
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 0); /* a3 held, gap pending */

  h = make_hdr(0x2222, 50); /* new ssrc: gap forced closed, tracking restarts at 50 */
  on_original(r, &h, (const unsigned char *)"b50", 3, mono_seconds());

  /* abandon_gap() flushes a3 (received), drops seq2 (never received) */
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 2);
  ck_assert_mem_eq(buf, "a3", 2);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 3);
  ck_assert_mem_eq(buf, "b50", 3);
  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 0);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(gap_too_large_resyncs_without_holding) {
  ret_client_t *r = open_client(15406, 50);
  mcast_t *main_ = open_scratch_main(15506);
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xDDDD, 1);
  on_original(r, &h, (const unsigned char *)"d1", 2, now);
  ret_client_read(r, main_, buf, sizeof buf);

  h = make_hdr(0xDDDD, 1000); /* far beyond RET_GAP_MAX: resync immediately, no hold */
  on_original(r, &h, (const unsigned char *)"d1000", 5, mono_seconds());

  ck_assert_int_eq(ret_client_read(r, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "d1000", 5);

  ret_client_close(r);
  mcast_close(main_);
}
END_TEST

START_TEST(sequence_wraps_from_65535_to_0_without_gap) {
  ret_client_t *r = open_client(15407, 50);
  mcast_t *main_ = open_scratch_main(15507);
  unsigned char buf[64];
  rtp_hdr_t h;
  double now = mono_seconds();
  ck_assert_ptr_nonnull(r);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xEEEE, 65535);
  on_original(r, &h, (const unsigned char *)"last", 4, now);
  h = make_hdr(0xEEEE, 0);
  on_original(r, &h, (const unsigned char *)"wrap", 4, mono_seconds());

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
  mcast_t *main_ = open_scratch_main(15508);
  unsigned char rbuf[256];
  rtp_hdr_t h;
  ssize_t n;
  double now;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(15409);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ck_assert_int_ge(listener, 0);
  ck_assert_int_eq(bind(listener, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_ptr_nonnull(main_);

  r = open_client(15409, 50);
  ck_assert_ptr_nonnull(r);

  now = mono_seconds();
  h = make_hdr(0xF0F0, 100);
  on_original(r, &h, (const unsigned char *)"x", 1, now);
  h = make_hdr(0xF0F0, 103); /* 2 missing: 101, 102 */
  on_original(r, &h, (const unsigned char *)"y", 1, now);

  n = recv(listener, rbuf, sizeof rbuf, 0);
  ck_assert_int_gt(n, 0);

  g_nack_count = 0;
  rtcp_parse(rbuf, (size_t)n, nack_cb, NULL, NULL, NULL, NULL, NULL, NULL);
  ck_assert_int_eq(g_nack_count, 1);
  ck_assert_uint_eq(g_last_nack.entry_count, 1u);
  ck_assert_uint_eq(g_last_nack.entry[0].pid, 101u);
  ck_assert_uint_eq(g_last_nack.entry[0].blp, 0x0001u); /* bit0 set: pid+1 (102) also missing */

  ret_client_close(r);
  mcast_close(main_);
  close(listener);
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
