/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../reactor/qsbr.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* pump-thread only: worker self-block risks deadlock */
void snap_retire(hls_store_t *s, hls_snapshot_t *old) {
  if (!old) return;
  for (;;) {
    for (int i = 0; i < s->retiring_n; ) {
      if (qsbr_mark_passed(g_segstore_qsbr, s->retiring_mark[i])) {
        snap_free(s->retiring[i]);
        s->retiring[i] = s->retiring[--s->retiring_n];
        memcpy(s->retiring_mark[i], s->retiring_mark[s->retiring_n], sizeof s->retiring_mark[i]);
      } else i++;
    }
    if (s->retiring_n < HLS_SNAP_RETIRE_DEPTH) break;
    pthread_mutex_unlock(store_lock(s));
    { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    pthread_mutex_lock(store_lock(s));
  }

  qsbr_mark(g_segstore_qsbr, s->retiring_mark[s->retiring_n]);
  s->retiring[s->retiring_n] = old;
  s->retiring_n++;
}

static _Atomic(slot_retire_node_t *) g_slot_retire_head;

void slot_retire_push(slot_retire_node_t *node) {
  slot_retire_node_t *old_head = atomic_load_explicit(&g_slot_retire_head, memory_order_relaxed);
  for (;;) {
    atomic_store_explicit(&node->next, old_head, memory_order_relaxed);
    if (atomic_compare_exchange_weak_explicit(&g_slot_retire_head, &old_head, node, memory_order_release, memory_order_relaxed)) break;
  }
}

void hls_store_slot_reclaim_sweep(void) {
  slot_retire_node_t *chain = atomic_exchange_explicit(&g_slot_retire_head, NULL, memory_order_acquire);
  slot_retire_node_t *keep_head = NULL;
  slot_retire_node_t *keep_tail = NULL;
  while (chain) {
    slot_retire_node_t *next = atomic_load_explicit(&chain->next, memory_order_relaxed);
    if (!chain->mark || qsbr_mark_passed(g_segstore_qsbr, chain->mark)) {
      for (int i = 0; i < chain->nsnaps; i++) snap_free(chain->snaps[i]);
      if (chain->idx >= 0) atomic_store_explicit(&g_slot_state[chain->idx], STORE_FREE, memory_order_release);
      free(chain->mark);
      free(chain);
    } else {
      atomic_store_explicit(&chain->next, keep_head, memory_order_relaxed);
      keep_head = chain;
      if (!keep_tail) keep_tail = chain;
    }
    chain = next;
  }
  if (keep_head) {
    slot_retire_node_t *old_head = atomic_load_explicit(&g_slot_retire_head, memory_order_relaxed);
    for (;;) {
      atomic_store_explicit(&keep_tail->next, old_head, memory_order_relaxed);
      if (atomic_compare_exchange_weak_explicit(&g_slot_retire_head, &old_head, keep_head, memory_order_release, memory_order_relaxed)) break;
    }
  }
}

/* non-blocking, safe for a reactor worker thread to call */
void snap_retire_async(hls_snapshot_t *snap) {
  slot_retire_node_t *node;
  int nw;
  if (!snap) return;
  node = malloc(sizeof *node);
  if (!node) {
    snap_free(snap);
    return;
  }
  node->idx = -1;
  node->nsnaps = 1;
  node->snaps[0] = snap;
  nw = qsbr_worker_count(g_segstore_qsbr);
  nw = nw > 0 ? nw : 1;
  node->mark = calloc((size_t)nw, sizeof *node->mark);
  if (node->mark) qsbr_mark(g_segstore_qsbr, node->mark);
  slot_retire_push(node);
}

/* caller holds store_lock(s), non-blocking */
void snap_drain_all_async(hls_store_t *s) {
  hls_snapshot_t *cur = atomic_exchange_explicit(&s->snap, NULL, memory_order_acq_rel);
  snap_retire_async(cur);
  for (int i = 0; i < s->retiring_n; i++) snap_retire_async(s->retiring[i]);
  s->retiring_n = 0;
}
