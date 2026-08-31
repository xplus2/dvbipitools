/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* ts_push.c - HTTP MPEG-TS push: subscribers match by capture_ctx_t*,
   no ticket/username auth, no PCR pacing (multicast source already arrives in real time) */

#include "ts_push_int.h"
#include "../reactor/conn.h"
#include "../version.h"

#include "lib/helper/log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ts_sub_t *g_ts_subs;
int g_ts_subs_n;

static int g_ts_push_reactor_efds[TS_PUSH_MAX_REACTOR_THREADS];

/* flush_ready(tid)'s subscriber chain. slot T touched only by thread T:
   every begin/close path for subscription runs on its own owning thread (H1/H2/H3 don't migrate threads) */
int g_tid_head[TS_PUSH_MAX_REACTOR_THREADS];

void ts_push_wake_reactor(int tid) {
  int efd;
  if (tid < 0 || tid >= TS_PUSH_MAX_REACTOR_THREADS) return;
  efd = g_ts_push_reactor_efds[tid];
  if (efd >= 0) {
    uint64_t one = 1;
    ssize_t r = write(efd, &one, sizeof one);
    (void)r;
  }
}

void ts_push_ring_enqueue(ts_sub_t *s, const uint8_t *data, size_t len) {
  uint32_t wpos, rpos, idx;
  size_t first;
  if (!s->pkt_ring || !len)
    return;
  wpos = atomic_load_explicit(&s->pkt_wpos, memory_order_relaxed);
  rpos = atomic_load_explicit(&s->pkt_rpos, memory_order_acquire);
  if (len > TS_RING_PUSH_BYTES - (wpos - rpos)) {
    atomic_store_explicit(&s->pkt_overrun, 1, memory_order_release);
    ts_push_wake_reactor(s->reactor_tid);
    return;
  }
  idx = wpos & (TS_RING_PUSH_BYTES - 1u);
  first = len;
  if (idx + first > TS_RING_PUSH_BYTES)
    first = TS_RING_PUSH_BYTES - idx;
  memcpy(s->pkt_ring + idx, data, first);
  if (first < len)
    memcpy(s->pkt_ring, data + first, len - first);
  atomic_store_explicit(&s->pkt_wpos, wpos + (uint32_t)len, memory_order_release);
  ws_clients_add_bytes(s->ws_handle, len);
  ts_push_wake_reactor(s->reactor_tid);
}

/* first N slots pre-alloc, rest malloc on subscribe */
#define TS_PUSH_PREALLOC_SUBS 32
#define TS_PUSH_SUBS_HEADROOM 16
#define TS_PUSH_SUBS_FLOOR 32

void ts_push_init(int prealloc, int max_clients) {
  int i, n;

  n = (max_clients > 0 ? max_clients : 0) + TS_PUSH_SUBS_HEADROOM;
  if (n < TS_PUSH_SUBS_FLOOR)
    n = TS_PUSH_SUBS_FLOOR;
  if (n > TS_PUSH_MAX_SUBS)
    n = TS_PUSH_MAX_SUBS;
  g_ts_subs = calloc((size_t)n, sizeof *g_ts_subs);
  if (!g_ts_subs) {
    log_line(TOOL_NAME ": out of memory sizing ts_push subscriber table (%d entries)", n);
    return;
  }
  g_ts_subs_n = n;

  for (i = 0; i < TS_PUSH_MAX_REACTOR_THREADS; i++) {
    g_ts_push_reactor_efds[i] = -1;
    g_tid_head[i] = -1;
  }
  if (!prealloc)
    return;
  for (i = 0; i < TS_PUSH_PREALLOC_SUBS && i < g_ts_subs_n; i++) {
    g_ts_subs[i].pkt_ring = malloc((size_t)TS_RING_PUSH_BYTES);
    g_ts_subs[i].h2_ring = malloc((size_t)TS_RING_H2_BYTES);
    g_ts_subs[i].h3_ring = malloc((size_t)TS_RING_H3_BYTES);
  }
}

void ts_push_set_reactor_tid(int idx, int tid) {
  ts_sub_t *s = &g_ts_subs[idx];
  s->reactor_tid = tid;
  s->tid_next = g_tid_head[tid];
  g_tid_head[tid] = idx;
}

