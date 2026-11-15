/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../version.h"

#include "lib/helper/growutil.h"
#include "lib/helper/dateutil.h"

#include <sched.h>
#include <stdlib.h>

static _Atomic(hls_seg_ctx_t *) *g_stores;
static int g_stores_n;
static stream_key_index_t g_index;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_closing_cv = PTHREAD_COND_INITIALIZER;
static hls_seg_ctx_t *g_closing[HLS_MAX_STORES]; /* swept, store close pending. g_mtx */
static int g_closing_n;
static qsbr_domain_t *g_seg_qsbr;
static int g_max_variants;
static _Atomic int64_t g_variant_warn_ms;

static seg_retiree_t *g_retired;
static pthread_mutex_t g_retire_mtx = PTHREAD_MUTEX_INITIALIZER;

void hls_seg_set_qsbr(qsbr_domain_t *d) { g_seg_qsbr = d; }

void hls_seg_set_max_variants(int n) { g_max_variants = n > 0 ? n : 0; }

void hls_seg_init(int max_channels) {
  int n = max_channels > 0 ? max_channels : 1;
  if (n > HLS_MAX_STORES) n = HLS_MAX_STORES;
  g_stores = calloc((size_t)n, sizeof *g_stores);
  if (!g_stores || stream_key_index_init(&g_index, n)) {
    free(g_stores);
    g_stores = NULL;
    log_code("YS500218", TOOL_NAME ": out of memory sizing segmenter table (%d entries)", n);
    return;
  }
  g_stores_n = n;
}

int seg_try_pin(hls_seg_ctx_t *s) {
  int cur = atomic_load_explicit(&s->refcount, memory_order_relaxed);
  for (;;) {
    if (cur <= 0) return 0;
    if (atomic_compare_exchange_weak_explicit(&s->refcount, &cur, cur + 1, memory_order_acquire, memory_order_relaxed)) return 1;
  }
}

void seg_unpin(hls_seg_ctx_t *s) {
  atomic_fetch_sub_explicit(&s->refcount, 1, memory_order_release);
}

static int seg_key_equal(const hls_seg_ctx_t *s, const stream_key_t *k) {
  return s->cap_ctx == k->ctx && s->pmt_pid == k->pmt_pid && s->container == k->container && pid_filter_equal(&s->filter, k->filter) && lcevc_select_equal(&s->lcevc, k->lcevc);
}

static uint32_t seg_hash(const hls_seg_ctx_t *s) {
  stream_key_t k = {s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, s->container};
  return stream_key_hash(&k);
}

static hls_seg_ctx_t *index_find(const stream_key_t *k, memory_order mo) {
  for (int i = stream_key_index_first(&g_index, stream_key_hash(k)); i >= 0; i = stream_key_index_next(&g_index, i)) {
    hls_seg_ctx_t *s = atomic_load_explicit(&g_stores[i], mo);
    if (s && seg_key_equal(s, k)) return s;
  }
  return NULL;
}

int buf_reserve(unsigned char **buf, size_t *cap, size_t need) {
  return growbuf_reserve((void **)buf, cap, 1, need, 65536);
}

/* preallocation guess, buf_reserve still grows past this */
#define HLS_SEG_BUF_ASSUMED_BPS (20 * 1000 * 1000 / 8)

/* hard cap: sat transponder ceiling, times factor for GOPs longer than seg_target */
#define HLS_SEG_BUF_CAP_BPS (30 * 1000 * 1000 / 8)
#define HLS_SEG_BUF_MAX_FACTOR 4

/* caller must hold g_mtx. container in key: ts, fmp4 segmenters coexist per channel */
hls_seg_ctx_t *find_locked(const stream_key_t *k) { return index_find(k, memory_order_relaxed); }

static int closing_pending_locked(const stream_key_t *k) {
  for (int i = 0; i < g_closing_n; i++) {
    if (seg_key_equal(g_closing[i], k)) return 1;
  }
  return 0;
}

static void closing_remove(const hls_seg_ctx_t *s) {
  pthread_mutex_lock(&g_mtx);
  for (int i = 0; i < g_closing_n; i++) {
    if (g_closing[i] == s) {
      g_closing[i] = g_closing[--g_closing_n];
      break;
    }
  }
  pthread_cond_broadcast(&g_closing_cv);
  pthread_mutex_unlock(&g_mtx);
}

void hls_seg_registry_lock(void) { pthread_mutex_lock(&g_mtx); }
void hls_seg_registry_unlock(void) { pthread_mutex_unlock(&g_mtx); }

hls_seg_ctx_t *hls_seg_find_locked(const stream_key_t *k) {
  return find_locked(k);
}

