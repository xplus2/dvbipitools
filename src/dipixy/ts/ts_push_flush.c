/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ts_push_int.h"
#include "../reactor/conn.h"

#include <string.h>

#ifdef HAVE_HTTP2
void ts_push_h2_enqueue(int sub_idx, const uint8_t *pkt, size_t len) {
  ts_sub_t *s;
  if (sub_idx < 0 || sub_idx >= g_ts_subs_n || !len) return;
  s = &g_ts_subs[sub_idx];
  if (!byte_ring_write(&s->h2_ring, pkt, len)) {
    log_throttled(&s->ring_drop_throttle, LOG_THROTTLE_WINDOW_S, "ts_push: h2 ring full, dropping packet");
    return;
  }
  ws_clients_add_bytes(s->ws_handle, len);
  ts_push_wake_reactor(s->reactor_tid);
}
#endif

#ifdef HAVE_HTTP3
void ts_push_h3_enqueue(int sub_idx, const uint8_t *pkt, size_t len) {
  ts_sub_t *s;
  if (sub_idx < 0 || sub_idx >= g_ts_subs_n || !len) return;
  s = &g_ts_subs[sub_idx];
  if (!byte_ring_write(&s->h3_ring, pkt, len)) {
    log_throttled(&s->ring_drop_throttle, LOG_THROTTLE_WINDOW_S, "ts_push: h3 ring full, dropping packet");
    return;
  }
  ws_clients_add_bytes(s->ws_handle, len);
  ts_push_wake_reactor(s->reactor_tid);
}
#endif

int ts_push_active_count(void) {
  int n = 0;
  for (int i = 0; i < g_ts_subs_n; i++) if (atomic_load_explicit(&g_ts_subs[i].alive, memory_order_relaxed) == TS_SUB_ALIVE) n++;
  return n;
}

void ts_push_flush_ready(int tid) {
  int i = g_tid_head[tid];
  while (i != -1) {
    ts_sub_t *s = &g_ts_subs[i];
    int next = s->tid_next; /* captured before ts_push_drop_sub() may unlink i */
    if (atomic_load_explicit(&s->alive, memory_order_acquire) != TS_SUB_ALIVE) goto next_sub;
    if (!atomic_load_explicit(&s->ready, memory_order_acquire)) goto next_sub;
    if (s->reactor_tid != tid) goto next_sub;
#ifdef HAVE_HTTP2
    if (s->proto == 2) {
      if (atomic_load_explicit(&s->h2_ring.wpos, memory_order_acquire) != atomic_load_explicit(&s->h2_ring.rpos, memory_order_relaxed))
        h2_tspush_wake(i);
      goto next_sub;
    }
#endif
#ifdef HAVE_HTTP3
    if (s->proto == 3) {
      if (atomic_load_explicit(&s->h3_ring.wpos, memory_order_acquire) != atomic_load_explicit(&s->h3_ring.rpos, memory_order_relaxed))
        h3_tspush_wake(i);
      goto next_sub;
    }
#endif
    if (s->proto != 1) goto next_sub;
    if (atomic_load_explicit(&s->pkt_overrun, memory_order_acquire)) {
      ts_push_drop_sub(s, i);
      goto next_sub;
    }
    for (;;) {
      size_t contig;
      const uint8_t *p;
      conn_t *c;
      p = byte_ring_peek(&s->pkt_ring, &contig);
      if (!p) break;
      c = conn_for_fd(s->fd);
      if (!c || conn_send_buffered(c, p, contig, NULL, 0) < 0) {
        ts_push_drop_sub(s, i);
        break;
      }
      byte_ring_advance(&s->pkt_ring, contig);
    }
  next_sub:
    i = next;
  }
}
