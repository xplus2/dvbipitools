/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

void ring_ref(hls_seg_ring_t *r) {
  atomic_fetch_add_explicit(&r->refcount, 1, memory_order_relaxed);
}

void ring_unref(hls_seg_ring_t *r) {
  if (!r) return;
  if (atomic_fetch_sub_explicit(&r->refcount, 1, memory_order_acq_rel) == 1) {
    for (int i = 0; i < HLS_MAX_SEGS; i++) seg_buf_unref(r->segs[i].data);
    free(r);
  }
}

hls_seg_ring_t *ring_cow(hls_snapshot_t *ns) {
  hls_seg_ring_t *r = ns->ring;
  hls_seg_ring_t *nr;
  if (atomic_load_explicit(&r->refcount, memory_order_acquire) == 1) return r;
  nr = malloc(sizeof *nr);
  if (!nr) return NULL;
  memcpy(nr->segs, r->segs, sizeof nr->segs);
  atomic_init(&nr->refcount, 1);
  for (int i = 0; i < HLS_MAX_SEGS; i++) seg_buf_ref(nr->segs[i].data);
  ring_unref(r);
  ns->ring = nr;
  return nr;
}

/* floor for reopened stores: seq and ETags stay unique */
static _Atomic uint32_t g_seq_floor;

void seq_floor_note(uint32_t next_seq) {
  uint32_t cur = atomic_load_explicit(&g_seq_floor, memory_order_relaxed);
  while (next_seq > cur && !atomic_compare_exchange_weak_explicit(&g_seq_floor, &cur, next_seq, memory_order_relaxed, memory_order_relaxed)) {}
}

hls_snapshot_t *snap_clone(const hls_snapshot_t *base) {
  hls_snapshot_t *ns = malloc(sizeof *ns);
  if (!ns) return NULL;
  if (!base) {
    memset(ns, 0, sizeof *ns);
    ns->ring = malloc(sizeof *ns->ring);
    if (!ns->ring) {
      free(ns);
      return NULL;
    }
    memset(ns->ring, 0, sizeof *ns->ring);
    atomic_init(&ns->ring->refcount, 1);
    ns->next_seq = atomic_load_explicit(&g_seq_floor, memory_order_relaxed);
    ns->oldest_seq = ns->next_seq;
    ns->live_msn = ns->next_seq;
    return ns;
  }
  *ns = *base;
  ring_ref(ns->ring);
  seg_buf_ref(ns->live_data);
  seg_buf_ref(ns->init_data);
  atomic_store_explicit(&ns->cache_plain, NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_ll, NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_lcevc[0], NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_lcevc[1], NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_mpd, NULL, memory_order_relaxed);
  for (int i = 0; i < MPD_LL_VARIANTS; i++) atomic_store_explicit(&ns->cache_mpd_ll[i], NULL, memory_order_relaxed);
  return ns;
}

typedef struct {
  _Atomic int refcnt;
} cached_text_hdr_t;

void cached_text_ref(const char *text) {
  if (!text) return;
  atomic_fetch_add_explicit(&((cached_text_hdr_t *)text - 1)->refcnt, 1, memory_order_relaxed);
}

void cached_text_unref(const char *text) {
  cached_text_hdr_t *h;
  if (!text) return;
  h = (cached_text_hdr_t *)text - 1;
  if (atomic_fetch_sub_explicit(&h->refcnt, 1, memory_order_acq_rel) == 1) free(h);
}

static void cached_text_free(cached_text_t *t) {
  if (!t) return;
  cached_text_unref(t->text);
  free(t);
}

void snap_free(hls_snapshot_t *ns) {
  if (!ns) return;
  ring_unref(ns->ring);
  seg_buf_unref(ns->live_data);
  seg_buf_unref(ns->init_data);
  cached_text_free(atomic_load_explicit(&ns->cache_plain, memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_ll, memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_lcevc[0], memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_lcevc[1], memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_mpd, memory_order_relaxed));
  for (int i = 0; i < MPD_LL_VARIANTS; i++) cached_text_free(atomic_load_explicit(&ns->cache_mpd_ll[i], memory_order_relaxed));
  free(ns);
}

static cached_text_t *cached_text_build(const char *key, text_fmt_fn fmt, void *ctx, size_t buf_cap) {
  cached_text_t *nc = malloc(sizeof *nc);
  cached_text_hdr_t *h;
  char *buf;
  if (!nc) return NULL;
  h = malloc(sizeof *h + buf_cap);
  if (!h) {
    free(nc);
    return NULL;
  }
  atomic_init(&h->refcnt, 1);
  buf = (char *)(h + 1);
  nc->len = fmt(ctx, buf, buf_cap);
  nc->text = buf;
  bufcpy(nc->key, sizeof nc->key, key);
  /* cppcheck-suppress memleak -- h escapes via buf=(char*)(h+1), freed by cached_text_unref/cached_text_free */
  return nc;
}

const cached_text_t *snapshot_cache_text(_Atomic(cached_text_t *) *slot, text_fmt_fn fmt, void *ctx, size_t buf_cap) {
  int owned;
  return snapshot_cache_text_keyed(slot, 1, "", fmt, ctx, buf_cap, &owned);
}

void cached_text_release(const cached_text_t *t) {
  cached_text_free((cached_text_t *)t);
}

const cached_text_t *snapshot_cache_text_keyed(_Atomic(cached_text_t *) *slots, int n, const char *key, text_fmt_fn fmt, void *ctx, size_t buf_cap, int *owned) {
  cached_text_t *nc = NULL;
  *owned = 0;
  for (int i = 0; i < n; i++) {
    const cached_text_t *cur = atomic_load_explicit(&slots[i], memory_order_acquire);
    cached_text_t *expected = NULL;
    if (cur) {
      if (!strcmp(cur->key, key)) {
        cached_text_free(nc);
        return cur;
      }
      continue;
    }
    if (!nc) {
      nc = cached_text_build(key, fmt, ctx, buf_cap);
      if (!nc) return NULL;
    }
    if (atomic_compare_exchange_strong_explicit(&slots[i], &expected, nc, memory_order_release, memory_order_acquire)) return nc;
    if (!strcmp(expected->key, key)) {
      cached_text_free(nc);
      return expected;
    }
  }
  if (!nc) nc = cached_text_build(key, fmt, ctx, buf_cap);
  *owned = nc != NULL;
  return nc;
}

/* RFC8216 4.3.3.1: TARGETDURATION must not change. td_hw immune to ring eviction shrink. */
int hls_target_duration(const hls_snapshot_t *snap) {
  int td = (int)ceil(snap->td_hw);
  return td > 0 ? td : 1;
}
