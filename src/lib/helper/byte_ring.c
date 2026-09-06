/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "byte_ring.h"

void byte_ring_reset(byte_ring_t *r, uint32_t cap) {
  if (!r->buf) r->buf = malloc(cap);
  r->cap = cap;
  atomic_store_explicit(&r->wpos, 0, memory_order_relaxed);
  atomic_store_explicit(&r->rpos, 0, memory_order_relaxed);
}

void byte_ring_free(byte_ring_t *r) {
  free(r->buf);
  r->buf = NULL;
}

int byte_ring_write(byte_ring_t *r, const uint8_t *data, size_t len) {
  uint32_t wpos;
  uint32_t rpos;
  uint32_t idx;
  size_t first;
  if (!len) return 1;
  if (!r->buf) return 0;
  wpos = atomic_load_explicit(&r->wpos, memory_order_relaxed);
  rpos = atomic_load_explicit(&r->rpos, memory_order_acquire);
  if (len > r->cap - (wpos - rpos)) return 0;
  idx = wpos & (r->cap - 1u);
  first = len;
  if (idx + first > r->cap) first = r->cap - idx;
  memcpy(r->buf + idx, data, first);
  if (first < len) memcpy(r->buf, data + first, len - first);
  atomic_store_explicit(&r->wpos, wpos + (uint32_t)len, memory_order_release);
  return 1;
}

size_t byte_ring_read(byte_ring_t *r, uint8_t *dst, size_t maxlen) {
  uint32_t wpos;
  uint32_t rpos;
  uint32_t idx;
  uint32_t avail;
  uint32_t n;
  if (!r->buf) return 0;
  wpos = atomic_load_explicit(&r->wpos, memory_order_acquire);
  rpos = atomic_load_explicit(&r->rpos, memory_order_relaxed);
  if (wpos == rpos) return 0;
  idx = rpos & (r->cap - 1u);
  avail = wpos - rpos;
  n = r->cap - idx;
  if (n > avail) n = avail;
  if (n > maxlen) n = (uint32_t)maxlen;
  memcpy(dst, r->buf + idx, n);
  atomic_store_explicit(&r->rpos, rpos + n, memory_order_release);
  return n;
}

const uint8_t *byte_ring_peek(const byte_ring_t *r, size_t *len) {
  uint32_t wpos;
  uint32_t rpos;
  uint32_t idx;
  uint32_t avail;
  uint32_t contig;
  *len = 0;
  if (!r->buf) return NULL;
  wpos = atomic_load_explicit(&r->wpos, memory_order_acquire);
  rpos = atomic_load_explicit(&r->rpos, memory_order_relaxed);
  if (wpos == rpos) return NULL;
  idx = rpos & (r->cap - 1u);
  avail = wpos - rpos;
  contig = r->cap - idx;
  if (contig > avail) contig = avail;
  *len = contig;
  return r->buf + idx;
}

void byte_ring_advance(byte_ring_t *r, size_t n) {
  atomic_fetch_add_explicit(&r->rpos, (uint32_t)n, memory_order_release);
}
