/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/dash/dash_int.h"
#include "dipixy/altsvc.h"
#include "dipixy/version.h"
#include "lib/helper/byte_ring.h"
#include "lib/helper/strutil.h"
#include "lib/helper/log.h"
#include "dipixy/dash/lldash/lldash_int.h"

dashchunk_sub_t *g_lldash_subs;
int g_lldash_subs_n;
subtab_t g_lldash_tab;

static void free_slot(int idx) {
  atomic_store_explicit(&g_lldash_subs[idx].h.alive, DASHCHUNK_SUB_FREE, memory_order_release);
  subtab_free(&g_lldash_tab, idx);
}

void dash_lldash_init(int max_clients) {
  int n = (max_clients > 0 ? max_clients : 0) + DASHCHUNK_SUBS_HEADROOM;
  if (n < DASHCHUNK_SUBS_FLOOR) n = DASHCHUNK_SUBS_FLOOR;
  if (n > DASHCHUNK_MAX_SUBS) n = DASHCHUNK_MAX_SUBS;
  g_lldash_subs = calloc((size_t)n, sizeof *g_lldash_subs);
  if (!g_lldash_subs) {
    log_code("YS500202", TOOL_NAME ": out of memory sizing dash_lldash subscriber table (%d entries)", n);
    return;
  }
  g_lldash_subs_n = n;
  for (int i = 0; i < n; i++) pthread_mutex_init(&g_lldash_subs[i].lock, NULL);
  if (subtab_init(&g_lldash_tab, n, g_lldash_subs, sizeof *g_lldash_subs)) log_code("YS500202", TOOL_NAME ": out of memory sizing dash_lldash subscriber table (%d entries)", n);
  hls_set_part_pushed_cb(on_part_pushed);
  hls_set_segment_done_cb(on_segment_done);
  hls_set_store_closing_cb(on_store_closing);
}

int dash_lldash_subscribe(const stream_key_t *k, const route_file_t *f, conn_proto_t proto) {
  hls_store_t *s;
  const hls_snapshot_t *snap;
  uint32_t want_seg;
  int idx = -1;

  if (f->kind != ROUTE_FILE_DSEG) return -1;
  s = hls_store_find(STREAM_KEY_AS(k, dash_track_container(f->track)));
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  if (!snap || snap->part_target <= 0.0 || snap->cum_ms != f->t_ms) return -1;
  want_seg = snap->live_msn;
  idx = subtab_alloc(&g_lldash_tab);
  if (idx < 0) return -1;

  g_lldash_subs[idx].store = s;
  g_lldash_subs[idx].h.cap_ctx = k->ctx;
  g_lldash_subs[idx].filter = *k->filter;
  g_lldash_subs[idx].pmt_pid = k->pmt_pid;
  g_lldash_subs[idx].lcevc = *k->lcevc;
  g_lldash_subs[idx].want_seg = want_seg;
  g_lldash_subs[idx].h.proto = proto;
  g_lldash_subs[idx].h.fd = -1;
  g_lldash_subs[idx].h.conn_gen = 0;
  g_lldash_subs[idx].h2c = NULL;
  g_lldash_subs[idx].h2_slot = NULL;
  g_lldash_subs[idx].h3c = NULL;
  g_lldash_subs[idx].h3_sid = -1;
  g_lldash_subs[idx].h.ws_handle = -1;
  atomic_store_explicit(&g_lldash_subs[idx].finalized, 0, memory_order_relaxed);
  atomic_store_explicit(&g_lldash_subs[idx].ring_errored, 0, memory_order_relaxed);
  g_lldash_subs[idx].attached = 0;
  g_lldash_subs[idx].sent = 0;
  g_lldash_subs[idx].ended = 0;
  g_lldash_subs[idx].aborted = 0;
  if (proto == CONN_PROTO_H2 || proto == CONN_PROTO_H3) {
    byte_ring_reset(&g_lldash_subs[idx].ring, DASHCHUNK_RING_BYTES);
    if (!g_lldash_subs[idx].ring.buf) {
      free_slot(idx);
      return -1;
    }
  }
  atomic_store_explicit(&g_lldash_subs[idx].h.alive, DASHCHUNK_SUB_ALIVE, memory_order_release);
  link_store_chain(s, idx);
  return idx;
}

void dash_lldash_flush_ready(int tid) {
  int i;
  i = subtab_first(&g_lldash_tab, tid);
  while (i != -1) {
    const dashchunk_sub_t *s = &g_lldash_subs[i];
    int next = subtab_next(&g_lldash_tab, i);
    if (atomic_load_explicit(&s->h.alive, memory_order_relaxed) == DASHCHUNK_SUB_ALIVE &&
        (dash_lldash_ring_pending(i) || atomic_load_explicit(&s->finalized, memory_order_acquire) ||
         atomic_load_explicit(&s->ring_errored, memory_order_acquire))) {
#ifdef HAVE_HTTP2
      if (s->h.proto == CONN_PROTO_H2) h2_dashchunk_wake(i);
#endif
#ifdef HAVE_HTTP3
      if (s->h.proto == CONN_PROTO_H3) h3_dashchunk_wake(i);
#endif
    }
    i = next;
  }
}

int dash_lldash_try_attach(conn_t *c, const stream_key_t *k, const route_file_t *f, int keep_alive, const char *origin_hdr, int ws_handle) {
  char cors_hdr[192];
  char hdr[448];
  sbuf_t b;
  int idx = dash_lldash_subscribe(STREAM_KEY_AS(k, SEG_CONTAINER_FMP4), f, 1);
  if (idx < 0) return 0;
  g_lldash_subs[idx].h.fd = c->fd;
  g_lldash_subs[idx].h.conn_gen = atomic_load_explicit(&c->gen, memory_order_relaxed);
  g_lldash_subs[idx].h.ws_handle = ws_handle;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 200 OK\r\nServer: " TOOL_NAME "/" TOOL_VERSION "\r\nContent-Type: video/mp4\r\nTransfer-Encoding: chunked\r\nCache-Control: no-cache, no-store, must-revalidate\r\n");
  sbuf_add(&b, cors_hdr);
  sbuf_add(&b, altsvc_h1_line(c->ssl != NULL));
  sbuf_add(&b, "Connection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\n\r\n");
  conn_queue(c, hdr, b.len);
  c->slot = idx;
  c->next_state = CONN_NEXT_DASHCHUNK;
  c->keep_alive = keep_alive ? 1 : 0;
  return 1;
}

void dash_lldash_sub_attach(int slot) {
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  sub_attach(slot);
}

int dash_lldash_sub_finalized(int slot) {
  if (slot < 0 || slot >= g_lldash_subs_n) return 1;
  return atomic_load_explicit(&g_lldash_subs[slot].finalized, memory_order_acquire);
}

static void dashchunk_finish_sub_close(void *arg) {
  int slot = (int)(intptr_t)arg;
  subtab_unlink(&g_lldash_tab, slot);
  free_slot(slot);
}

void dash_lldash_sub_close(int slot) {
  dashchunk_sub_t *s;
  int expected;
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  s = &g_lldash_subs[slot];
  expected = DASHCHUNK_SUB_ALIVE;
  if (!atomic_compare_exchange_strong_explicit(&s->h.alive, &expected, DASHCHUNK_SUB_CLOSING, memory_order_acquire, memory_order_relaxed)) return;
  unlink_store_chain(s->store, slot);
  if (!capture_defer_after_quiescent(dashchunk_finish_sub_close, (void *)(intptr_t)slot)) {
    capture_wait_pumps_quiescent();
    dashchunk_finish_sub_close((void *)(intptr_t)slot);
  }
}
