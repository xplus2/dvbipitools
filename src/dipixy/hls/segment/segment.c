/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/helper/ioutil.h"

#include <sched.h>
#include <stdlib.h>
#include <time.h>

#define HLS_SEG_MAX_STORES 32

static _Atomic(hls_seg_ctx_t *) g_stores[HLS_SEG_MAX_STORES];
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

static int seg_try_pin(hls_seg_ctx_t *s) {
  int cur = atomic_load_explicit(&s->refcount, memory_order_relaxed);
  for (;;) {
    if (cur <= 0)
      return 0;
    if (atomic_compare_exchange_weak_explicit(&s->refcount, &cur, cur + 1, memory_order_acquire, memory_order_relaxed))
      return 1;
  }
}

static void seg_unpin(hls_seg_ctx_t *s) {
  atomic_fetch_sub_explicit(&s->refcount, 1, memory_order_release);
}

static int seg_key_equal(const hls_seg_ctx_t *s, const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container) {
  return s->cap_ctx == ctx && s->pmt_pid == pmt_pid && s->container == container && pid_filter_equal(&s->filter, filter);
}

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int buf_reserve(unsigned char **buf, size_t *cap, size_t need) {
  return growbuf_reserve((void **)buf, cap, 1, need, 65536);
}

/* preallocation guess, buf_reserve still grows past this */
#define HLS_SEG_BUF_ASSUMED_BPS (20 * 1000 * 1000 / 8)

/* caller must hold g_mtx. container in key: ts, fmp4 segmenters coexist per channel */
static hls_seg_ctx_t *find_locked(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container) {
  for (int i = 0; i < HLS_SEG_MAX_STORES; i++) {
    hls_seg_ctx_t *s = atomic_load_explicit(&g_stores[i], memory_order_relaxed);
    if (s && seg_key_equal(s, ctx, filter, pmt_pid, container)) return s;
  }
  return NULL;
}

/* lock free touch, somewhat. fails if s mid removal by sweep_idle, caller retries under g_mtx */
static int touch_pinned(hls_seg_ctx_t *s, hls_container_t container, double part_target, int *llhls_newly_on) {
  if (!seg_try_pin(s)) return 0;
  atomic_store_explicit(&s->last_request_ms, now_ms(), memory_order_relaxed);
  *llhls_newly_on = container == HLS_CONTAINER_TS && part_target > 0.0 && atomic_load_explicit(&s->part_target, memory_order_relaxed) <= 0.0;
  if (container == HLS_CONTAINER_TS && part_target > 0.0) atomic_store_explicit(&s->part_target, part_target, memory_order_release);
  seg_unpin(s);
  return 1;
}

