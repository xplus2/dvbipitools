/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "dipifccret/fcc/burst.h"
#include "lib/demux/rtx.h"

static channel_table_t *g_table;

/* channel_lookup() takes raw address bytes now, not text */
static channel_t *lookup_ip(channel_table_t *t, const char *ip, unsigned port) {
  unsigned char addr[4];
  inet_pton(AF_INET, ip, addr);
  return channel_lookup(t, AF_INET, addr, sizeof addr, port);
}

static channel_t *make_channel_with_rap(size_t cache_cap, int n_extra_entries) {
  channel_t *c;
  unsigned char pkt[188];
  int i;

  g_table = channel_table_new(1, 0, cache_cap);
  c = lookup_ip(g_table, "239.1.1.1", 5000);

  /* fake RAP: channel.c already covers detection, just need have_rap=1 */
  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  atomic_store_explicit(&c->cache.have_rap, 1, memory_order_relaxed);
  channel_store(g_table, c, 0x1234, 1, 0, pkt, sizeof pkt); /* entry 0: becomes "RAP" once have_rap seen */
  for (i = 0; i < n_extra_entries; i++)
    channel_store(g_table, c, 0x1234, (uint16_t)(2 + i), 0, pkt, sizeof pkt);
  return c;
}

START_TEST(burst_decide_rejects_null_channel) {
  rtcp_rams_r_t req;
  memset(&req, 0, sizeof req);
  ck_assert_int_eq(burst_decide(NULL, &req), BURST_REJECT);
}
END_TEST

START_TEST(burst_decide_rejects_when_no_rap) {
  channel_table_t *t = channel_table_new(1, 0, 8); /* FCC cache, but nothing stored yet */
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  rtcp_rams_r_t req;
  memset(&req, 0, sizeof req);
  ck_assert_int_eq(burst_decide(c, &req), BURST_REJECT);
  channel_table_free(t);
}
END_TEST

START_TEST(burst_decide_flags_malformed_buffer_fill_range) {
  channel_t *c = make_channel_with_rap(8, 0);
  rtcp_rams_r_t req;
  memset(&req, 0, sizeof req);
  req.has_min_buffer_fill = 1;
  req.min_buffer_fill_ms = 500;
  req.has_max_buffer_fill = 1;
  req.max_buffer_fill_ms = 100; /* min > max */
  ck_assert_int_eq(burst_decide(c, &req), BURST_MALFORMED);
  channel_table_free(g_table);
}
END_TEST

START_TEST(burst_decide_rejects_insufficient_client_bitrate) {
  channel_t *c = make_channel_with_rap(8, 0);
  rtcp_rams_r_t req;
  atomic_store_explicit(&c->nominal_bps, 1000000.0, memory_order_relaxed);
  memset(&req, 0, sizeof req);
  req.has_max_bitrate = 1;
  req.max_bitrate_bps = 1000000; /* == nominal: can never catch up */
  ck_assert_int_eq(burst_decide(c, &req), BURST_BITRATE_INSUFFICIENT);
  channel_table_free(g_table);
}
END_TEST

START_TEST(burst_decide_accepts_valid_request) {
  channel_t *c = make_channel_with_rap(8, 0);
  rtcp_rams_r_t req;
  atomic_store_explicit(&c->nominal_bps, 1000000.0, memory_order_relaxed);
  memset(&req, 0, sizeof req);
  req.has_max_bitrate = 1;
  req.max_bitrate_bps = 5000000; /* well above nominal */
  ck_assert_int_eq(burst_decide(c, &req), BURST_ACCEPT);
  channel_table_free(g_table);
}
END_TEST

static int g_send_calls;
static unsigned char g_last_pkt[2048];

static void capture_send(const unsigned char *pkt, size_t len, void *user) {
  (void)user;
  g_send_calls++;
  if (len <= sizeof g_last_pkt)
    memcpy(g_last_pkt, pkt, len);
}

/* no target_bps getter: compare uncapped vs. client-capped sends instead */
START_TEST(burst_new_caps_target_to_client_max) {
  channel_t *c;
  burst_t *uncapped, *capped;
  struct timespec ts;
  int uncapped_sent, capped_sent;

  c = make_channel_with_rap(8, 30); /* RAP + 30 = 31 cached entries */
  atomic_store_explicit(&c->nominal_bps, 100000000.0, memory_order_relaxed); /* 100 Mbps */

  uncapped = burst_new(c, 1.0, 0 /* no client cap: target stays ~100 Mbps */, 99);
  capped = burst_new(c, 1.0, 1000.0 /* client caps to 1 kbps: glacial */, 99);

  ts.tv_sec = 0;
  ts.tv_nsec = 20000000; /* 20ms */
  nanosleep(&ts, NULL);

  g_send_calls = 0;
  burst_tick(uncapped, 60000, capture_send, NULL);
  uncapped_sent = g_send_calls;

  g_send_calls = 0;
  burst_tick(capped, 60000, capture_send, NULL);
  capped_sent = g_send_calls;

  ck_assert_int_gt(uncapped_sent, capped_sent);
  ck_assert_int_le(capped_sent, 1); /* budget>0 always admits one entry, no more at 1 kbps */

  burst_free(uncapped);
  burst_free(capped);
  channel_table_free(g_table);
}
END_TEST

