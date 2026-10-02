/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/segstore_int.h"
#include "dipixy/reactor/qsbr.h"
#include "dipixy/ts/pidfilter.h"

#define POOL_CAP 16
#define NWORKERS 2
#define TABLE_SIZE 4
#define MAX_PROBES (8 * (POOL_CAP + 1))

static int g_ctx_marker;
#define CTX ((capture_ctx_t *)&g_ctx_marker)
static const lcevc_select_t full = {LCEVC_SEL_FULL, 0, 0};

static qsbr_domain_t *g_test_qsbr;
static pid_filter_t g_filter;
static uint8_t *g_probe[MAX_PROBES];
static int g_probe_n;

static void use_domain(void) {
  g_test_qsbr = qsbr_domain_create(NWORKERS);
  hls_store_set_qsbr(g_test_qsbr);
}

static void open_store(unsigned pmt) {
  hls_store_open(CTX, &g_filter, pmt, &full, 0.01, 6, SEG_CONTAINER_FMP4);
}

static void close_store(unsigned pmt) {
  hls_store_close(CTX, &g_filter, pmt, &full, SEG_CONTAINER_FMP4);
}

static const hls_store_t *find_store(unsigned pmt) {
  return hls_store_find(CTX, &g_filter, pmt, &full, SEG_CONTAINER_FMP4);
}

static void put_init(unsigned pmt, uint8_t v) {
  uint8_t init[64];

  memset(init, v, sizeof init);
  ck_assert_int_eq(hls_set_init_segment(CTX, &g_filter, pmt, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init), 0);
}

static const uint8_t *init_buf(unsigned pmt) {
  const hls_store_t *st = find_store(pmt);
  const hls_snapshot_t *snap;

  ck_assert_ptr_nonnull(st);
  snap = atomic_load_explicit(&st->snap, memory_order_acquire);
  ck_assert_ptr_nonnull(snap);
  return snap->init_data;
}

static const hls_snapshot_t *snapshot_of(unsigned pmt) {
  const hls_store_t *st = find_store(pmt);
  const hls_snapshot_t *snap;

  ck_assert_ptr_nonnull(st);
  snap = atomic_load_explicit(&st->snap, memory_order_acquire);
  ck_assert_ptr_nonnull(snap);
  return snap;
}

static int uniform(const uint8_t *buf, size_t len) {
  for (size_t i = 1; i < len; i++) if (buf[i] != buf[0]) return 0;
  return 1;
}

static int snapshot_intact(const hls_snapshot_t *snap) {
  if (snap->init_size && !uniform(snap->init_data, snap->init_size)) return 0;
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->ring->segs[(snap->head + i) % HLS_MAX_SEGS];

    if (seg->data && !uniform(seg->data, seg->size)) return 0;
  }
  if (snap->live_data && snap->live_len && !uniform(snap->live_data, snap->live_len)) return 0;
  return 1;
}

static void tick_all(void) {
  for (int i = 0; i < NWORKERS; i++) qsbr_worker_quiescent(g_test_qsbr, i);
}

static void put_segment(unsigned pmt, uint8_t v) {
  uint8_t part[512];

  memset(part, v, sizeof part);
  ck_assert_int_eq(hls_push_part(CTX, &g_filter, pmt, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.005, 1), 0);
  ck_assert_int_eq(hls_push_segment_ll(CTX, &g_filter, pmt, &full, SEG_CONTAINER_FMP4, 0.02), 0);
}

static const uint8_t *segment_buf(unsigned pmt, int idx) {
  const hls_store_t *st = find_store(pmt);
  const hls_snapshot_t *snap;

  ck_assert_ptr_nonnull(st);
  snap = atomic_load_explicit(&st->snap, memory_order_acquire);
  ck_assert_ptr_nonnull(snap);
  ck_assert_int_gt(snap->count, idx);
  return snap->ring->segs[(snap->head + idx) % HLS_MAX_SEGS].data;
}

static int released(const uint8_t *buf) {
  int hit = 0;

  for (int i = 0; i <= POOL_CAP; i++) {
    uint8_t *got = seg_buf_alloc(64);

    g_probe[g_probe_n++] = got;
    if (got == buf) hit = 1;
  }
  return hit;
}

static void drop_probes(void) {
  while (g_probe_n > 0) seg_buf_unref(g_probe[--g_probe_n]);
}

START_TEST(close_keeps_snapshot_until_every_worker_ticks) {
  const uint8_t *held;
  const hls_snapshot_t *snap;

  use_domain();
  open_store(1);
  put_init(1, 0x11);
  held = init_buf(1);
  snap = snapshot_of(1);
  close_store(1);
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(held));
  ck_assert(snapshot_intact(snap));
  for (int i = 0; i < 3; i++) qsbr_worker_quiescent(g_test_qsbr, 1);
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(held));
  ck_assert(snapshot_intact(snap));
  ck_assert_uint_eq(held[0], 0x11);
  qsbr_worker_quiescent(g_test_qsbr, 0);
  hls_store_slot_reclaim_sweep();
  ck_assert(released(held));
  drop_probes();
}
END_TEST

