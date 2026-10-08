/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../dipitvhead/psi_fixture.h"
#include "dipixy/segment/pidlock.h"
#include "dipixy/segment/priv.h"
#include "dipixy/segstore.h"

#define GROUP "239.255.42.40"
#define PORT 42140
#define SEG_TARGET 2.0
#define MAX_SEGS 3
#define MAX_STORES 8
#define CHAIN 3

static const lcevc_select_t full = {LCEVC_SEL_FULL, 0, 0};
void *__real_malloc(size_t size);
static int g_fail_retire_alloc;

void *__wrap_malloc(size_t size) {
  if (g_fail_retire_alloc && size >= QSBR_MAX_WORKERS * sizeof(uint64_t) && size < QSBR_MAX_WORKERS * sizeof(uint64_t) + 64) {
    g_fail_retire_alloc = 0;
    return NULL;
  }
  return __real_malloc(size);
}

static const lcevc_select_t base = {LCEVC_SEL_BASE, 0, 0};

static void world_open(void) {
  hls_store_init(MAX_STORES);
  hls_seg_init(MAX_STORES);
}

static capture_ctx_t *open_ctx(void) {
  capture_ctx_t *ctx = capture_open(AF_INET, GROUP, PORT, NULL, 0, NULL, NULL, NULL, 0, 0);

  ck_assert_ptr_nonnull(ctx);
  return ctx;
}

static hls_seg_ctx_t *make_seg(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt, const lcevc_select_t *lcevc, seg_container_t container, double part) {
  hls_seg_ctx_t *s;

  ck_assert_int_eq(hls_seg_touch(capture_open(AF_INET, GROUP, PORT, NULL, 0, NULL, NULL, NULL, 0, 0), filter, pmt, lcevc, SEG_TARGET, MAX_SEGS, container, part), 1);
  hls_seg_registry_lock();
  s = hls_seg_find_locked(ctx, filter, pmt, lcevc, container);
  hls_seg_registry_unlock();
  ck_assert_ptr_nonnull(s);
  return s;
}

static void release_all(hls_seg_ctx_t **segs, int n) {
  for (int i = 0; i < n; i++) atomic_store(&segs[i]->last_request_ms, 0);
  hls_seg_sweep_idle();
}

static int chain_length(capture_ctx_t *ctx) {
  const _Atomic(void *) *head = capture_hls_seg_head_ptr(ctx);
  const hls_seg_ctx_t *cur = atomic_load(head);
  int n = 0;

  while (cur && n < 100) {
    n++;
    cur = atomic_load(&cur->chain_next);
  }
  return n;
}

static int chain_contains(capture_ctx_t *ctx, const hls_seg_ctx_t *s) {
  const _Atomic(void *) *head = capture_hls_seg_head_ptr(ctx);
  const hls_seg_ctx_t *cur = atomic_load(head);

  while (cur) {
    if (cur == s) return 1;
    cur = atomic_load(&cur->chain_next);
  }
  return 0;
}

START_TEST(pin_follows_the_reference_count) {
  hls_seg_ctx_t s;

  memset(&s, 0, sizeof s);
  atomic_store(&s.refcount, 0);
  ck_assert_int_eq(seg_try_pin(&s), 0);
  ck_assert_int_eq(atomic_load(&s.refcount), 0);
  atomic_store(&s.refcount, -3);
  ck_assert_int_eq(seg_try_pin(&s), 0);
  atomic_store(&s.refcount, 1);
  ck_assert_int_eq(seg_try_pin(&s), 1);
  ck_assert_int_eq(atomic_load(&s.refcount), 2);
  ck_assert_int_eq(seg_try_pin(&s), 1);
  ck_assert_int_eq(atomic_load(&s.refcount), 3);
  seg_unpin(&s);
  seg_unpin(&s);
  ck_assert_int_eq(atomic_load(&s.refcount), 1);
}
END_TEST

