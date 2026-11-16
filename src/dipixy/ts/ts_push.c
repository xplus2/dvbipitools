/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* ts_push.c - HTTP MPEG-TS push: subscribers match by capture_ctx_t*,
   no ticket/username auth, no PCR pacing (multicast source already arrives in real time) */

#include "ts_push_int.h"
#include "../reactor/conn.h"
#include "../reactor/subtab.h"
#include "../reactor/wake.h"
#include "../version.h"

#include "lib/helper/log.h"

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int ts_push_pid_excluded(const void *ctx, unsigned pid) { return pid_mask_has((const pid_mask_t *)ctx, pid); }

ts_sub_t *g_ts_subs;
int g_ts_subs_n;
_Atomic int g_ts_active_count;

/* serialize per-ctx chain link/unlink. pump readers lock-free */
static pthread_mutex_t g_chain_mtx = PTHREAD_MUTEX_INITIALIZER;

/* chain T touched only by thread T: every begin/close path for subscription runs on its own owning thread (H1/H2/H3 don't migrate threads) */
subtab_t g_ts_tab;

static _Atomic uint64_t g_queue_hwm;
static _Atomic uint64_t g_queue_dropped;

int g_ts_queue_metrics;

void ts_push_note_enqueue_slow(const byte_ring_t *r, int wrote) {
  uint64_t used;
  uint64_t cur;
  if (!wrote) {
    atomic_fetch_add_explicit(&g_queue_dropped, 1, memory_order_relaxed);
    return;
  }
  used = atomic_load_explicit(&r->wpos, memory_order_relaxed) - atomic_load_explicit(&r->rpos, memory_order_relaxed);
  cur = atomic_load_explicit(&g_queue_hwm, memory_order_relaxed);
  while (used > cur) {
    if (atomic_compare_exchange_weak_explicit(&g_queue_hwm, &cur, used, memory_order_relaxed, memory_order_relaxed)) break;
  }
}


void ts_push_set_queue_metrics(int level) {
  g_ts_queue_metrics = level > 1;
}

uint64_t ts_push_queue_high_watermark(void) {
  return atomic_load_explicit(&g_queue_hwm, memory_order_relaxed);
}

uint64_t ts_push_queue_dropped(void) {
  return atomic_load_explicit(&g_queue_dropped, memory_order_relaxed);
}

typedef struct {
  uint32_t wpos;
  double at;
} rate_mark_t;

static rate_mark_t g_rate_marks[TS_PUSH_MAX_SUBS];

static int64_t queue_ms(rate_mark_t *m, uint32_t wpos, uint64_t used, double now) {
  int64_t ms = -1;
  if (m->at > 0.0 && now > m->at && (wpos - m->wpos) > 0) ms = (int64_t)((double)used * (now - m->at) * 1000.0 / (double)(wpos - m->wpos));
  m->wpos = wpos;
  m->at = now;
  return ms;
}

static const byte_ring_t *sub_ring(const ts_sub_t *s) {
  if (s->h.proto == CONN_PROTO_H2) return &s->h2_ring;
  if (s->h.proto == CONN_PROTO_H3) return &s->h3_ring;
  return &s->pkt_ring;
}

void ts_push_queue_stats(ts_push_queue_stats_t *out, double now) {
  out->bytes = 0;
  out->max_bytes = 0;
  out->max_ms = 0;
  out->ms_known = 0;
  for (int i = 0; i < g_ts_subs_n; i++) {
    const ts_sub_t *s = &g_ts_subs[i];
    const byte_ring_t *r;
    uint64_t used;
    uint32_t wpos;
    int64_t ms;
    if (atomic_load_explicit(&s->h.alive, memory_order_relaxed) != TS_SUB_ALIVE) {
      g_rate_marks[i].at = 0.0;
      continue;
    }
    r = sub_ring(s);
    wpos = atomic_load_explicit(&r->wpos, memory_order_relaxed);
    used = wpos - atomic_load_explicit(&r->rpos, memory_order_relaxed);
    out->bytes += used;
    if (used > out->max_bytes) out->max_bytes = used;
    ms = queue_ms(&g_rate_marks[i], wpos, used, now);
    if (ms < 0) continue;
    out->ms_known = 1;
    if ((uint64_t)ms > out->max_ms) out->max_ms = (uint64_t)ms;
  }
}