/* lock free touch, somewhat. fails if s mid removal by sweep_idle, caller retries under g_mtx */
int touch_pinned(hls_seg_ctx_t *s, double part_target, int *llhls_newly_on) {
  if (!seg_try_pin(s)) return 0;
  atomic_store_explicit(&s->last_request_ms, now_ms(), memory_order_relaxed);
  *llhls_newly_on = part_target > 0.0 && atomic_load_explicit(&s->part.part_target, memory_order_relaxed) <= 0.0;
  if (*llhls_newly_on) atomic_store_explicit(&s->part.part_target, part_target, memory_order_release);
  seg_unpin(s);
  return 1;
}

/* caller holds g_mtx */
static int ctx_variant_count(capture_ctx_t *ctx) {
  int n = 0;
  const hls_seg_ctx_t *cur = atomic_load_explicit(capture_hls_seg_head_ptr(ctx), memory_order_acquire);
  for (; cur; cur = atomic_load_explicit(&cur->chain_next, memory_order_relaxed)) n++;
  return n;
}

/* at most once a minute: attacker drives call rate */
static void warn_variant_cap(void) {
  int64_t now = now_ms();
  int64_t last = atomic_load_explicit(&g_variant_warn_ms, memory_order_relaxed);
  if (last && now - last < 60000) return;
  if (!atomic_compare_exchange_strong_explicit(&g_variant_warn_ms, &last, now, memory_order_relaxed, memory_order_relaxed)) return;
  log_code("YW500229", TOOL_NAME ": segmenter variant cap (%d per source) reached, rejecting new variants", g_max_variants);
}

int hls_seg_touch(const stream_key_t *k, double seg_target, int max_segs, double part_target) {
  hls_seg_ctx_t *s;
  int i;
  int llhls_newly_on = 0;
  _Atomic(void *) *head;
  void *old_head;
  s = index_find(k, memory_order_acquire);
  if (s && touch_pinned(s, part_target, &llhls_newly_on)) {
    capture_close(k->ctx); /* redundant ref: existing segmenter already holds its own */
    if (llhls_newly_on) hls_llhls_enable(k, part_target);
    return 1;
  }

  pthread_mutex_lock(&g_mtx);
  while (closing_pending_locked(k)) pthread_cond_wait(&g_closing_cv, &g_mtx); /* old store still open: reuse would be closed under us */
  s = find_locked(k);
  if (s) {
    /* g_mtx excludes sweep removal: pin here cannot fail */
    touch_pinned(s, part_target, &llhls_newly_on);
    pthread_mutex_unlock(&g_mtx);
    capture_close(k->ctx);
    if (llhls_newly_on) hls_llhls_enable(k, part_target);
    return 1;
  }
  if (g_max_variants > 0 && ctx_variant_count(k->ctx) >= g_max_variants) {
    pthread_mutex_unlock(&g_mtx);
    warn_variant_cap();
    capture_close(k->ctx);
    return 0;
  }
  s = calloc(1, sizeof *s);
  if (!s) {
    pthread_mutex_unlock(&g_mtx);
    capture_close(k->ctx);
    return 0;
  }
  s->cap_ctx = k->ctx;
  s->filter = *k->filter;
  s->drop = *k->filter;
  pid_mask_from_filter(&s->drop_mask, &s->drop);
  s->pmt_pid = k->pmt_pid;
  s->lcevc = *k->lcevc;
  s->video.seg_target = seg_target;
  if (k->container == SEG_CONTAINER_TS) {
    buf_reserve(&s->buf, &s->cap, (size_t)(seg_target * HLS_SEG_BUF_ASSUMED_BPS));
    s->buf_max = (size_t)((seg_target > 2.0 ? seg_target : 2.0) * HLS_SEG_BUF_CAP_BPS) * HLS_SEG_BUF_MAX_FACTOR;
  }
  s->video.max_segs = max_segs;
  s->video.boot_left = max_segs;
  s->container = k->container;
  atomic_store_explicit(&s->part.part_target, part_target, memory_order_relaxed);
  s->video.first_ts = -1;
  s->fmp4.fmp4_audio_track_idx = -1;
  atomic_store_explicit(&s->mp4push_sub_head, -1, memory_order_relaxed);
  atomic_store_explicit(&s->last_request_ms, now_ms(), memory_order_relaxed);
  atomic_store_explicit(&s->refcount, 1, memory_order_relaxed);
  s->demux.psi = psi_new();
  if (s->demux.psi && k->pmt_pid) psi_select_pmt_pid(s->demux.psi, k->pmt_pid);
  s->demux.pes = s->demux.psi ? pes_new(hls_seg_on_pes, s) : NULL;
  if (!s->demux.psi || !s->demux.pes) goto fail;

  for (i = 0; i < g_stores_n; i++) {
    if (!atomic_load_explicit(&g_stores[i], memory_order_relaxed)) break;
  }
  if (i == g_stores_n) goto fail; /* registry full */
  hls_store_open(k, seg_target, max_segs);
  s->store = hls_store_find(k);
  if (!s->store) {
    hls_store_slot_reclaim_sweep(); /* closed slots may have passed qsbr since maint ran */
    hls_store_open(k, seg_target, max_segs);
    s->store = hls_store_find(k);
  }
  if (!s->store) goto fail; /* store table full */
  head = capture_hls_seg_head_ptr(k->ctx);
  old_head = atomic_load_explicit(head, memory_order_acquire);
  atomic_store_explicit(&s->chain_next, old_head, memory_order_relaxed);
  atomic_store_explicit(head, s, memory_order_release);
  atomic_store_explicit(&g_stores[i], s, memory_order_release);
  stream_key_index_insert(&g_index, i, stream_key_hash(k));
  pthread_mutex_unlock(&g_mtx);

  if (part_target > 0.0) hls_llhls_enable(k, part_target);
  return 1;

fail:
  psi_free(s->demux.psi);
  pes_free(s->demux.pes);
  free(s->buf);
  free(s);
  pthread_mutex_unlock(&g_mtx);
  capture_close(k->ctx);
  return 0;
}