START_TEST(lookup_matches_only_the_exact_key) {
  capture_ctx_t *ctx;
  hls_seg_ctx_t *s;
  pid_filter_t none = {0};
  pid_filter_t some = {0};
  hls_seg_ctx_t *segs[1];

  world_open();
  ctx = open_ctx();
  pid_filter_add(&some, 0x123);
  hls_seg_registry_lock();
  ck_assert_ptr_null(find_locked(ctx, &none, 0, &full, SEG_CONTAINER_TS));
  hls_seg_registry_unlock();
  s = make_seg(ctx, &none, 0, &full, SEG_CONTAINER_TS, 0.0);
  segs[0] = s;
  hls_seg_registry_lock();
  ck_assert_ptr_eq(find_locked(ctx, &none, 0, &full, SEG_CONTAINER_TS), s);
  ck_assert_ptr_null(find_locked(ctx, &none, 0x100, &full, SEG_CONTAINER_TS));
  ck_assert_ptr_null(find_locked(ctx, &none, 0, &full, SEG_CONTAINER_FMP4));
  ck_assert_ptr_null(find_locked(ctx, &some, 0, &full, SEG_CONTAINER_TS));
  ck_assert_ptr_null(find_locked(ctx, &none, 0, &base, SEG_CONTAINER_TS));
  ck_assert_ptr_null(find_locked((capture_ctx_t *)(void *)&none, &none, 0, &full, SEG_CONTAINER_TS));
  hls_seg_registry_unlock();
  release_all(segs, 1);
  capture_close(ctx);
}
END_TEST

typedef struct {
  const char *name;
  double existing_part;
  double request_part;
  int expect_newly_on;
  double expect_part_after;
} touch_case_t;

static const touch_case_t touch_cases[] = {
    {"enable low latency", 0.0, 0.5, 1, 0.5},
    {"already enabled", 0.5, 0.5, 0, 0.5},
    {"retarget while enabled", 0.5, 0.25, 0, 0.25},
    {"plain request leaves it off", 0.0, 0.0, 0, 0.0},
    {"plain request leaves it on", 0.5, 0.0, 0, 0.5},
};

START_TEST(touch_refreshes_activity_and_reports_low_latency_enablement) {
  const touch_case_t *tc = &touch_cases[_i];
  capture_ctx_t *ctx;
  hls_seg_ctx_t *s;
  hls_seg_ctx_t *segs[1];
  pid_filter_t none = {0};
  int newly = -1;

  world_open();
  ctx = open_ctx();
  s = make_seg(ctx, &none, 0, &full, SEG_CONTAINER_TS, 0.0);
  segs[0] = s;
  atomic_store(&s->part.part_target, tc->existing_part);
  atomic_store(&s->last_request_ms, 1);
  atomic_store(&s->refcount, 1);
  ck_assert_int_eq(touch_pinned(s, tc->request_part, &newly), 1);
  ck_assert_int_eq(newly, tc->expect_newly_on);
  ck_assert_double_eq(atomic_load(&s->part.part_target), tc->expect_part_after);
  ck_assert_int_gt((int)(atomic_load(&s->last_request_ms) > 1), 0);
  ck_assert_int_eq(atomic_load(&s->refcount), 1);
  release_all(segs, 1);
  capture_close(ctx);
}
END_TEST

START_TEST(touch_fails_on_a_segmenter_being_retired) {
  capture_ctx_t *ctx;
  hls_seg_ctx_t *s;
  hls_seg_ctx_t *segs[1];
  pid_filter_t none = {0};
  int newly = 7;

  world_open();
  ctx = open_ctx();
  s = make_seg(ctx, &none, 0, &full, SEG_CONTAINER_TS, 0.0);
  segs[0] = s;
  atomic_store(&s->last_request_ms, 1);
  atomic_store(&s->refcount, 0);
  ck_assert_int_eq(touch_pinned(s, 0.5, &newly), 0);
  ck_assert_int_eq(newly, 7);
  ck_assert_int_eq((int)atomic_load(&s->last_request_ms), 1);
  ck_assert_double_eq(atomic_load(&s->part.part_target), 0.0);
  atomic_store(&s->refcount, 1);
  release_all(segs, 1);
  capture_close(ctx);
}
END_TEST