void ts_push_ring_enqueue(ts_sub_t *s, const uint8_t *data, size_t len) {
  if (!len) return;
  if (!byte_ring_write(&s->pkt_ring, data, len)) {
    ts_push_note_enqueue(&s->pkt_ring, 0);
    atomic_store_explicit(&s->pkt_overrun, 1, memory_order_release);
    reactor_wake(s->h.reactor_tid, WAKE_TSPUSH);
    return;
  }
  ts_push_note_enqueue(&s->pkt_ring, 1);
  reactor_wake(s->h.reactor_tid, WAKE_TSPUSH);
}

void ts_push_account_bytes(ts_sub_t *s) {
  uint32_t w = atomic_load_explicit(&sub_ring(s)->wpos, memory_order_relaxed);
  if (w == s->acct_wpos) return;
  ws_clients_add_bytes(s->h.ws_handle, w - s->acct_wpos);
  s->acct_wpos = w;
}

/* first N slots pre-alloc, rest malloc on subscribe */
#define TS_PUSH_PREALLOC_SUBS 32
#define TS_PUSH_SUBS_HEADROOM 16
#define TS_PUSH_SUBS_FLOOR 32

void ts_push_init(int prealloc, int max_clients) {
  int n;
  n = (max_clients > 0 ? max_clients : 0) + TS_PUSH_SUBS_HEADROOM;
  if (n < TS_PUSH_SUBS_FLOOR) n = TS_PUSH_SUBS_FLOOR;
  if (n > TS_PUSH_MAX_SUBS) n = TS_PUSH_MAX_SUBS;
  g_ts_subs = calloc((size_t)n, sizeof *g_ts_subs);
  if (!g_ts_subs) {
    log_code("YS500224", TOOL_NAME ": out of memory sizing ts_push subscriber table (%d entries)", n);
    return;
  }
  g_ts_subs_n = n;
  if (subtab_init(&g_ts_tab, n, g_ts_subs, sizeof *g_ts_subs)) {
    log_code("YS500224", TOOL_NAME ": out of memory sizing ts_push subscriber table (%d entries)", n);
    return;
  }
  if (!prealloc) return;
  for (int i = 0; i < TS_PUSH_PREALLOC_SUBS && i < g_ts_subs_n; i++) {
    byte_ring_reset(&g_ts_subs[i].pkt_ring, TS_RING_PUSH_BYTES);
    byte_ring_reset(&g_ts_subs[i].h2_ring, TS_RING_H2_BYTES);
    byte_ring_reset(&g_ts_subs[i].h3_ring, TS_RING_H3_BYTES);
  }
}

void ts_push_set_reactor_tid(int idx, int tid) { subtab_link(&g_ts_tab, idx, tid); }

