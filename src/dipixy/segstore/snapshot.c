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
  atomic_store_explicit(&ns->cache_mpd_ll, NULL, memory_order_relaxed);
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
  cached_text_free(atomic_load_explicit(&ns->cache_mpd_ll, memory_order_relaxed));
  free(ns);
}

const cached_text_t *snapshot_cache_text(_Atomic(cached_text_t *) *slot, text_fmt_fn fmt, void *ctx, size_t buf_cap) {
  const cached_text_t *cur = atomic_load_explicit(slot, memory_order_acquire);
  cached_text_t *nc;
  cached_text_hdr_t *h;
  char *buf;
  cached_text_t *expected;
  if (cur) return cur;
  nc = malloc(sizeof *nc);
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
  expected = NULL;
  /* cppcheck-suppress memleak -- h escapes via buf=(char*)(h+1), freed by cached_text_unref/cached_text_free */
  if (atomic_compare_exchange_strong_explicit(slot, &expected, nc, memory_order_release, memory_order_acquire)) return nc;
  cached_text_unref(buf);
  free(nc);
  /* cppcheck-suppress memleak -- h freed above via cached_text_unref(buf) */
  return expected;
}

/* RFC8216 4.3.3.1: TARGETDURATION must not change. td_hw immune to ring eviction shrink. */
int hls_target_duration(const hls_snapshot_t *snap) {
  int td = (int)ceil(snap->td_hw);
  return td > 0 ? td : 1;
}