START_TEST(full_table_reopens_only_after_every_worker_ticks) {
  use_domain();
  for (unsigned pmt = 1; pmt <= TABLE_SIZE; pmt++) open_store(pmt);
  for (unsigned pmt = 1; pmt <= TABLE_SIZE; pmt++) ck_assert_ptr_nonnull(find_store(pmt));
  for (unsigned pmt = 1; pmt <= TABLE_SIZE; pmt++) close_store(pmt);
  open_store(TABLE_SIZE + 1);
  ck_assert_ptr_null(find_store(TABLE_SIZE + 1));
  qsbr_worker_quiescent(g_test_qsbr, 0);
  hls_store_slot_reclaim_sweep();
  open_store(TABLE_SIZE + 1);
  ck_assert_ptr_null(find_store(TABLE_SIZE + 1));
  qsbr_worker_quiescent(g_test_qsbr, 1);
  hls_store_slot_reclaim_sweep();
  open_store(TABLE_SIZE + 1);
  ck_assert_ptr_nonnull(find_store(TABLE_SIZE + 1));
}
END_TEST

START_TEST(replaced_snapshots_survive_until_every_worker_ticks) {
  const uint8_t *first;
  const uint8_t *overflow;
  const hls_snapshot_t *first_snap;
  const hls_snapshot_t *overflow_snap;

  use_domain();
  open_store(1);
  put_init(1, 0x21);
  first = init_buf(1);
  first_snap = snapshot_of(1);
  for (uint8_t v = 0x22; v <= 0x25; v++) put_init(1, v);
  overflow = init_buf(1);
  overflow_snap = snapshot_of(1);
  put_init(1, 0x26);
  put_init(1, 0x27);
  ck_assert(!released(first));
  ck_assert(!released(overflow));
  ck_assert(snapshot_intact(first_snap));
  ck_assert(snapshot_intact(overflow_snap));
  tick_all();
  hls_store_slot_reclaim_sweep();
  ck_assert(released(overflow));
  put_init(1, 0x28);
  ck_assert(released(first));
  drop_probes();
}
END_TEST

START_TEST(reopened_key_does_not_release_old_snapshot) {
  const uint8_t *old_buf;

  use_domain();
  open_store(1);
  put_init(1, 0x31);
  old_buf = init_buf(1);
  close_store(1);
  open_store(1);
  put_init(1, 0x32);
  ck_assert_ptr_ne(init_buf(1), old_buf);
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(old_buf));
  ck_assert_uint_eq(old_buf[0], 0x31);
  tick_all();
  hls_store_slot_reclaim_sweep();
  ck_assert(released(old_buf));
  drop_probes();
}
END_TEST

START_TEST(lagging_worker_keeps_replaced_snapshots_alive) {
  const uint8_t *first;
  const uint8_t *overflow;

  use_domain();
  open_store(1);
  put_init(1, 0x51);
  first = init_buf(1);
  for (uint8_t v = 0x52; v <= 0x55; v++) put_init(1, v);
  overflow = init_buf(1);
  put_init(1, 0x56);
  put_init(1, 0x57);
  for (int i = 0; i < 3; i++) qsbr_worker_quiescent(g_test_qsbr, 1);
  hls_store_slot_reclaim_sweep();
  put_init(1, 0x58);
  ck_assert(!released(first));
  ck_assert(!released(overflow));
  qsbr_worker_quiescent(g_test_qsbr, 0);
  hls_store_slot_reclaim_sweep();
  ck_assert(released(overflow));
  put_init(1, 0x59);
  ck_assert(released(first));
  drop_probes();
}
END_TEST

START_TEST(closed_segment_buffer_waits_for_every_worker) {
  const uint8_t *seg;
  const hls_snapshot_t *snap;

  use_domain();
  open_store(1);
  put_init(1, 0x61);
  put_segment(1, 0x62);
  seg = segment_buf(1, 0);
  snap = snapshot_of(1);
  close_store(1);
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(seg));
  ck_assert(snapshot_intact(snap));
  ck_assert_uint_eq(seg[0], 0x62);
  qsbr_worker_quiescent(g_test_qsbr, 0);
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(seg));
  ck_assert(snapshot_intact(snap));
  qsbr_worker_quiescent(g_test_qsbr, 1);
  hls_store_slot_reclaim_sweep();
  ck_assert(released(seg));
  drop_probes();
}
END_TEST