START_TEST(unlinking_keeps_the_rest_of_the_chain_intact) {
  capture_ctx_t *ctx;
  hls_seg_ctx_t *segs[CHAIN];
  hls_seg_ctx_t stranger;
  pid_filter_t none = {0};
  hls_seg_ctx_t *victim;

  world_open();
  ctx = open_ctx();
  for (int i = 0; i < CHAIN; i++) segs[i] = make_seg(ctx, &none, 0x100 + (unsigned)i, &full, SEG_CONTAINER_TS, 0.0);
  ck_assert_int_eq(chain_length(ctx), CHAIN);
  victim = segs[_i];
  unlink_from_ctx_chain(victim);
  ck_assert_int_eq(chain_length(ctx), CHAIN - 1);
  ck_assert_int_eq(chain_contains(ctx, victim), 0);
  for (int i = 0; i < CHAIN; i++) {
    if (segs[i] != victim) ck_assert_int_eq(chain_contains(ctx, segs[i]), 1);
  }
  unlink_from_ctx_chain(victim);
  ck_assert_int_eq(chain_length(ctx), CHAIN - 1);
  memset(&stranger, 0, sizeof stranger);
  stranger.cap_ctx = ctx;
  unlink_from_ctx_chain(&stranger);
  ck_assert_int_eq(chain_length(ctx), CHAIN - 1);
  release_all(segs, CHAIN);
  ck_assert_int_eq(chain_length(ctx), 0);
  capture_close(ctx);
}
END_TEST

START_TEST(sweep_leaves_active_and_subscribed_segmenters_alone) {
  capture_ctx_t *ctx;
  hls_seg_ctx_t *segs[2];
  pid_filter_t none = {0};

  world_open();
  ctx = open_ctx();
  segs[0] = make_seg(ctx, &none, 0x100, &full, SEG_CONTAINER_TS, 0.0);
  segs[1] = make_seg(ctx, &none, 0x101, &full, SEG_CONTAINER_TS, 0.0);
  hls_seg_sweep_idle();
  ck_assert_int_eq(chain_length(ctx), 2);
  atomic_store(&segs[0]->last_request_ms, 0);
  atomic_store(&segs[1]->last_request_ms, 0);
  atomic_store(&segs[1]->mp4push_sub_head, 3);
  hls_seg_sweep_idle();
  ck_assert_int_eq(chain_contains(ctx, segs[0]), 0);
  ck_assert_int_eq(chain_contains(ctx, segs[1]), 1);
  atomic_store(&segs[1]->mp4push_sub_head, -1);
  hls_seg_sweep_idle();
  ck_assert_int_eq(chain_length(ctx), 0);
  capture_close(ctx);
}
END_TEST

START_TEST(sweep_retire_oom_keeps_the_segmenter_alive) {
  capture_ctx_t *ctx;
  hls_seg_ctx_t *segs[1];
  pid_filter_t none = {0};

  world_open();
  ctx = open_ctx();
  segs[0] = make_seg(ctx, &none, 0x100, &full, SEG_CONTAINER_TS, 0.0);
  atomic_store(&segs[0]->last_request_ms, 0);
  g_fail_retire_alloc = 1;
  hls_seg_sweep_idle();
  ck_assert_int_eq(g_fail_retire_alloc, 0);
  ck_assert_uint_eq(segs[0]->pmt_pid, 0x100);
  free(segs[0]);
  capture_close(ctx);
}
END_TEST

START_TEST(touch_with_an_existing_key_reuses_the_segmenter) {
  capture_ctx_t *ctx;
  hls_seg_ctx_t *a;
  hls_seg_ctx_t *segs[1];
  pid_filter_t none = {0};

  world_open();
  ctx = open_ctx();
  a = make_seg(ctx, &none, 0, &full, SEG_CONTAINER_FMP4, 0.0);
  segs[0] = a;
  ck_assert_ptr_eq(make_seg(ctx, &none, 0, &full, SEG_CONTAINER_FMP4, 0.5), a);
  ck_assert_double_eq(atomic_load(&a->part.part_target), 0.5);
  ck_assert_int_eq(chain_length(ctx), 1);
  release_all(segs, 1);
  capture_close(ctx);
}
END_TEST

static Suite *segment_suite(void) {
  Suite *s = suite_create("dipixy_segment");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, pin_follows_the_reference_count);
  tcase_add_test(tc, lookup_matches_only_the_exact_key);
  tcase_add_loop_test(tc, touch_refreshes_activity_and_reports_low_latency_enablement, 0, (int)(sizeof touch_cases / sizeof touch_cases[0]));
  tcase_add_test(tc, touch_fails_on_a_segmenter_being_retired);
  tcase_add_loop_test(tc, unlinking_keeps_the_rest_of_the_chain_intact, 0, CHAIN);
  tcase_add_test(tc, sweep_leaves_active_and_subscribed_segmenters_alone);
  tcase_add_test(tc, sweep_retire_oom_keeps_the_segmenter_alive);
  tcase_add_test(tc, touch_with_an_existing_key_reuses_the_segmenter);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(segment_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
