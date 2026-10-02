/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/segstore_int.h"
#include "dipixy/reactor/qsbr.h"
#include "dipixy/ts/pidfilter.h"

#define NREADERS 6
#define SEG_LEN 4096
#define PART_LEN 512
#define RUN_MS 500

static int g_ctx_marker;
#define CTX ((capture_ctx_t *)&g_ctx_marker)
static const lcevc_select_t full = {LCEVC_SEL_FULL, 0, 0};

static _Atomic int g_stop;
static _Atomic int g_bad;
static _Atomic uint32_t g_fill_counter;
static qsbr_domain_t *g_test_qsbr;

static void fill(uint8_t *buf, size_t len, uint8_t v) { memset(buf, v, len); }

/* 1: every byte equals buf[0], catches a torn or freed read */
static int uniform(const uint8_t *buf, size_t len) {
  for (size_t i = 1; i < len; i++) if (buf[i] != buf[0]) return 0;
  return 1;
}

static void *writer_thread(void *arg) {
  pid_filter_t f;
  uint8_t seg[SEG_LEN];
  uint8_t part[PART_LEN];
  uint8_t init[64];
  (void)arg;
  memset(&f, 0, sizeof f);
  hls_store_open(CTX, &f, 0, &full, 0.01, 6, SEG_CONTAINER_FMP4);
  while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
    uint32_t n = atomic_fetch_add_explicit(&g_fill_counter, 1, memory_order_relaxed);
    uint8_t v = (uint8_t)(n / 4); /* constant across one live-segment cycle (4 parts) */
    fill(init, sizeof init, v);
    hls_set_init_segment(CTX, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
    fill(part, sizeof part, v);
    hls_push_part(CTX, &f, 0, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.005, 1);
    if (n % 4 == 3) {
      fill(seg, sizeof seg, v);
      hls_push_segment_ll(CTX, &f, 0, &full, SEG_CONTAINER_FMP4, 0.02);
    }
  }
  hls_store_close(CTX, &f, 0, &full, SEG_CONTAINER_FMP4);
  return NULL;
}

typedef struct {
  int tid;
} reader_arg_t;

static void *reader_thread(void *arg) {
  const reader_arg_t *ra = arg;
  pid_filter_t f;
  memset(&f, 0, sizeof f);
  while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
    const hls_store_t *s = hls_store_find(CTX, &f, 0, &full, SEG_CONTAINER_FMP4);
    if (s) {
      const hls_snapshot_t *snap = atomic_load_explicit(&s->snap, memory_order_acquire);
      if (snap) {
        if (snap->init_size && !uniform(snap->init_data, snap->init_size))
          atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
        for (int i = 0; i < snap->count; i++) {
          const hls_seg_t *seg = &snap->ring->segs[(snap->head + i) % HLS_MAX_SEGS];
          if (seg->data && !uniform(seg->data, seg->size))
            atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
        }
        if (snap->live_data && snap->live_len && !uniform(snap->live_data, snap->live_len))
          atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
      }
    }
    if (ra->tid == 0) hls_llhls_enable(CTX, &f, 0, &full, SEG_CONTAINER_FMP4, 0.005);
    qsbr_worker_quiescent(g_test_qsbr, ra->tid);
  }
  return NULL;
}

START_TEST(concurrent_readers_see_no_torn_or_freed_data) {
  pthread_t writer;
  pthread_t readers[NREADERS];
  reader_arg_t rargs[NREADERS];

  g_test_qsbr = qsbr_domain_create(NREADERS);
  hls_store_set_qsbr(g_test_qsbr);
  atomic_store_explicit(&g_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&g_bad, 0, memory_order_relaxed);
  atomic_store_explicit(&g_fill_counter, 0, memory_order_relaxed);
  pthread_create(&writer, NULL, writer_thread, NULL);
  for (int i = 0; i < NREADERS; i++) {
    rargs[i].tid = i;
    pthread_create(&readers[i], NULL, reader_thread, &rargs[i]);
  }
  usleep(RUN_MS * 1000);
  atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
  pthread_join(writer, NULL);
  for (int i = 0; i < NREADERS; i++) pthread_join(readers[i], NULL);
  ck_assert_int_eq(atomic_load_explicit(&g_bad, memory_order_relaxed), 0);
}
END_TEST