int hls_seg_touch(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, double seg_target, int max_segs, hls_container_t container, double part_target) {
  hls_seg_ctx_t *s;
  int i;
  int llhls_newly_on = 0;

  for (i = 0; i < HLS_SEG_MAX_STORES; i++) {
    s = atomic_load_explicit(&g_stores[i], memory_order_acquire);
    if (s && seg_key_equal(s, ctx, filter, pmt_pid, container) && touch_pinned(s, container, part_target, &llhls_newly_on)) {
      capture_close(ctx); /* redundant ref: existing segmenter already holds its own */
      if (llhls_newly_on) hls_llhls_enable(ctx, filter, pmt_pid, part_target);
      return 1;
    }
  }

  pthread_mutex_lock(&g_mtx);
  s = find_locked(ctx, filter, pmt_pid, container);
  if (s) {
    /* g_mtx excludes sweep removal: pin here cannot fail */
    touch_pinned(s, container, part_target, &llhls_newly_on);
    pthread_mutex_unlock(&g_mtx);
    capture_close(ctx);
    if (llhls_newly_on) hls_llhls_enable(ctx, filter, pmt_pid, part_target);
    return 1;
  }
  s = calloc(1, sizeof *s);
  if (!s) {
    pthread_mutex_unlock(&g_mtx);
    capture_close(ctx);
    return 0;
  }
  s->cap_ctx = ctx;
  s->filter = *filter;
  s->pmt_pid = pmt_pid;
  s->seg_target = seg_target;
  buf_reserve(&s->buf, &s->cap, (size_t)(seg_target * HLS_SEG_BUF_ASSUMED_BPS));
  s->max_segs = max_segs;
  s->boot_left = max_segs;
  s->container = container;
  if (container == HLS_CONTAINER_TS) atomic_store_explicit(&s->part_target, part_target, memory_order_relaxed);
  s->first_ts_ms = -1;
  s->fmp4_audio_track_idx = -1;
  atomic_store_explicit(&s->last_request_ms, now_ms(), memory_order_relaxed);
  atomic_store_explicit(&s->refcount, 1, memory_order_relaxed);
  s->psi = psi_new();
  if (s->psi && pmt_pid) psi_select_pmt_pid(s->psi, pmt_pid);
  s->pes = s->psi ? pes_new(hls_seg_on_pes, s) : NULL;
  if (!s->psi || !s->pes) {
    psi_free(s->psi);
    pes_free(s->pes);
    free(s);
    pthread_mutex_unlock(&g_mtx);
    capture_close(ctx);
    return 0;
  }

  for (i = 0; i < HLS_SEG_MAX_STORES; i++) if (!atomic_load_explicit(&g_stores[i], memory_order_relaxed)) break;
  if (i == HLS_SEG_MAX_STORES) {
    psi_free(s->psi);
    pes_free(s->pes);
    free(s);
    pthread_mutex_unlock(&g_mtx);
    capture_close(ctx); /* registry full */
    return 0;
  }
  {
    _Atomic(void *) *head = capture_hls_seg_head_ptr(ctx);
    void *old_head = atomic_load_explicit(head, memory_order_acquire);
    atomic_store_explicit(&s->chain_next, old_head, memory_order_relaxed);
    atomic_store_explicit(head, s, memory_order_release);
  }
  atomic_store_explicit(&g_stores[i], s, memory_order_release);
  pthread_mutex_unlock(&g_mtx);

  hls_store_open(ctx, filter, pmt_pid, seg_target, max_segs, container);
  if (container == HLS_CONTAINER_TS && part_target > 0.0) hls_llhls_enable(ctx, filter, pmt_pid, part_target);
  return 1;
}

static void unlink_from_ctx_chain(hls_seg_ctx_t *victim) {
  _Atomic(void *) *head = capture_hls_seg_head_ptr(victim->cap_ctx);
  hls_seg_ctx_t *cur = atomic_load_explicit(head, memory_order_relaxed);
  hls_seg_ctx_t *next = atomic_load_explicit(&victim->chain_next, memory_order_relaxed);
  if (cur == victim) {
    atomic_store_explicit(head, next, memory_order_relaxed);
    return;
  }
  while (cur) {
    hls_seg_ctx_t *cur_next = atomic_load_explicit(&cur->chain_next, memory_order_relaxed);
    if (cur_next == victim) {
      atomic_store_explicit(&cur->chain_next, next, memory_order_relaxed);
      return;
    }
    cur = cur_next;
  }
}

void hls_seg_sweep_idle(void) {
  hls_seg_ctx_t *victims[HLS_SEG_MAX_STORES];
  int nvictims = 0;
  int i;
  int64_t now = now_ms();

  pthread_mutex_lock(&g_mtx);
  for (i = 0; i < HLS_SEG_MAX_STORES; i++) {
    hls_seg_ctx_t *s = atomic_load_explicit(&g_stores[i], memory_order_relaxed);
    if (s && now - atomic_load_explicit(&s->last_request_ms, memory_order_relaxed) > (int64_t)(s->seg_target * 6000.0)) {
      victims[nvictims++] = s;
      atomic_store_explicit(&g_stores[i], NULL, memory_order_release);
      unlink_from_ctx_chain(s);
    }
  }
  pthread_mutex_unlock(&g_mtx);
  for (i = 0; i < nvictims; i++) {
    /* releases array's pin. nonzero: racing touch pinned first, wait its unpin */
    if (atomic_fetch_sub_explicit(&victims[i]->refcount, 1, memory_order_acq_rel) != 1) {
      while (atomic_load_explicit(&victims[i]->refcount, memory_order_acquire) != 0)
        sched_yield();
    }
  }
  if (nvictims) capture_wait_pumps_quiescent(); /* victim's owning pump thread may still be mid feed_one() unlocked */
  for (i = 0; i < nvictims; i++) {
    hls_seg_ctx_t *s = victims[i];
    hls_store_close(s->cap_ctx, &s->filter, s->pmt_pid, s->container);
    capture_close(s->cap_ctx);
    psi_free(s->psi);
    pes_free(s->pes);
    free(s->buf);
    free(s->nal_scratch);
    fmp4_mux_free(s->fmux);
    free(s->fmp4_pend_data);
    free(s->audio_rem);
    free(s);
  }
}
