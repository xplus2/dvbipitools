/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "dipixy/segment/pidlock.h"
#include "dipixy/ts/ts_push_int.h"

void capture_close(capture_ctx_t *ctx) { (void)ctx; }
void capture_wait_pumps_quiescent(void) {}
int capture_defer_after_quiescent(qsbr_deferred_fn fn, void *arg) { (void)fn; (void)arg; return 0; }

_Atomic int *capture_ts_push_head_ptr(capture_ctx_t *ctx) {
  static _Atomic int head = -1;
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

#define FAKE_FDS 8
#define SENT_CAP 8192
#define NO_FD -1000

static int g_conn_request_close_calls;
static int g_gone_fd = NO_FD;
static int g_send_fail_fd = NO_FD;
static uint8_t g_sent[FAKE_FDS][SENT_CAP];
static size_t g_sent_len[FAKE_FDS];
static int g_sent_calls[FAKE_FDS];
static int g_h2_wakes;
static int g_h3_wakes;
static int g_last_wake;

conn_t *conn_for_fd(int fd) {
  if (fd == g_gone_fd) return NULL;
  return (conn_t *)(intptr_t)(fd + 1);
}

int conn_send_buffered(conn_t *c, const void *a, size_t alen, const void *b, size_t blen) {
  int fd = (int)(intptr_t)c - 1;

  (void)b;
  ck_assert_uint_eq(blen, 0);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_lt(fd, FAKE_FDS);
  if (fd == g_send_fail_fd) return -1;
  ck_assert_uint_le(g_sent_len[fd] + alen, SENT_CAP);
  memcpy(g_sent[fd] + g_sent_len[fd], a, alen);
  g_sent_len[fd] += alen;
  g_sent_calls[fd]++;
  return 0;
}

void h2_tspush_wake(int sub_idx) {
  g_h2_wakes++;
  g_last_wake = sub_idx;
}

void h3_tspush_wake(int sub_idx) {
  g_h3_wakes++;
  g_last_wake = sub_idx;
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

  ts_push_queue_stats(&st, 0.0);
  ck_assert_uint_eq(st.bytes, 0u);
  ck_assert_uint_eq(st.max_bytes, 0u);

  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_ring_enqueue(b, chunk, sizeof chunk);
  ts_push_queue_stats(&st, 0.0);
  ck_assert_uint_eq(st.bytes, 3000u);
  ck_assert_uint_eq(st.max_bytes, 2000u);

  ck_assert_uint_eq(byte_ring_read(&a->pkt_ring, sink, 1500), 1500u);
  ts_push_queue_stats(&st, 0.0);
  ck_assert_uint_eq(st.bytes, 1500u);
  ck_assert_uint_eq(st.max_bytes, 1000u);

  atomic_store(&b->alive, TS_SUB_FREE);
  ts_push_queue_stats(&st, 0.0);
  ck_assert_uint_eq(st.bytes, 500u);
}
END_TEST

START_TEST(queue_ms_follows_enqueue_rate_between_calls) {
  ts_push_queue_stats_t st;
  ts_sub_t *a;
  uint8_t chunk[1000];
  uint8_t sink[4096];

  memset(chunk, 0x47, sizeof chunk);
  ts_push_init(0, 4);
  a = alive_sub(0, 4096);

  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_queue_stats(&st, 1.0);
  ck_assert_int_eq(st.ms_known, 0);
  ck_assert_uint_eq(st.max_ms, 0u);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_queue_stats(&st, 2.0);
  ck_assert_uint_eq(st.max_bytes, 3000u);
  ck_assert_int_eq(st.ms_known, 1);
  ck_assert_uint_eq(st.max_ms, 1500u);
  ck_assert_uint_eq(byte_ring_read(&a->pkt_ring, sink, sizeof sink), 3000u);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ck_assert_uint_eq(byte_ring_read(&a->pkt_ring, sink, sizeof sink), 1000u);
  ts_push_queue_stats(&st, 2.5);
  ck_assert_uint_eq(st.max_bytes, 0u);
  ck_assert_int_eq(st.ms_known, 1);
  ck_assert_uint_eq(st.max_ms, 0u);

  atomic_store(&a->alive, TS_SUB_FREE);
  ts_push_queue_stats(&st, 3.0);
  ck_assert_int_eq(st.ms_known, 0);
  a = alive_sub(0, 4096);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_queue_stats(&st, 4.0);
  ck_assert_uint_eq(st.max_ms, 0u);
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

static void reset_fakes(void) {
  g_conn_request_close_calls = 0;
  g_gone_fd = NO_FD;
  g_send_fail_fd = NO_FD;
  memset(g_sent_len, 0, sizeof g_sent_len);
  memset(g_sent_calls, 0, sizeof g_sent_calls);
  g_h2_wakes = 0;
  g_h3_wakes = 0;
  g_last_wake = -1;
}

static ts_sub_t *attached_sub(int idx, conn_proto_t proto, int fd, int tid) {
  ts_sub_t *s = alive_sub(idx, 4096);

  s->proto = proto;
  s->fd = fd;
  byte_ring_reset(&s->h2_ring, 4096);
  byte_ring_reset(&s->h3_ring, 4096);
  atomic_store(&s->ready, 1);
  ts_push_set_reactor_tid(idx, tid);
  return s;
}

static void fill(uint8_t *buf, size_t len, unsigned seed) {
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(seed + i * 7);
}

START_TEST(flush_drains_h1_ring_into_the_connection) {
  ts_sub_t *a;
  uint8_t chunk[1000];

  fill(chunk, sizeof chunk, 1);
  ts_push_init(0, 4);
  a = attached_sub(0, CONN_PROTO_H1, 3, 0);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_flush_ready(0);
  ck_assert_uint_eq(g_sent_len[3], sizeof chunk);
  ck_assert_mem_eq(g_sent[3], chunk, sizeof chunk);
  ck_assert_int_eq(g_sent_calls[3], 1);
  ck_assert_int_eq(atomic_load(&a->alive), TS_SUB_ALIVE);
  ts_push_flush_ready(0);
  ck_assert_int_eq(g_sent_calls[3], 1);
}
END_TEST

START_TEST(flush_sends_a_wrapped_ring_in_order) {
  ts_sub_t *a;
  uint8_t chunk[3000];
  uint8_t wrap[2000];
  uint8_t sink[4096];

  fill(chunk, sizeof chunk, 5);
  fill(wrap, sizeof wrap, 99);
  ts_push_init(0, 4);
  a = attached_sub(0, CONN_PROTO_H1, 2, 0);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ck_assert_uint_eq(byte_ring_read(&a->pkt_ring, sink, sizeof chunk), sizeof chunk);
  ts_push_ring_enqueue(a, wrap, sizeof wrap);
  ts_push_flush_ready(0);
  ck_assert_int_eq(g_sent_calls[2], 2);
  ck_assert_uint_eq(g_sent_len[2], sizeof wrap);
  ck_assert_mem_eq(g_sent[2], wrap, sizeof wrap);
}
END_TEST

START_TEST(flush_with_an_empty_ring_sends_nothing) {
  ts_push_init(0, 4);
  attached_sub(0, CONN_PROTO_H1, 1, 0);
  ts_push_flush_ready(0);
  ck_assert_int_eq(g_sent_calls[1], 0);
  ck_assert_int_eq(g_conn_request_close_calls, 0);
}
END_TEST

START_TEST(flush_drops_a_sub_whose_connection_is_gone) {
  ts_sub_t *a;
  uint8_t chunk[188];

  fill(chunk, sizeof chunk, 3);
  ts_push_init(0, 4);
  a = attached_sub(0, CONN_PROTO_H1, 4, 0);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  g_gone_fd = 4;
  atomic_store(&g_ts_active_count, 1);
  ts_push_flush_ready(0);
  ck_assert_int_eq(atomic_load(&a->alive), TS_SUB_FREE);
  ck_assert_int_eq(g_conn_request_close_calls, 0);
  ck_assert_int_eq(ts_push_active_count(), 0);
}
END_TEST

START_TEST(flush_drops_a_sub_when_the_send_fails) {
  ts_sub_t *a;
  uint8_t chunk[188];

  fill(chunk, sizeof chunk, 4);
  ts_push_init(0, 4);
  a = attached_sub(0, CONN_PROTO_H1, 5, 0);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  g_send_fail_fd = 5;
  atomic_store(&g_ts_active_count, 1);
  ts_push_flush_ready(0);
  ck_assert_int_eq(atomic_load(&a->alive), TS_SUB_FREE);
  ck_assert_int_eq(g_conn_request_close_calls, 1);
  ck_assert_int_eq(g_sent_calls[5], 0);
  ck_assert_int_eq(ts_push_active_count(), 0);
}
END_TEST

START_TEST(flush_drops_an_overrun_sub_without_sending) {
  ts_sub_t *a;
  uint8_t chunk[188];

  fill(chunk, sizeof chunk, 6);
  ts_push_init(0, 4);
  a = attached_sub(0, CONN_PROTO_H1, 6, 0);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  atomic_store(&a->pkt_overrun, 1);
  ts_push_flush_ready(0);
  ck_assert_int_eq(atomic_load(&a->alive), TS_SUB_FREE);
  ck_assert_int_eq(g_conn_request_close_calls, 1);
  ck_assert_int_eq(g_sent_calls[6], 0);
}
END_TEST

START_TEST(flush_skips_subs_that_are_dead_unready_or_foreign) {
  ts_sub_t *dead;
  ts_sub_t *unready;
  ts_sub_t *foreign;
  ts_sub_t *ok;
  uint8_t chunk[188];

  fill(chunk, sizeof chunk, 8);
  ts_push_init(0, 4);
  dead = attached_sub(0, CONN_PROTO_H1, 0, 0);
  unready = attached_sub(1, CONN_PROTO_H1, 1, 0);
  foreign = attached_sub(2, CONN_PROTO_H1, 2, 0);
  ok = attached_sub(3, CONN_PROTO_H1, 3, 0);
  foreign->reactor_tid = 1;
  atomic_store(&dead->alive, TS_SUB_FREE);
  atomic_store(&unready->ready, 0);
  for (int i = 0; i < 4; i++) ts_push_ring_enqueue(&g_ts_subs[i], chunk, sizeof chunk);
  ts_push_flush_ready(0);
  ck_assert_int_eq(g_sent_calls[0], 0);
  ck_assert_int_eq(g_sent_calls[1], 0);
  ck_assert_int_eq(g_sent_calls[2], 0);
  ck_assert_int_eq(g_sent_calls[3], 1);
  ck_assert_int_eq(atomic_load(&ok->alive), TS_SUB_ALIVE);
  ck_assert_int_eq(g_conn_request_close_calls, 0);
}
END_TEST

START_TEST(flush_serves_only_the_requested_reactor) {
  ts_sub_t *a;
  ts_sub_t *b;
  uint8_t chunk[188];

  fill(chunk, sizeof chunk, 9);
  ts_push_init(0, 4);
  a = attached_sub(0, CONN_PROTO_H1, 0, 0);
  b = attached_sub(1, CONN_PROTO_H1, 1, 1);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_ring_enqueue(b, chunk, sizeof chunk);
  ts_push_flush_ready(1);
  ck_assert_int_eq(g_sent_calls[0], 0);
  ck_assert_int_eq(g_sent_calls[1], 1);
  ts_push_flush_ready(0);
  ck_assert_int_eq(g_sent_calls[0], 1);
  ck_assert_int_eq(g_sent_calls[1], 1);
}
END_TEST

START_TEST(flush_continues_past_a_sub_dropped_mid_walk) {
  ts_sub_t *s[3];
  uint8_t chunk[188];

  fill(chunk, sizeof chunk, 11);
  ts_push_init(0, 4);
  for (int i = 0; i < 3; i++) {
    s[i] = attached_sub(i, CONN_PROTO_H1, i, 0);
    ts_push_ring_enqueue(s[i], chunk, sizeof chunk);
  }
  g_send_fail_fd = 1;
  ts_push_flush_ready(0);
  ck_assert_int_eq(g_sent_calls[0], 1);
  ck_assert_int_eq(g_sent_calls[2], 1);
  ck_assert_int_eq(atomic_load(&s[1]->alive), TS_SUB_FREE);
  ck_assert_int_eq(atomic_load(&s[0]->alive), TS_SUB_ALIVE);
  ck_assert_int_eq(atomic_load(&s[2]->alive), TS_SUB_ALIVE);
}
END_TEST

START_TEST(flush_wakes_h2_and_h3_subs_with_pending_bytes_only) {
  conn_proto_t proto = _i ? CONN_PROTO_H3 : CONN_PROTO_H2;
  ts_sub_t *busy;
  ts_sub_t *idle;
  uint8_t chunk[188];
  int *wakes = _i ? &g_h3_wakes : &g_h2_wakes;
  int *other = _i ? &g_h2_wakes : &g_h3_wakes;

  fill(chunk, sizeof chunk, 13);
  ts_push_init(0, 4);
  busy = attached_sub(0, proto, 0, 0);
  idle = attached_sub(1, proto, 1, 0);
  ck_assert_int_eq(byte_ring_write(_i ? &busy->h3_ring : &busy->h2_ring, chunk, sizeof chunk), 1);
  ts_push_flush_ready(0);
  ck_assert_int_eq(*wakes, 1);
  ck_assert_int_eq(*other, 0);
  ck_assert_int_eq(g_last_wake, 0);
  atomic_store(&busy->ready, 0);
  ts_push_flush_ready(0);
  ck_assert_int_eq(*wakes, 1);
  ck_assert_int_eq(atomic_load(&idle->alive), TS_SUB_ALIVE);
  ck_assert_int_eq(g_sent_calls[0] + g_sent_calls[1], 0);
}
END_TEST

START_TEST(flush_ignores_subs_of_unknown_protocol) {
  ts_sub_t *a;
  uint8_t chunk[188];

  fill(chunk, sizeof chunk, 15);
  ts_push_init(0, 4);
  a = attached_sub(0, (conn_proto_t)0, 0, 0);
  ts_push_ring_enqueue(a, chunk, sizeof chunk);
  ts_push_flush_ready(0);
  ck_assert_int_eq(g_sent_calls[0], 0);
  ck_assert_int_eq(g_h2_wakes + g_h3_wakes, 0);
  ck_assert_int_eq(atomic_load(&a->alive), TS_SUB_ALIVE);
}
END_TEST

START_TEST(flush_with_no_subs_on_the_reactor_is_a_noop) {
  ts_push_init(0, 4);
  ts_push_flush_ready(0);
  ts_push_flush_ready(TS_PUSH_MAX_REACTOR_THREADS - 1);
  ck_assert_int_eq(g_conn_request_close_calls, 0);
}
END_TEST

START_TEST(h2_h3_enqueue_fill_their_own_ring_and_wake_the_reactor) {
  conn_proto_t proto = _i ? CONN_PROTO_H3 : CONN_PROTO_H2;
  ts_sub_t *a;
  byte_ring_t *ring;
  uint8_t chunk[1000];
  uint8_t sink[4096];
  uint64_t ticks = 0;
  int efd = eventfd(0, EFD_NONBLOCK);

  ck_assert_int_ge(efd, 0);
  fill(chunk, sizeof chunk, 21);
  ts_push_init(0, 4);
  a = attached_sub(0, proto, 0, 2);
  ring = _i ? &a->h3_ring : &a->h2_ring;
  ts_push_register_reactor_efd(2, efd);
  if (_i) ts_push_h3_enqueue(0, chunk, sizeof chunk);
  else ts_push_h2_enqueue(0, chunk, sizeof chunk);
  ck_assert_int_eq((int)read(efd, &ticks, sizeof ticks), (int)sizeof ticks);
  ck_assert_uint_eq(byte_ring_read(ring, sink, sizeof sink), sizeof chunk);
  ck_assert_mem_eq(sink, chunk, sizeof chunk);
  ts_push_register_reactor_efd(2, -1);
  close(efd);
}
END_TEST

START_TEST(h2_h3_enqueue_drops_on_a_full_ring_and_ignores_bad_input) {
  conn_proto_t proto = _i ? CONN_PROTO_H3 : CONN_PROTO_H2;
  ts_sub_t *a;
  byte_ring_t *ring;
  uint8_t chunk[1000];
  uint8_t sink[8192];
  size_t got;

  fill(chunk, sizeof chunk, 23);
  ts_push_init(0, 4);
  a = attached_sub(0, proto, 0, 0);
  ring = _i ? &a->h3_ring : &a->h2_ring;
  for (int i = 0; i < 6; i++) {
    if (_i) ts_push_h3_enqueue(0, chunk, sizeof chunk);
    else ts_push_h2_enqueue(0, chunk, sizeof chunk);
  }
  got = byte_ring_read(ring, sink, sizeof sink);
  ck_assert_uint_eq(got, 4000u);
  for (size_t i = 0; i < got; i++) ck_assert_uint_eq(sink[i], chunk[i % sizeof chunk]);
  if (_i) {
    ts_push_h3_enqueue(-1, chunk, sizeof chunk);
    ts_push_h3_enqueue(g_ts_subs_n, chunk, sizeof chunk);
    ts_push_h3_enqueue(0, chunk, 0);
  } else {
    ts_push_h2_enqueue(-1, chunk, sizeof chunk);
    ts_push_h2_enqueue(g_ts_subs_n, chunk, sizeof chunk);
    ts_push_h2_enqueue(0, chunk, 0);
  }
  ck_assert_uint_eq(byte_ring_read(ring, sink, sizeof sink), 0u);
}
END_TEST

START_TEST(active_count_tracks_the_counter) {
  ts_push_init(0, 4);
  atomic_store(&g_ts_active_count, 0);
  ck_assert_int_eq(ts_push_active_count(), 0);
  atomic_store(&g_ts_active_count, 7);
  ck_assert_int_eq(ts_push_active_count(), 7);
}
END_TEST

static Suite *ts_push_queue_suite(void) {
  Suite *s = suite_create("dipixy_ts_push_queue");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, reset_fakes, NULL);
  tcase_add_test(tc, queue_stats_report_bytes_and_fullest_ring);
  tcase_add_test(tc, queue_ms_follows_enqueue_rate_between_calls);
  tcase_add_test(tc, high_watermark_and_drops_counted_from_level_two);
  tcase_add_test(tc, high_watermark_and_drops_ignored_below_level_two);
  tcase_add_test(tc, drop_sub_h1_closes_conn_and_frees_slot);
  tcase_add_test(tc, drop_sub_h2_skips_conn_close_but_frees_slot);
  tcase_add_test(tc, flush_drains_h1_ring_into_the_connection);
  tcase_add_test(tc, flush_sends_a_wrapped_ring_in_order);
  tcase_add_test(tc, flush_with_an_empty_ring_sends_nothing);
  tcase_add_test(tc, flush_drops_a_sub_whose_connection_is_gone);
  tcase_add_test(tc, flush_drops_a_sub_when_the_send_fails);
  tcase_add_test(tc, flush_drops_an_overrun_sub_without_sending);
  tcase_add_test(tc, flush_skips_subs_that_are_dead_unready_or_foreign);
  tcase_add_test(tc, flush_serves_only_the_requested_reactor);
  tcase_add_test(tc, flush_continues_past_a_sub_dropped_mid_walk);
  tcase_add_loop_test(tc, flush_wakes_h2_and_h3_subs_with_pending_bytes_only, 0, 2);
  tcase_add_test(tc, flush_ignores_subs_of_unknown_protocol);
  tcase_add_test(tc, flush_with_no_subs_on_the_reactor_is_a_noop);
  tcase_add_loop_test(tc, h2_h3_enqueue_fill_their_own_ring_and_wake_the_reactor, 0, 2);
  tcase_add_loop_test(tc, h2_h3_enqueue_drops_on_a_full_ring_and_ignores_bad_input, 0, 2);
  tcase_add_test(tc, active_count_tracks_the_counter);
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
