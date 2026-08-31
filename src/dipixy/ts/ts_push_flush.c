/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ts_push_int.h"
#include "../reactor/conn.h"

#include <string.h>

#ifdef HAVE_HTTP2
void ts_push_h2_enqueue(int sub_idx, const uint8_t *pkt, size_t len) {
  ts_sub_t *s;
  uint32_t wpos, rpos, idx;
  size_t first;
  if (sub_idx < 0 || sub_idx >= g_ts_subs_n || !len)
    return;
  s = &g_ts_subs[sub_idx];
  if (!s->h2_ring)
    return;
  wpos = atomic_load_explicit(&s->h2_wpos, memory_order_relaxed);
  rpos = atomic_load_explicit(&s->h2_rpos, memory_order_acquire);
  if (len > TS_RING_H2_BYTES - (wpos - rpos))
    return; /* ring full: drop */
  idx = wpos & (TS_RING_H2_BYTES - 1u);
  first = len;
  if (idx + first > TS_RING_H2_BYTES)
    first = TS_RING_H2_BYTES - idx;
  memcpy(s->h2_ring + idx, pkt, first);
  if (first < len)
    memcpy(s->h2_ring, pkt + first, len - first);
  atomic_store_explicit(&s->h2_wpos, wpos + (uint32_t)len, memory_order_release);
  ws_clients_add_bytes(s->ws_handle, len);
  ts_push_wake_reactor(s->reactor_tid);
}
#endif

#ifdef HAVE_HTTP3
void ts_push_h3_enqueue(int sub_idx, const uint8_t *pkt, size_t len) {
  ts_sub_t *s;
  uint32_t wpos, rpos, idx;
  size_t first;
  if (sub_idx < 0 || sub_idx >= g_ts_subs_n || !len) return;
  s = &g_ts_subs[sub_idx];
  if (!s->h3_ring) return;
  wpos = atomic_load_explicit(&s->h3_wpos, memory_order_relaxed);
  rpos = atomic_load_explicit(&s->h3_rpos, memory_order_acquire);
  if (len > TS_RING_H3_BYTES - (wpos - rpos)) return; /* ring full: drop */
  idx = wpos & (TS_RING_H3_BYTES - 1u);
  first = len;
  if (idx + first > TS_RING_H3_BYTES) first = TS_RING_H3_BYTES - idx;
  memcpy(s->h3_ring + idx, pkt, first);
  if (first < len) memcpy(s->h3_ring, pkt + first, len - first);
  atomic_store_explicit(&s->h3_wpos, wpos + (uint32_t)len, memory_order_release);
  ws_clients_add_bytes(s->ws_handle, len);
  ts_push_wake_reactor(s->reactor_tid);
}
#endif

int ts_push_active_count(void) {
  int i, n = 0;
  for (i = 0; i < g_ts_subs_n; i++) if (atomic_load_explicit(&g_ts_subs[i].alive, memory_order_relaxed) == TS_SUB_ALIVE) n++;
  return n;
}

void ts_push_flush_ready(int tid) {
  int i = g_tid_head[tid];
  while (i != -1) {
    ts_sub_t *s = &g_ts_subs[i];
    int next = s->tid_next; /* captured before ts_push_drop_sub() may unlink i */
    if (atomic_load_explicit(&s->alive, memory_order_acquire) != TS_SUB_ALIVE)
      goto next_sub;
    if (!atomic_load_explicit(&s->ready, memory_order_acquire))
      goto next_sub;
    if (s->reactor_tid != tid)
      goto next_sub;
#ifdef HAVE_HTTP2
    if (s->proto == 2) {
      if (atomic_load_explicit(&s->h2_wpos, memory_order_acquire) != atomic_load_explicit(&s->h2_rpos, memory_order_relaxed))
        h2_tspush_wake(i);
      goto next_sub;
    }
#endif
    if (s->proto != 1)
      goto next_sub;
    if (atomic_load_explicit(&s->pkt_overrun, memory_order_acquire)) {
      ts_push_drop_sub(s, i);
      goto next_sub;
    }
    for (;;) {
      uint32_t wpos = atomic_load_explicit(&s->pkt_wpos, memory_order_acquire);
      uint32_t rpos = atomic_load_explicit(&s->pkt_rpos, memory_order_relaxed);
      uint32_t ring_idx, avail, contig;
      const uint8_t *p;
      conn_t *c;
      if (wpos == rpos || !s->pkt_ring)
        break;
      ring_idx = rpos & (TS_RING_PUSH_BYTES - 1u);
      avail = wpos - rpos;
      contig = TS_RING_PUSH_BYTES - ring_idx;
      if (contig > avail) contig = avail;
      p = s->pkt_ring + ring_idx;
      c = conn_for_fd(s->fd);
      if (!c || conn_send_buffered(c, p, contig, NULL, 0) < 0) {
        ts_push_drop_sub(s, i);
        break;
      }
      atomic_store_explicit(&s->pkt_rpos, rpos + contig, memory_order_release);
    }
  next_sub:
    i = next;
  }
}
