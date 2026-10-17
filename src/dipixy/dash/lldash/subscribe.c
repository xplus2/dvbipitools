/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/dash/dash_int.h"
#include "dipixy/altsvc.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/version.h"
#include "dipixy/ws/ws_clients.h"
#include "lib/helper/byte_ring.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "dipixy/dash/lldash/lldash_int.h"

dashchunk_sub_t *g_lldash_subs;
int g_lldash_subs_n;
int g_lldash_reactor_efds[DASHCHUNK_MAX_REACTOR_THREADS];
int g_lldash_tid_head[DASHCHUNK_MAX_REACTOR_THREADS];
static int *g_free_slots;
static int g_free_slots_n;
static pthread_mutex_t g_free_lock = PTHREAD_MUTEX_INITIALIZER;

static void free_slot(int idx) {
  atomic_store_explicit(&g_lldash_subs[idx].alive, DASHCHUNK_SUB_FREE, memory_order_release);
  pthread_mutex_lock(&g_free_lock);
  g_free_slots[g_free_slots_n++] = idx;
  pthread_mutex_unlock(&g_free_lock);
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
  for (int i = 0; i < DASHCHUNK_MAX_REACTOR_THREADS; i++) {
    g_lldash_reactor_efds[i] = -1;
    g_lldash_tid_head[i] = -1;
  }
  g_free_slots = malloc(sizeof(int) * (size_t)n);
  if (g_free_slots) {
    for (int i = 0; i < n; i++) g_free_slots[i] = i;
    g_free_slots_n = n;
  }
  hls_set_part_pushed_cb(on_part_pushed);
  hls_set_segment_done_cb(on_segment_done);
  hls_set_store_closing_cb(on_store_closing);
}

int dash_lldash_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, conn_proto_t proto) {
  hls_store_t *s;
  const hls_snapshot_t *snap;
  seg_container_t container;
  uint64_t want_t_ms;
  uint32_t want_seg;
  int idx = -1;

  if (!parse_dash_seg_filename(filename, &want_t_ms, &container)) return -1;
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  if (!snap || snap->part_target <= 0.0 || snap->cum_ms != want_t_ms) return -1;
  want_seg = snap->live_msn;
  pthread_mutex_lock(&g_free_lock);
  if (g_free_slots_n > 0) idx = g_free_slots[--g_free_slots_n];
  pthread_mutex_unlock(&g_free_lock);
  if (idx < 0) return -1;

  g_lldash_subs[idx].store = s;
  g_lldash_subs[idx].cap_ctx = ctx;
  g_lldash_subs[idx].filter = *filter;
  g_lldash_subs[idx].pmt_pid = pmt_pid;
  g_lldash_subs[idx].lcevc = *lcevc;
  g_lldash_subs[idx].want_seg = want_seg;
  g_lldash_subs[idx].proto = proto;
  g_lldash_subs[idx].fd = -1;
  g_lldash_subs[idx].conn_gen = 0;
  g_lldash_subs[idx].h2c = NULL;
  g_lldash_subs[idx].h2_slot = NULL;
  g_lldash_subs[idx].h3c = NULL;
  g_lldash_subs[idx].h3_sid = -1;
  g_lldash_subs[idx].ws_handle = -1;
  g_lldash_subs[idx].reactor_tid = -1;
  g_lldash_subs[idx].tid_next = -1;
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
  atomic_store_explicit(&g_lldash_subs[idx].alive, DASHCHUNK_SUB_ALIVE, memory_order_release);
  link_store_chain(s, idx);
  return idx;
}

void dash_lldash_flush_ready(int tid) {
  int i;
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  i = g_lldash_tid_head[tid];
  while (i != -1) {
    const dashchunk_sub_t *s = &g_lldash_subs[i];
    int next = s->tid_next;
    if (atomic_load_explicit(&s->alive, memory_order_relaxed) == DASHCHUNK_SUB_ALIVE &&
        (dash_lldash_ring_pending(i) || atomic_load_explicit(&s->finalized, memory_order_acquire) ||
         atomic_load_explicit(&s->ring_errored, memory_order_acquire))) {
#ifdef HAVE_HTTP2
      if (s->proto == CONN_PROTO_H2) h2_dashchunk_wake(i);
#endif
#ifdef HAVE_HTTP3
      if (s->proto == CONN_PROTO_H3) h3_dashchunk_wake(i);
#endif
    }
    i = next;
  }
}

int dash_lldash_try_attach(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int keep_alive, const char *origin_hdr, int ws_handle) {
  char cors_hdr[192];
  char hdr[448];
  sbuf_t b;
  int idx = dash_lldash_subscribe(ctx, filter, pmt_pid, lcevc, filename, 1);
  if (idx < 0) return 0;
  g_lldash_subs[idx].fd = c->fd;
  g_lldash_subs[idx].conn_gen = atomic_load_explicit(&c->gen, memory_order_relaxed);
  g_lldash_subs[idx].ws_handle = ws_handle;
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
  const dashchunk_sub_t *s = &g_lldash_subs[slot];
  if (s->proto == CONN_PROTO_H2 || s->proto == CONN_PROTO_H3) unlink_tid_chain(slot);
  free_slot(slot);
}

void dash_lldash_sub_close(int slot) {
  dashchunk_sub_t *s;
  int expected;
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  s = &g_lldash_subs[slot];
  expected = DASHCHUNK_SUB_ALIVE;
  if (!atomic_compare_exchange_strong_explicit(&s->alive, &expected, DASHCHUNK_SUB_CLOSING, memory_order_acquire, memory_order_relaxed)) return;
  unlink_store_chain(s->store, slot);
  if (!capture_defer_after_quiescent(dashchunk_finish_sub_close, (void *)(intptr_t)slot)) {
    capture_wait_pumps_quiescent();
    dashchunk_finish_sub_close((void *)(intptr_t)slot);
  }
}
