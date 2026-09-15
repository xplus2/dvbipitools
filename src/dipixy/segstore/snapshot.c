/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

hls_snapshot_t *snap_clone(const hls_snapshot_t *base) {
  hls_snapshot_t *ns = malloc(sizeof *ns);
  if (!ns) return NULL;
  if (!base) {
    memset(ns, 0, sizeof *ns);
    return ns;
  }
  *ns = *base;
  for (int i = 0; i < ns->count; i++) seg_buf_ref(ns->segs[(ns->head + i) % HLS_MAX_SEGS].data);
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

static void cached_text_free(cached_text_t *t) {
  if (!t) return;
  free(t->text);
  free(t);
}

void snap_free(hls_snapshot_t *ns) {
  if (!ns) return;
  for (int i = 0; i < ns->count; i++) seg_buf_unref(ns->segs[(ns->head + i) % HLS_MAX_SEGS].data);
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
  char *buf;
  cached_text_t *expected;
  if (cur) return cur;
  nc = malloc(sizeof *nc);
  if (!nc) return NULL;
  buf = malloc(buf_cap);
  if (!buf) {
    free(nc);
    return NULL;
  }
  nc->len = fmt(ctx, buf, buf_cap);
  nc->text = buf;
  expected = NULL;
  if (atomic_compare_exchange_strong_explicit(slot, &expected, nc, memory_order_release, memory_order_acquire)) return nc;
  free(buf);
  free(nc);
  return expected;
}

/* RFC8216 4.3.3.1: TARGETDURATION must not change. td_hw immune to ring eviction shrink. */
int hls_target_duration(const hls_snapshot_t *snap) {
  int td = (int)ceil(snap->td_hw);
  return td > 0 ? td : 1;
}
