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
static stream_key_index_t g_index;

qsbr_domain_t *g_segstore_qsbr;

void hls_store_set_qsbr(qsbr_domain_t *d) { g_segstore_qsbr = d; }

void hls_store_init(int max_channels) {
  int n = max_channels > 0 ? max_channels : 1;
  if (n > HLS_MAX_STORES * HLS_STORES_PER_SEG) n = HLS_MAX_STORES * HLS_STORES_PER_SEG;
  g_stores = calloc((size_t)n, sizeof *g_stores);
  g_store_locks = calloc((size_t)n, sizeof *g_store_locks);
  g_slot_state = calloc((size_t)n, sizeof *g_slot_state);
  if (!g_stores || !g_store_locks || !g_slot_state) {
    log_code("YS500221", TOOL_NAME ": out of memory sizing store table (%d entries)", n);
    free(g_stores);
    free(g_store_locks);
    free(g_slot_state);
    g_stores = NULL;
    g_store_locks = NULL;
    g_slot_state = NULL;
    return;
  }
  if (stream_key_index_init(&g_index, n)) {
    log_code("YS500221", TOOL_NAME ": out of memory sizing store table (%d entries)", n);
    return;
  }
  slot_retire_pool_init(n);
  g_stores_n = n;
}

static stream_key_t store_key(hls_store_t *s) { return (stream_key_t){s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, s->container}; }

pthread_mutex_t *store_lock(const hls_store_t *s) { return &g_store_locks[s - g_stores]; }
static _Atomic int *slot_state(const hls_store_t *s) { return &g_slot_state[s - g_stores]; }

hls_store_t *hls_store_find(const stream_key_t *k) {
  for (int i = stream_key_index_first(&g_index, stream_key_hash(k)); i >= 0; i = stream_key_index_next(&g_index, i)) {
    if (atomic_load_explicit(&g_slot_state[i], memory_order_acquire) != STORE_OPEN) continue;
    if (g_stores[i].cap_ctx == k->ctx && g_stores[i].pmt_pid == k->pmt_pid && g_stores[i].container == k->container &&
        pid_filter_equal(&g_stores[i].filter, k->filter) && lcevc_select_equal(&g_stores[i].lcevc, k->lcevc))
      return &g_stores[i];
  }
  return NULL;
}

void hls_set_store_closing_cb(hls_store_closing_cb cb) { g_store_closing_cb = cb; }

void hls_store_open(const stream_key_t *k, double seg_target, int max_segs) {
  hls_store_t *s;
  int expected;
  int fresh = 0;
  if (max_segs < 2) max_segs = 2;
  if (max_segs > HLS_MAX_SEGS) max_segs = HLS_MAX_SEGS;

  for (;;) {
    s = hls_store_find(k);
    if (s) {
      expected = STORE_OPEN;
      if (atomic_compare_exchange_strong_explicit(slot_state(s), &expected, STORE_OPENING, memory_order_acq_rel, memory_order_relaxed)) break;
      continue;
    }
    for (int i = 0; i < g_stores_n; i++) {
      expected = STORE_FREE;
      if (atomic_compare_exchange_strong_explicit(&g_slot_state[i], &expected, STORE_OPENING, memory_order_acq_rel, memory_order_relaxed)) {
        s = &g_stores[i];
        fresh = 1;
        break;
      }
    }
    break;
  }
  if (!s) return;
  pthread_mutex_lock(store_lock(s));
  snap_drain_all_async(s);
  if (fresh) {
    s->cap_ctx = k->ctx;
    s->filter = *k->filter;
    s->pmt_pid = k->pmt_pid;
    s->lcevc = *k->lcevc;
    s->container = k->container;
    stream_key_index_insert(&g_index, (int)(s - g_stores), stream_key_hash(k));
  }
  s->seg_target = seg_target;
  s->max_segs = max_segs;
  s->opened_at = time(NULL);
  atomic_init(&s->lldash_sub_head, -1);
  atomic_store_explicit(slot_state(s), STORE_OPEN, memory_order_release);
  pthread_mutex_unlock(store_lock(s));
}

void hls_store_close(const stream_key_t *k) {
  hls_store_t *s = hls_store_find(k);
  int expected = STORE_OPEN;
  slot_retire_node_t *node;
  hls_snapshot_t *cur;
  stream_key_t sk;
  if (!s) return;
  if (!atomic_compare_exchange_strong_explicit(slot_state(s), &expected, STORE_CLOSING, memory_order_acq_rel, memory_order_relaxed)) return;
  node = slot_retire_node_get();
  if (!node) {
    log_code("YE500222", TOOL_NAME ": hls: close retire alloc failed, store stays open");
    atomic_store_explicit(slot_state(s), STORE_OPEN, memory_order_release);
    return;
  }
  node->idx = (int)(s - g_stores);
  node->nsnaps = 0;
  sk = store_key(s);
  stream_key_index_remove(&g_index, node->idx, stream_key_hash(&sk));

  pthread_mutex_lock(store_lock(s));
  cur = atomic_exchange_explicit(&s->snap, NULL, memory_order_acq_rel);
  if (cur) {
    seq_floor_note(cur->next_seq);
    node->snaps[node->nsnaps++] = cur;
  }
  for (int i = 0; i < s->retiring_n; i++) node->snaps[node->nsnaps++] = s->retiring[i];
  s->retiring_n = 0;
  free(s->spare);
  s->spare = NULL;
  pthread_mutex_unlock(store_lock(s));

  qsbr_mark(g_segstore_qsbr, node->mark);
  slot_retire_push(node);

  if (g_store_closing_cb) g_store_closing_cb(s);
}
