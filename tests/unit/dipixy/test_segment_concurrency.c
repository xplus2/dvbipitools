/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipixy/segment/priv.h"
#include "dipixy/segstore.h"

#define GROUP "239.255.42.41"
#define PORT 42141
#define TOUCHERS 4
#define KEYS 3
#define RUN_MS 400
#define MAX_STORES 8
#define SEG_TARGET 2.0
#define MAX_SEGS 3

static const lcevc_select_t full = {LCEVC_SEL_FULL, 0, 0};

static _Atomic int g_stop;
static _Atomic long g_touches;
static _Atomic int g_touch_failures;
static _Atomic int g_pin_failures;
static _Atomic long g_evictions;

static capture_ctx_t *open_ctx(void) {
  return capture_open(AF_INET, GROUP, PORT, NULL, 0, NULL, NULL, NULL, 0, 0);
}

static qsbr_domain_t *g_qsbr;

static void *toucher(void *arg) {
  int tid = (int)(uintptr_t)arg - 1;
  unsigned seed = (unsigned)(uintptr_t)arg;
  pid_filter_t none = {0};

  while (!atomic_load(&g_stop)) {
    unsigned pmt = 0x100 + (seed++ % KEYS);
    int ok = hls_seg_touch(open_ctx(), &none, pmt, &full, SEG_TARGET, MAX_SEGS, SEG_CONTAINER_TS, (seed & 1) ? 0.5 : 0.0);

    if (!ok) atomic_fetch_add(&g_touch_failures, 1);
    atomic_fetch_add(&g_touches, 1);
    qsbr_worker_quiescent(g_qsbr, tid);
  }
  return NULL;
}

static void *finder(void *arg) {
  capture_ctx_t *ctx = open_ctx();
  pid_filter_t none = {0};

  (void)arg;
  while (!atomic_load(&g_stop)) {
    for (unsigned k = 0; k < KEYS; k++) {
      hls_seg_ctx_t *s;
      int newly = 0;

      hls_seg_registry_lock();
      s = find_locked(ctx, &none, 0x100 + k, &full, SEG_CONTAINER_TS);
      if (s && seg_try_pin(s)) {
        if (atomic_load(&s->refcount) < 2) atomic_fetch_add(&g_pin_failures, 1);
        hls_seg_registry_unlock();
        touch_pinned(s, 0.0, &newly);
        seg_unpin(s);
      } else {
        hls_seg_registry_unlock();
      }
    }
  }
  capture_close(ctx);
  return NULL;
}

static void *evictor(void *arg) {
  capture_ctx_t *ctx = open_ctx();
  pid_filter_t none = {0};

  (void)arg;
  while (!atomic_load(&g_stop)) {
    for (unsigned k = 0; k < KEYS; k++) {
      hls_seg_ctx_t *s;

      hls_seg_registry_lock();
      s = find_locked(ctx, &none, 0x100 + k, &full, SEG_CONTAINER_TS);
      if (s) atomic_store(&s->last_request_ms, 0);
      hls_seg_registry_unlock();
    }
    hls_seg_sweep_idle();
    atomic_fetch_add(&g_evictions, 1);
  }
  capture_close(ctx);
  return NULL;
}

START_TEST(touch_pin_and_eviction_race_without_corruption) {
  pthread_t th[TOUCHERS + 2];
  struct timespec ts = {0, RUN_MS * 1000000L};
  capture_ctx_t *ctx;
  _Atomic(void *) *head;

  hls_store_init(MAX_STORES);
  hls_seg_init(MAX_STORES);
  g_qsbr = qsbr_domain_create(TOUCHERS);
  hls_seg_set_qsbr(g_qsbr);
  ctx = open_ctx();
  ck_assert_ptr_nonnull(ctx);
  for (int i = 0; i < TOUCHERS; i++) ck_assert_int_eq(pthread_create(&th[i], NULL, toucher, (void *)(uintptr_t)(i + 1)), 0);
  ck_assert_int_eq(pthread_create(&th[TOUCHERS], NULL, finder, NULL), 0);
  ck_assert_int_eq(pthread_create(&th[TOUCHERS + 1], NULL, evictor, NULL), 0);
  nanosleep(&ts, NULL);
  atomic_store(&g_stop, 1);
  for (int i = 0; i < TOUCHERS + 2; i++) pthread_join(th[i], NULL);

  ck_assert_int_eq(atomic_load(&g_touch_failures), 0);
  ck_assert_int_eq(atomic_load(&g_pin_failures), 0);
  ck_assert_int_gt((int)atomic_load(&g_touches), 0);
  ck_assert_int_gt((int)atomic_load(&g_evictions), 0);
  head = capture_hls_seg_head_ptr(ctx);
  for (int pass = 0; pass < 2; pass++) {
    pid_filter_t none = {0};

    for (unsigned k = 0; k < KEYS; k++) {
      hls_seg_ctx_t *s;

      hls_seg_registry_lock();
      s = find_locked(ctx, &none, 0x100 + k, &full, SEG_CONTAINER_TS);
      if (s) atomic_store(&s->last_request_ms, 0);
      hls_seg_registry_unlock();
    }
    hls_seg_sweep_idle();
  }
  ck_assert_ptr_null(atomic_load(head));
  capture_close(ctx);
}
END_TEST

static Suite *seg_conc_suite(void) {
  Suite *s = suite_create("dipixy_segment_concurrency");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, touch_pin_and_eviction_race_without_corruption);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(seg_conc_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