void ts_push_register_reactor_efd(int tid, int efd) {
  if (tid >= 0 && tid < TS_PUSH_MAX_REACTOR_THREADS)
    g_ts_push_reactor_efds[tid] = efd;
}

int ts_push_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, int proto, int fd, unsigned pmt_pid, int spts,
                       int rawaudio, const client_info_t *info) {
  psi_t *spts_psi = NULL;
  psi_t *filter_psi = NULL;
  rawaudio_demux_t *rawaudio_demux = NULL;
  int i, ws_handle;
  if (spts) {
    spts_psi = psi_new();
    if (!spts_psi) return -1;
    if (pmt_pid)
      psi_select_pmt_pid(spts_psi, pmt_pid);
  } else if (!rawaudio && filter->count > 0) {
    filter_psi = psi_new();
    if (!filter_psi) return -1;
  }

  for (;;) {
    ts_sub_t *s;
    uint8_t *ring;
    int expected;
    for (i = 0; i < g_ts_subs_n; i++) {
      if (atomic_load_explicit(&g_ts_subs[i].alive, memory_order_relaxed) != TS_SUB_FREE)
        continue;
      expected = TS_SUB_FREE;
      if (atomic_compare_exchange_strong_explicit(&g_ts_subs[i].alive, &expected, TS_SUB_ALIVE, memory_order_acquire, memory_order_relaxed))
        break;
    }
    if (i == g_ts_subs_n) break; /* table full */
    s = &g_ts_subs[i];

    /* rings never freed: pump reads s->*_ring unlocked off alive,
       free-on-unsubscribe races it (UAF). reuse prior occupant's ring
       if sized for proto, else allocate once */
    if (proto == 3) {
      ring = s->h3_ring;
      if (!ring) {
        ring = malloc((size_t)TS_RING_H3_BYTES);
        if (!ring) {
          atomic_store_explicit(&s->alive, TS_SUB_FREE, memory_order_release);
          psi_free(spts_psi);
          psi_free(filter_psi);
          return -1;
        }
        s->h3_ring = ring;
      }
    } else if (proto == 2) {
      ring = s->h2_ring;
      if (!ring) {
        ring = malloc((size_t)TS_RING_H2_BYTES);
        if (!ring) {
          atomic_store_explicit(&s->alive, TS_SUB_FREE, memory_order_release);
          psi_free(spts_psi);
          psi_free(filter_psi);
          return -1;
        }
        s->h2_ring = ring;
      }
    } else {
      ring = s->pkt_ring;
      if (!ring) {
        ring = malloc((size_t)TS_RING_PUSH_BYTES);
        if (!ring) {
          atomic_store_explicit(&s->alive, TS_SUB_FREE, memory_order_release);
          psi_free(spts_psi);
          psi_free(filter_psi);
          return -1;
        }
        s->pkt_ring = ring;
      }
    }

    if (rawaudio) {
      rawaudio_demux = rawaudio_demux_new(pmt_pid, filter, ts_push_rawaudio_emit, s);
      if (!rawaudio_demux) {
        atomic_store_explicit(&s->alive, TS_SUB_FREE, memory_order_release);
        psi_free(spts_psi);
        psi_free(filter_psi);
        return -1;
      }
    }
    ws_handle = ws_clients_add_persistent(info);
    if (ws_handle < 0) {
      atomic_store_explicit(&s->alive, TS_SUB_FREE, memory_order_release);
      psi_free(spts_psi);
      psi_free(filter_psi);
      rawaudio_demux_free(rawaudio_demux);
      return -1;
    }
    s->ws_handle = ws_handle;
    atomic_store_explicit(&s->ctx, ctx, memory_order_relaxed);
    s->filter = *filter;
    s->proto = proto;
    s->fd = fd;
    atomic_store_explicit(&s->ready, 0, memory_order_relaxed);
    s->h2_sid = -1;
    s->reactor_tid = -1;
    s->tid_next = -1;
    s->h3c = NULL;
    s->h3_sid = -1;
    atomic_store_explicit(&s->h3_wpos, 0, memory_order_relaxed);
    atomic_store_explicit(&s->h3_rpos, 0, memory_order_relaxed);
    atomic_store_explicit(&s->h2_wpos, 0, memory_order_relaxed);
    atomic_store_explicit(&s->h2_rpos, 0, memory_order_relaxed);
    atomic_store_explicit(&s->pkt_wpos, 0, memory_order_relaxed);
    atomic_store_explicit(&s->pkt_rpos, 0, memory_order_relaxed);
    atomic_store_explicit(&s->pkt_overrun, 0, memory_order_relaxed);
    s->spts = spts;
    s->spts_pmt_pid = pmt_pid;
    s->spts_psi = spts_psi;
    s->spts_locked = 0;
    s->spts_n_allowed = 0;
    s->filter_psi = filter_psi;
    s->cc_pmt = 0;
    s->rawaudio = rawaudio_demux;
    {
      _Atomic int *head = capture_ts_push_head_ptr(ctx);
      int old_head = atomic_load_explicit(head, memory_order_acquire);
      atomic_store_explicit(&s->ctx_next, old_head, memory_order_relaxed);
      atomic_store_explicit(head, i, memory_order_release);
    }
    return i;
  }
  psi_free(spts_psi);
  psi_free(filter_psi);
  rawaudio_demux_free(rawaudio_demux);
  return -1;
}