void unlink_from_ctx_chain(hls_seg_ctx_t *victim) {
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

static void seg_reclaim_retired(void) {
  seg_retiree_t *keep = NULL;
  seg_retiree_t *list;
  pthread_mutex_lock(&g_retire_mtx);
  list = g_retired;
  g_retired = NULL;
  pthread_mutex_unlock(&g_retire_mtx);
  while (list) {
    seg_retiree_t *n = list;
    list = n->next;
    if (qsbr_mark_passed(g_seg_qsbr, n->mark)) {
      free(n->s);
      free(n);
    } else {
      n->next = keep;
      keep = n;
    }
  }
  if (!keep) return;
  pthread_mutex_lock(&g_retire_mtx);
  while (keep) {
    seg_retiree_t *n = keep;
    keep = n->next;
    n->next = g_retired;
    g_retired = n;
  }
  pthread_mutex_unlock(&g_retire_mtx);
}

static void seg_retire(hls_seg_ctx_t *s) {
  seg_retiree_t *n = malloc(sizeof *n);
  if (!n) {
    log_code("YE500219", TOOL_NAME ": segmenter retire alloc failed, leaking segmenter");
    return;
  }
  qsbr_mark(g_seg_qsbr, n->mark);
  n->s = s;
  pthread_mutex_lock(&g_retire_mtx);
  n->next = g_retired;
  g_retired = n;
  pthread_mutex_unlock(&g_retire_mtx);
}

void hls_seg_sweep_idle(void) {
  hls_seg_ctx_t *victims[HLS_MAX_STORES];
  int nvictims = 0;
  int i;
  int64_t now = now_ms();

  pthread_mutex_lock(&g_mtx);
  for (i = 0; i < g_stores_n; i++) {
    hls_seg_ctx_t *s = atomic_load_explicit(&g_stores[i], memory_order_relaxed);
    if (s && now - atomic_load_explicit(&s->last_request_ms, memory_order_relaxed) > (int64_t)(s->video.seg_target * (double)s->video.max_segs * 1500.0) &&
        atomic_load_explicit(&s->mp4push_sub_head, memory_order_relaxed) == -1) {
      victims[nvictims++] = s;
      g_closing[g_closing_n++] = s;
      atomic_store_explicit(&g_stores[i], NULL, memory_order_release);
      stream_key_index_remove(&g_index, i, seg_hash(s));
      unlink_from_ctx_chain(s);
    }
  }
  pthread_mutex_unlock(&g_mtx);
  for (i = 0; i < nvictims; i++) {
    /* releases array's pin. nonzero: racing touch pinned first, wait its unpin */
    if (atomic_fetch_sub_explicit(&victims[i]->refcount, 1, memory_order_acq_rel) != 1) {
      while (atomic_load_explicit(&victims[i]->refcount, memory_order_acquire) != 0) sched_yield();
    }
  }
  if (nvictims) capture_wait_pumps_quiescent(); /* victim's owning pump thread may still be mid feed_one() unlocked */
  for (i = 0; i < nvictims; i++) {
    hls_seg_ctx_t *s = victims[i];
    hls_store_close(&(stream_key_t){s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, s->container});
    if (s->store_v) hls_store_close(&(stream_key_t){s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, SEG_CONTAINER_FMP4_VIDEO});
    if (s->store_a) hls_store_close(&(stream_key_t){s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, SEG_CONTAINER_FMP4_AUDIO});
    closing_remove(s);
    capture_close(s->cap_ctx);
    psi_free(s->demux.psi);
    pes_free(s->demux.pes);
    free(s->buf);
    free(s->video.nal_scratch);
    free(s->video.av1_rb);
    fmp4_mux_free_all(s);
    free(s->fmp4.pend_data);
    free(s->audio.rem);
    free(s->lcevc_track.pend_data);
    seg_retire(s);
  }
  seg_reclaim_retired();
}
