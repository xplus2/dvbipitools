/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../version.h"
#include "lib/helper/log.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

static hls_store_t *g_stores;
static int g_stores_n;
static pthread_mutex_t *g_store_locks; /* writer-only now, see segstore_int.h */

_Atomic int *g_slot_state; /* kept out of hls_store_t: open() sets fields, no memset */

static hls_store_closing_cb g_store_closing_cb;

qsbr_domain_t *g_segstore_qsbr;

void hls_store_set_qsbr(qsbr_domain_t *d) { g_segstore_qsbr = d; }

void hls_store_init(int max_channels) {
  int n = max_channels > 0 ? max_channels : 1;
  if (n > HLS_MAX_STORES) n = HLS_MAX_STORES;
  g_stores = calloc((size_t)n, sizeof *g_stores);
  g_store_locks = calloc((size_t)n, sizeof *g_store_locks);
  g_slot_state = calloc((size_t)n, sizeof *g_slot_state);
  if (!g_stores || !g_store_locks || !g_slot_state) {
    log_line(TOOL_NAME ": out of memory sizing store table (%d entries)", n);
    free(g_stores);
    free(g_store_locks);
    free(g_slot_state);
    g_stores = NULL;
    g_store_locks = NULL;
    g_slot_state = NULL;
    return;
  }
  g_stores_n = n;
}

pthread_mutex_t *store_lock(const hls_store_t *s) { return &g_store_locks[s - g_stores]; }
static _Atomic int *slot_state(const hls_store_t *s) { return &g_slot_state[s - g_stores]; }

hls_store_t *hls_store_find(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  for (int i = 0; i < g_stores_n; i++) {
    if (atomic_load_explicit(&g_slot_state[i], memory_order_acquire) != STORE_OPEN) continue;
    if (g_stores[i].cap_ctx == ctx && g_stores[i].pmt_pid == pmt_pid && g_stores[i].container == container &&
        pid_filter_equal(&g_stores[i].filter, filter) && lcevc_select_equal(&g_stores[i].lcevc, lcevc))
      return &g_stores[i];
  }
  return NULL;
}

void hls_set_store_closing_cb(hls_store_closing_cb cb) { g_store_closing_cb = cb; }

void hls_store_open(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, double seg_target, int max_segs, seg_container_t container) {
  hls_store_t *s;
  int expected;
  if (max_segs < 2) max_segs = 2;
  if (max_segs > HLS_MAX_SEGS) max_segs = HLS_MAX_SEGS;

  for (;;) {
    s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
    if (s) {
      expected = STORE_OPEN;
      if (atomic_compare_exchange_strong_explicit(slot_state(s), &expected, STORE_OPENING, memory_order_acq_rel, memory_order_relaxed)) break;
      continue;
    }
    for (int i = 0; i < g_stores_n; i++) {
      expected = STORE_FREE;
      if (atomic_compare_exchange_strong_explicit(&g_slot_state[i], &expected, STORE_OPENING, memory_order_acq_rel, memory_order_relaxed)) {
        s = &g_stores[i];
        break;
      }
    }
    break;
  }
  if (!s) return;
  pthread_mutex_lock(store_lock(s));
  snap_drain_all_async(s);
  s->cap_ctx = ctx;
  s->filter = *filter;
  s->pmt_pid = pmt_pid;
  s->lcevc = *lcevc;
  s->seg_target = seg_target;
  s->max_segs = max_segs;
  s->container = container;
  s->opened_at = time(NULL);
  atomic_init(&s->lldash_sub_head, -1);
  atomic_store_explicit(slot_state(s), STORE_OPEN, memory_order_release);
  pthread_mutex_unlock(store_lock(s));
}

void hls_store_close(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  int expected = STORE_OPEN;
  slot_retire_node_t *node;
  int nw;
  if (!s) return;
  if (!atomic_compare_exchange_strong_explicit(slot_state(s), &expected, STORE_CLOSING, memory_order_acq_rel, memory_order_relaxed)) return;
  node = malloc(sizeof *node);
  if (!node) {
    log_line(TOOL_NAME ": hls: close retire alloc failed, freeing slot without a QSBR wait");
    atomic_store_explicit(slot_state(s), STORE_FREE, memory_order_release);
    return;
  }
  node->idx = (int)(s - g_stores);
  node->nsnaps = 0;

  pthread_mutex_lock(store_lock(s));
  {
    hls_snapshot_t *cur = atomic_exchange_explicit(&s->snap, NULL, memory_order_acq_rel);
    if (cur) node->snaps[node->nsnaps++] = cur;
    for (int i = 0; i < s->retiring_n; i++) node->snaps[node->nsnaps++] = s->retiring[i];
    s->retiring_n = 0;
  }
  pthread_mutex_unlock(store_lock(s));

  nw = qsbr_worker_count(g_segstore_qsbr);
  nw = nw > 0 ? nw : 1;
  node->mark = calloc((size_t)nw, sizeof *node->mark);
  if (node->mark) qsbr_mark(g_segstore_qsbr, node->mark);
  slot_retire_push(node);

  if (g_store_closing_cb) g_store_closing_cb(s);
}