START_TEST(burst_tick_delivers_cached_packets_and_completes) {
  channel_t *c = make_channel_with_rap(8, 3); /* RAP + 3 more = 4 cached entries */
  burst_t *b;
  burst_tick_result_t r;
  struct timespec ts;
  int i;

  atomic_store_explicit(&c->nominal_bps, 100000000.0, memory_order_relaxed); /* fast: whole cache affordable at once */
  b = burst_new(c, 1.0, 0, 99);

  g_send_calls = 0;
  ts.tv_sec = 0;
  ts.tv_nsec = 20000000; /* 20ms, plenty at 100 Mbps target */
  nanosleep(&ts, NULL);

  for (i = 0; i < 5 && !burst_is_done(b); i++) {
    r = burst_tick(b, 60000, capture_send, NULL);
    if (r == BURST_TICK_DONE)
      break;
    nanosleep(&ts, NULL);
  }

  ck_assert_int_eq(burst_is_done(b), 1);
  ck_assert_int_eq(g_send_calls, 4);

  burst_free(b);
  channel_table_free(g_table);
}
END_TEST

START_TEST(burst_tick_stops_at_duration_cap) {
  channel_t *c = make_channel_with_rap(8, 3);
  burst_t *b;
  burst_tick_result_t r;

  atomic_store_explicit(&c->nominal_bps, 1.0, memory_order_relaxed); /* tiny: nothing affordable */
  b = burst_new(c, 1.0, 0, 99);

  r = burst_tick(b, 0 /* already-expired cap */, capture_send, NULL);
  ck_assert_int_eq(r, BURST_TICK_DONE);
  ck_assert_int_eq(burst_is_done(b), 1);

  burst_free(b);
  channel_table_free(g_table);
}
END_TEST

START_TEST(burst_terminate_marks_done_immediately) {
  channel_t *c = make_channel_with_rap(8, 3);
  burst_t *b = burst_new(c, 1.0, 0, 99);

  ck_assert_int_eq(burst_is_done(b), 0);
  burst_terminate(b);
  ck_assert_int_eq(burst_is_done(b), 1);
  ck_assert_int_eq(burst_tick(b, 60000, capture_send, NULL), BURST_TICK_DONE);

  burst_free(b);
  channel_table_free(g_table);
}
END_TEST

START_TEST(burst_tick_built_packets_round_trip_through_rtx_parse) {
  channel_t *c = make_channel_with_rap(8, 0); /* just RAP entry, seq 1 */
  burst_t *b;
  burst_tick_result_t r;
  struct timespec ts;
  rtx_pkt_t out;

  atomic_store_explicit(&c->nominal_bps, 100000000.0, memory_order_relaxed);
  b = burst_new(c, 1.0, 0, 99);

  ts.tv_sec = 0;
  ts.tv_nsec = 20000000;
  nanosleep(&ts, NULL);

  g_send_calls = 0;
  r = burst_tick(b, 60000, capture_send, NULL);
  ck_assert_int_eq(r, BURST_TICK_DONE);
  ck_assert_int_eq(g_send_calls, 1);

  ck_assert_int_eq(rtx_parse(g_last_pkt, sizeof g_last_pkt, 99, &out), 1);
  ck_assert_uint_eq(out.osn, 1u); /* cached entry's original seq */

  burst_free(b);
  channel_table_free(g_table);
}
END_TEST

static Suite *burst_suite(void) {
  Suite *s = suite_create("burst");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, burst_decide_rejects_null_channel);
  tcase_add_test(tc, burst_decide_rejects_when_no_rap);
  tcase_add_test(tc, burst_decide_flags_malformed_buffer_fill_range);
  tcase_add_test(tc, burst_decide_rejects_insufficient_client_bitrate);
  tcase_add_test(tc, burst_decide_accepts_valid_request);
  tcase_add_test(tc, burst_new_caps_target_to_client_max);
  tcase_add_test(tc, burst_tick_delivers_cached_packets_and_completes);
  tcase_add_test(tc, burst_tick_stops_at_duration_cap);
  tcase_add_test(tc, burst_terminate_marks_done_immediately);
  tcase_add_test(tc, burst_tick_built_packets_round_trip_through_rtx_parse);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(burst_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