START_TEST(evicted_segment_buffer_waits_for_every_worker) {
  const uint8_t *oldest;
  const hls_snapshot_t *snap;

  use_domain();
  open_store(1);
  put_init(1, 0x71);
  put_segment(1, 0x72);
  oldest = segment_buf(1, 0);
  snap = snapshot_of(1);
  for (uint8_t v = 0x73; v <= 0x78; v++) put_segment(1, v);
  ck_assert_ptr_ne(segment_buf(1, 0), oldest);
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(oldest));
  ck_assert(snapshot_intact(snap));
  ck_assert_uint_eq(oldest[0], 0x72);
  tick_all();
  hls_store_slot_reclaim_sweep();
  put_segment(1, 0x79);
  ck_assert(released(oldest));
  drop_probes();
}
END_TEST

START_TEST(reopening_an_open_store_defers_release_of_its_snapshot) {
  const uint8_t *held;
  const hls_snapshot_t *snap;
  const hls_store_t *st;

  use_domain();
  open_store(1);
  put_init(1, 0x81);
  held = init_buf(1);
  snap = snapshot_of(1);
  open_store(1);
  st = find_store(1);
  ck_assert_ptr_nonnull(st);
  ck_assert_ptr_null(atomic_load_explicit(&st->snap, memory_order_acquire));
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(held));
  ck_assert(snapshot_intact(snap));
  ck_assert_uint_eq(held[0], 0x81);
  tick_all();
  hls_store_slot_reclaim_sweep();
  ck_assert(released(held));
  drop_probes();
}
END_TEST

START_TEST(llhls_enable_defers_release_of_the_replaced_snapshot) {
  const uint8_t *held;

  use_domain();
  open_store(1);
  put_init(1, 0x91);
  held = init_buf(1);
  hls_llhls_enable(CTX, &g_filter, 1, &full, SEG_CONTAINER_FMP4, 0.005);
  put_init(1, 0x92);
  hls_store_slot_reclaim_sweep();
  ck_assert(!released(held));
  ck_assert_uint_eq(held[0], 0x91);
  tick_all();
  hls_store_slot_reclaim_sweep();
  put_init(1, 0x93);
  ck_assert(released(held));
  drop_probes();
}
END_TEST

static _Atomic int g_sweeper_stop;

static void *sweeper_main(void *arg) {
  (void)arg;
  while (!atomic_load_explicit(&g_sweeper_stop, memory_order_relaxed)) hls_store_slot_reclaim_sweep();
  return NULL;
}

START_TEST(concurrent_sweeper_does_not_release_closed_snapshot_early) {
  const uint8_t *held;
  const hls_snapshot_t *snap;
  pthread_t sweeper;

  use_domain();
  open_store(1);
  put_init(1, 0xa1);
  held = init_buf(1);
  snap = snapshot_of(1);
  atomic_store_explicit(&g_sweeper_stop, 0, memory_order_relaxed);
  pthread_create(&sweeper, NULL, sweeper_main, NULL);
  close_store(1);
  for (int i = 0; i < 3; i++) qsbr_worker_quiescent(g_test_qsbr, 1);
  for (int i = 0; i < 200; i++) ck_assert(snapshot_intact(snap));
  ck_assert(!released(held));
  atomic_store_explicit(&g_sweeper_stop, 1, memory_order_relaxed);
  pthread_join(sweeper, NULL);
  ck_assert(!released(held));
  qsbr_worker_quiescent(g_test_qsbr, 0);
  hls_store_slot_reclaim_sweep();
  ck_assert(released(held));
  drop_probes();
}
END_TEST

static Suite *segstore_reclaim_suite(void) {
  Suite *s = suite_create("dipixy_segstore_reclaim");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, close_keeps_snapshot_until_every_worker_ticks);
  tcase_add_test(tc, full_table_reopens_only_after_every_worker_ticks);
  tcase_add_test(tc, replaced_snapshots_survive_until_every_worker_ticks);
  tcase_add_test(tc, reopened_key_does_not_release_old_snapshot);
  tcase_add_test(tc, lagging_worker_keeps_replaced_snapshots_alive);
  tcase_add_test(tc, closed_segment_buffer_waits_for_every_worker);
  tcase_add_test(tc, evicted_segment_buffer_waits_for_every_worker);
  tcase_add_test(tc, reopening_an_open_store_defers_release_of_its_snapshot);
  tcase_add_test(tc, llhls_enable_defers_release_of_the_replaced_snapshot);
  tcase_add_test(tc, concurrent_sweeper_does_not_release_closed_snapshot_early);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  hls_set_seg_pool_cap(POOL_CAP);
  hls_store_init(TABLE_SIZE);
  SRunner *sr = srunner_create(segstore_reclaim_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
