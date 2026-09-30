/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/segment/pidlock.h"
#include "dipixy/ts/ts_push_int.h"

void capture_close(capture_ctx_t *ctx) { (void)ctx; }
void capture_wait_pumps_quiescent(void) {}
int capture_defer_after_quiescent(qsbr_deferred_fn fn, void *arg) { (void)fn; (void)arg; return 0; }

_Atomic int *capture_ts_push_head_ptr(capture_ctx_t *ctx) {
  static _Atomic int head;
  (void)ctx;
  return &head;
}

int ws_clients_add_persistent(const client_info_t *info) {
  (void)info;
  return -1;
}

void ws_clients_remove(int handle) { (void)handle; }
void ws_clients_add_bytes(int handle, size_t n) {
  (void)handle;
  (void)n;
}

void ts_push_h2_enqueue(int sub_idx, const uint8_t *pkt, size_t len) {
  (void)sub_idx;
  (void)pkt;
  (void)len;
}

void ts_push_h3_enqueue(int sub_idx, const uint8_t *pkt, size_t len) {
  (void)sub_idx;
  (void)pkt;
  (void)len;
}

void pidlock_snapshot(const psi_t *psi, unsigned *allowed, int *n_allowed, int cap) {
  (void)psi;
  (void)allowed;
  (void)cap;
  *n_allowed = 0;
}

int pidlock_allowed(const unsigned *allowed, int n_allowed, unsigned pid) {
  (void)allowed;
  (void)n_allowed;
  (void)pid;
  return 0;
}

void pidlock_apply_lcevc(const lcevc_select_t *lcevc, pid_filter_t *filter, const unsigned *pids, int count) {
  (void)lcevc;
  (void)filter;
  (void)pids;
  (void)count;
}

const unsigned char *pidlock_rewrite_pmt(const psi_t *tp, const pid_filter_t *filter, unsigned char *cc_pmt,
                                         const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out188) {
  (void)tp;
  (void)filter;
  (void)cc_pmt;
  (void)pid;
  (void)rw;
  (void)out188;
  return pkt;
}

static int g_conn_request_close_calls;

conn_t *conn_for_fd(int fd) {
  (void)fd;
  return (conn_t *)1;
}

void conn_request_close(conn_t *c) {
  (void)c;
  g_conn_request_close_calls++;
}

static ts_sub_t *alive_sub(int idx, uint32_t ring_bytes) {
  ts_sub_t *s = &g_ts_subs[idx];

  s->proto = CONN_PROTO_H1;
  s->reactor_tid = -1;
  s->ws_handle = -1;
  byte_ring_reset(&s->pkt_ring, ring_bytes);
  atomic_store(&s->alive, TS_SUB_ALIVE);
  return s;
}

START_TEST(queue_stats_report_bytes_and_fullest_ring) {
  ts_push_queue_stats_t st;
  ts_sub_t *a, *b;
  uint8_t chunk[1000];
  uint8_t sink[4096];

  memset(chunk, 0x47, sizeof chunk);
  ts_push_init(0, 4);
  a = alive_sub(0, 4096);
  b = alive_sub(1, 4096);

  ts_push_queue_stats(&st);
  ck_assert_uint_eq(st.bytes, 0u);
  ck_assert_uint_eq(st.max_bytes, 0u);

  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_ring_enqueue(b, chunk, sizeof chunk);
  ts_push_queue_stats(&st);
  ck_assert_uint_eq(st.bytes, 3000u);
  ck_assert_uint_eq(st.max_bytes, 2000u);

  ck_assert_uint_eq(byte_ring_read(&a->pkt_ring, sink, 1500), 1500u);
  ts_push_queue_stats(&st);
  ck_assert_uint_eq(st.bytes, 1500u);
  ck_assert_uint_eq(st.max_bytes, 1000u);

  atomic_store(&b->alive, TS_SUB_FREE);
  ts_push_queue_stats(&st);
  ck_assert_uint_eq(st.bytes, 500u);
}
END_TEST

START_TEST(high_watermark_and_drops_counted_from_level_two) {
  ts_sub_t *a;
  uint8_t chunk[1000];

  memset(chunk, 0x47, sizeof chunk);
  ts_push_init(0, 4);
  a = alive_sub(0, 4096);
  ts_push_set_queue_metrics(2);

  for (int i = 0; i < 4; i++) ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ck_assert_uint_eq(ts_push_queue_high_watermark(), 4000u);
  ck_assert_uint_eq(ts_push_queue_dropped(), 0u);

  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ck_assert_uint_eq(ts_push_queue_dropped(), 1u);
  ck_assert_uint_eq(ts_push_queue_high_watermark(), 4000u);
}
END_TEST

START_TEST(high_watermark_and_drops_ignored_below_level_two) {
  ts_sub_t *a;
  uint8_t chunk[1000];

  memset(chunk, 0x47, sizeof chunk);
  ts_push_init(0, 4);
  a = alive_sub(0, 4096);
  ts_push_set_queue_metrics(1);

  for (int i = 0; i < 5; i++) ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ck_assert_uint_eq(ts_push_queue_high_watermark(), 0u);
  ck_assert_uint_eq(ts_push_queue_dropped(), 0u);
}
END_TEST

START_TEST(drop_sub_h1_closes_conn_and_frees_slot) {
  ts_sub_t *a;

  ts_push_init(0, 4);
  a = alive_sub(0, 4096);
  a->proto = CONN_PROTO_H1;
  g_conn_request_close_calls = 0;

  ts_push_drop_sub(a, 0);

  ck_assert_int_eq(g_conn_request_close_calls, 1);
  ck_assert_int_eq(atomic_load(&a->alive), TS_SUB_FREE);
}
END_TEST

START_TEST(drop_sub_h2_skips_conn_close_but_frees_slot) {
  ts_sub_t *a;

  ts_push_init(0, 4);
  a = alive_sub(0, 4096);
  a->proto = CONN_PROTO_H2;
  g_conn_request_close_calls = 0;

  ts_push_drop_sub(a, 0);

  ck_assert_int_eq(g_conn_request_close_calls, 0);
  ck_assert_int_eq(atomic_load(&a->alive), TS_SUB_FREE);
}
END_TEST

static Suite *ts_push_queue_suite(void) {
  Suite *s = suite_create("dipixy_ts_push_queue");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, queue_stats_report_bytes_and_fullest_ring);
  tcase_add_test(tc, high_watermark_and_drops_counted_from_level_two);
  tcase_add_test(tc, high_watermark_and_drops_ignored_below_level_two);
  tcase_add_test(tc, drop_sub_h1_closes_conn_and_frees_slot);
  tcase_add_test(tc, drop_sub_h2_skips_conn_close_but_frees_slot);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ts_push_queue_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