/* removes idx from g_tid_head[s->reactor_tid]'s chain. owning thread only,
   no-op if idx was never linked (reactor_tid still -1) */
static void ts_push_unlink_tid(int idx) {
  ts_sub_t *s = &g_ts_subs[idx];
  int tid = s->reactor_tid;
  int cur;
  if (tid < 0 || tid >= TS_PUSH_MAX_REACTOR_THREADS)
    return;
  if (g_tid_head[tid] == idx) {
    g_tid_head[tid] = s->tid_next;
    return;
  }
  cur = g_tid_head[tid];
  while (cur != -1) {
    ts_sub_t *cs = &g_ts_subs[cur];
    if (cs->tid_next == idx) {
      cs->tid_next = s->tid_next;
      return;
    }
    cur = cs->tid_next;
  }
}

/* removes idx from ctx's feed_pkt chain. caller: pump quiescent, ctx not
   yet capture_close()'d (may free it) */
static void ts_push_unlink_ctx(capture_ctx_t *ctx, int idx) {
  _Atomic int *head = capture_ts_push_head_ptr(ctx);
  ts_sub_t *s = &g_ts_subs[idx];
  int next = atomic_load_explicit(&s->ctx_next, memory_order_relaxed);
  int cur = atomic_load_explicit(head, memory_order_relaxed);
  if (cur == idx) {
    atomic_store_explicit(head, next, memory_order_relaxed);
    return;
  }
  while (cur != -1) {
    ts_sub_t *cs = &g_ts_subs[cur];
    int cs_next = atomic_load_explicit(&cs->ctx_next, memory_order_relaxed);
    if (cs_next == idx) {
      atomic_store_explicit(&cs->ctx_next, next, memory_order_relaxed);
      return;
    }
    cur = cs_next;
  }
}

void ts_push_unsubscribe_by_idx(int idx) {
  ts_sub_t *s;
  capture_ctx_t *ctx;
  int expected;
  if (idx < 0 || idx >= g_ts_subs_n)
    return;
  s = &g_ts_subs[idx];
  expected = TS_SUB_ALIVE;
  if (!atomic_compare_exchange_strong_explicit(&s->alive, &expected, TS_SUB_CLOSING, memory_order_acquire,
                                                memory_order_relaxed))
    return; /* already closing or already closed */
  capture_wait_pumps_quiescent();
  ts_push_unlink_tid(idx);
  ws_clients_remove(s->ws_handle);
  ctx = atomic_load_explicit(&s->ctx, memory_order_relaxed);
  ts_push_unlink_ctx(ctx, idx);
  capture_close(ctx);
  atomic_store_explicit(&s->ctx, NULL, memory_order_relaxed);
  /* h3_ring/h2_ring/pkt_ring kept, not freed: pump reads unlocked. reused by next occupant in ts_push_subscribe() */
  psi_free(s->spts_psi);
  s->spts_psi = NULL;
  psi_free(s->filter_psi);
  s->filter_psi = NULL;
  rawaudio_demux_free(s->rawaudio);
  s->rawaudio = NULL;
  atomic_store_explicit(&s->alive, TS_SUB_FREE, memory_order_release);
}