#define CHURN_KEYS 3
#define CHURN_READERS 4
#define CHURN_CYCLES 3000

static _Atomic int g_churn_done;

static void check_snapshot(const hls_snapshot_t *snap) {
  if (snap->init_size && !uniform(snap->init_data, snap->init_size)) atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->ring->segs[(snap->head + i) % HLS_MAX_SEGS];
    if (seg->data && !uniform(seg->data, seg->size)) atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
  }
  if (snap->live_data && snap->live_len && !uniform(snap->live_data, snap->live_len)) atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
}

static void *churn_reader(void *arg) {
  const reader_arg_t *ra = arg;
  pid_filter_t f;

  memset(&f, 0, sizeof f);
  while (!atomic_load_explicit(&g_churn_done, memory_order_acquire)) {
    for (unsigned pmt = 0; pmt < CHURN_KEYS; pmt++) {
      const hls_store_t *st = hls_store_find(CTX, &f, pmt, &full, SEG_CONTAINER_FMP4);
      const hls_snapshot_t *snap = st ? atomic_load_explicit(&st->snap, memory_order_acquire) : NULL;
      if (snap) check_snapshot(snap);
    }
    qsbr_worker_quiescent(g_test_qsbr, ra->tid);
  }
  return NULL;
}

static void *churn_sweeper(void *arg) {
  (void)arg;
  while (!atomic_load_explicit(&g_churn_done, memory_order_acquire)) hls_store_slot_reclaim_sweep();
  return NULL;
}

static void churn_cycle(const pid_filter_t *f, int cycle) {
  unsigned pmt = (unsigned)(cycle % CHURN_KEYS);
  uint8_t v = (uint8_t)(cycle / CHURN_KEYS % 250 + 1);
  uint8_t init[64];
  uint8_t part[PART_LEN];

  fill(init, sizeof init, v);
  fill(part, sizeof part, v);
  hls_store_open(CTX, f, pmt, &full, 0.01, 6, SEG_CONTAINER_FMP4);
  hls_set_init_segment(CTX, f, pmt, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
  for (int i = 0; i < 4; i++) hls_push_part(CTX, f, pmt, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.005, 1);
  hls_push_segment_ll(CTX, f, pmt, &full, SEG_CONTAINER_FMP4, 0.02);
  hls_store_close(CTX, f, pmt, &full, SEG_CONTAINER_FMP4);
}

START_TEST(open_close_churn_with_concurrent_readers) {
  pthread_t sweeper;
  pthread_t readers[CHURN_READERS];
  reader_arg_t rargs[CHURN_READERS];
  pid_filter_t f;

  memset(&f, 0, sizeof f);
  g_test_qsbr = qsbr_domain_create(CHURN_READERS);
  hls_store_set_qsbr(g_test_qsbr);
  atomic_store_explicit(&g_churn_done, 0, memory_order_relaxed);
  atomic_store_explicit(&g_bad, 0, memory_order_relaxed);
  pthread_create(&sweeper, NULL, churn_sweeper, NULL);
  for (int i = 0; i < CHURN_READERS; i++) {
    rargs[i].tid = i;
    pthread_create(&readers[i], NULL, churn_reader, &rargs[i]);
  }
  for (int i = 0; i < CHURN_CYCLES; i++) churn_cycle(&f, i);
  atomic_store_explicit(&g_churn_done, 1, memory_order_release);
  for (int i = 0; i < CHURN_READERS; i++) pthread_join(readers[i], NULL);
  pthread_join(sweeper, NULL);
  ck_assert_int_eq(atomic_load_explicit(&g_bad, memory_order_relaxed), 0);
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < CHURN_READERS; i++) qsbr_worker_quiescent(g_test_qsbr, i);
    hls_store_slot_reclaim_sweep();
  }
  for (unsigned pmt = 0; pmt < CHURN_KEYS; pmt++) ck_assert_ptr_null(hls_store_find(CTX, &f, pmt, &full, SEG_CONTAINER_FMP4));
}
END_TEST

static Suite *segstore_concurrency_suite(void) {
  Suite *s = suite_create("dipixy_segstore_concurrency");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, concurrent_readers_see_no_torn_or_freed_data);
  tcase_add_test(tc, open_close_churn_with_concurrent_readers);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  hls_store_init(4);
  SRunner *sr = srunner_create(segstore_concurrency_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