int ts_push_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, conn_proto_t proto, int fd,
  unsigned pmt_pid, int spts, int rawaudio, const client_info_t *info, const lcevc_select_t *lcevc) {
  psi_t *spts_psi = NULL;
  psi_t *filter_psi = NULL;
  rawaudio_demux_t *rawaudio_demux = NULL;
  ts_sub_t *s;
  int i;
  int ws_handle;
  int lcevc_may_exclude = lcevc->mode == LCEVC_SEL_BASE || lcevc->mode == LCEVC_SEL_N;
  if (spts) {
    spts_psi = psi_new();
    if (!spts_psi) return -1;
    if (pmt_pid) psi_select_pmt_pid(spts_psi, pmt_pid);
  } else if (!rawaudio && (filter->count > 0 || lcevc_may_exclude)) {
    filter_psi = psi_new();
    if (!filter_psi) return -1;
  }

  i = subtab_alloc(&g_ts_tab);
  if (i < 0) goto fail;
  s = &g_ts_subs[i];
  atomic_store_explicit(&s->h.alive, TS_SUB_ALIVE, memory_order_relaxed);

  /* rings never freed: pump reads s->*_ring unlocked off alive,
     free-on-unsubscribe race (UAF). reuse prior occupant's ring if sized for proto, else allocate */
  if (proto == CONN_PROTO_H3) {
    byte_ring_reset(&s->h3_ring, TS_RING_H3_BYTES);
    if (!s->h3_ring.buf) goto fail_slot;
  } else if (proto == CONN_PROTO_H2) {
    byte_ring_reset(&s->h2_ring, TS_RING_H2_BYTES);
    if (!s->h2_ring.buf) goto fail_slot;
  } else {
    byte_ring_reset(&s->pkt_ring, TS_RING_PUSH_BYTES);
    if (!s->pkt_ring.buf) goto fail_slot;
  }

  if (rawaudio) {
    rawaudio_demux = rawaudio_demux_new(pmt_pid, ts_push_pid_excluded, &s->filter_mask, ts_push_rawaudio_emit, s);
    if (!rawaudio_demux) goto fail_slot;
  }
  ws_handle = ws_clients_add_persistent(info);
  if (ws_handle < 0) goto fail_slot;
  s->h.ws_handle = ws_handle;
  s->acct_wpos = 0;
  atomic_store_explicit(&s->h.cap_ctx, ctx, memory_order_relaxed);
  s->filter = *filter;
  pid_mask_from_filter(&s->filter_mask, &s->filter);
  s->h.proto = proto;
  s->h.fd = fd;
  s->h.conn_gen = proto == CONN_PROTO_H1 ? conn_gen_for_fd(fd) : 0;
  atomic_store_explicit(&s->ready, 0, memory_order_relaxed);
  s->h2c = NULL;
  s->h2_slot = NULL;
  s->h3c = NULL;
  s->h3_sid = -1;
  atomic_store_explicit(&s->pkt_overrun, 0, memory_order_relaxed);
  s->spts = spts;
  s->spts_pmt_pid = pmt_pid;
  s->spts_psi = spts_psi;
  s->spts_locked = 0;
  s->spts_n_allowed = 0;
  s->filter_psi = filter_psi;
  s->cc_pmt = 0;
  s->cc_pat = 0;
  s->filter_base = *filter;
  s->spts_pmt_seen = 0;
  s->lcevc = *lcevc;
  s->lcevc_locked = 0;
  s->rawaudio = rawaudio_demux;
  atomic_store_explicit(&s->ctype_ready, 0, memory_order_relaxed);
  s->audio_codec = CODEC_NONE;
  s->head_sent = 0;
  pthread_mutex_lock(&g_chain_mtx);
  subtab_chain_link(&g_ts_tab, capture_ts_push_head_ptr(ctx), i);
  pthread_mutex_unlock(&g_chain_mtx);
  atomic_fetch_add_explicit(&g_ts_active_count, 1, memory_order_relaxed);
  return i;

fail_slot:
  atomic_store_explicit(&s->h.alive, TS_SUB_FREE, memory_order_release);
  subtab_free(&g_ts_tab, i);
fail:
  psi_free(spts_psi);
  psi_free(filter_psi);
  rawaudio_demux_free(rawaudio_demux);
  return -1;
}

/* ctx not yet capture_close()'d (might free) */
static void ts_push_unlink_ctx(capture_ctx_t *ctx, int idx) {
  pthread_mutex_lock(&g_chain_mtx);
  subtab_chain_unlink(&g_ts_tab, capture_ts_push_head_ptr(ctx), idx);
  pthread_mutex_unlock(&g_chain_mtx);
}

static void ts_push_finish_unsub(void *arg) {
  int idx = (int)(intptr_t)arg;
  ts_sub_t *s = &g_ts_subs[idx];
  capture_ctx_t *ctx;
  subtab_unlink(&g_ts_tab, idx);
  ws_clients_remove(s->h.ws_handle);
  ctx = atomic_load_explicit(&s->h.cap_ctx, memory_order_relaxed);
  capture_close(ctx);
  atomic_store_explicit(&s->h.cap_ctx, NULL, memory_order_relaxed);
  /* h3_ring/h2_ring/pkt_ring kept, not freed: pump reads unlocked. reused by next occupant in ts_push_subscribe() */
  psi_free(s->spts_psi);
  s->spts_psi = NULL;
  psi_free(s->filter_psi);
  s->filter_psi = NULL;
  rawaudio_demux_free(s->rawaudio);
  s->rawaudio = NULL;
  atomic_store_explicit(&s->h.alive, TS_SUB_FREE, memory_order_release);
  subtab_free(&g_ts_tab, idx);
}

void ts_push_unsubscribe_by_idx(int idx) {
  ts_sub_t *s;
  int expected;
  if (idx < 0 || idx >= g_ts_subs_n) return;
  s = &g_ts_subs[idx];
  expected = TS_SUB_ALIVE;
  if (!atomic_compare_exchange_strong_explicit(&s->h.alive, &expected, TS_SUB_CLOSING, memory_order_acquire, memory_order_relaxed))
    return; /* already closing or already closed */
  atomic_fetch_sub_explicit(&g_ts_active_count, 1, memory_order_relaxed);
  ts_push_unlink_ctx(atomic_load_explicit(&s->h.cap_ctx, memory_order_relaxed), idx);
  if (!capture_defer_after_quiescent(ts_push_finish_unsub, (void *)(intptr_t)idx)) {
    capture_wait_pumps_quiescent();
    ts_push_finish_unsub((void *)(intptr_t)idx);
  }
}
