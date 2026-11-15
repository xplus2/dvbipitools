/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/dash/lldash/lldash_int.h"

static pthread_mutex_t g_chain_mtx = PTHREAD_MUTEX_INITIALIZER; /* store chain writers. pumps walk lock-free */

void link_store_chain(hls_store_t *store, int idx) {
  pthread_mutex_lock(&g_chain_mtx);
  subtab_chain_link(&g_lldash_tab, &store->lldash_sub_head, idx);
  pthread_mutex_unlock(&g_chain_mtx);
}

void unlink_store_chain(hls_store_t *store, int idx) {
  pthread_mutex_lock(&g_chain_mtx);
  subtab_chain_unlink(&g_lldash_tab, &store->lldash_sub_head, idx);
  pthread_mutex_unlock(&g_chain_mtx);
}

void on_part_pushed(const hls_store_t *store, uint32_t seq, int part_idx, const uint8_t *data, size_t len) {
  int i;
  if (store->container == SEG_CONTAINER_TS) return;
  i = atomic_load_explicit(&store->lldash_sub_head, memory_order_acquire);
  while (i != -1) {
    dashchunk_sub_t *s = &g_lldash_subs[i];
    int next = atomic_load_explicit(&s->h.chain_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->h.alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE && sub_matches(s, store, seq)) {
      pthread_mutex_lock(&s->lock);
      if (s->attached && !s->ended && part_idx == s->sent) {
        sub_emit(s, data, len);
        s->sent++;
      }
      pthread_mutex_unlock(&s->lock);
    }
    i = next;
  }
}

void on_segment_done(const hls_store_t *store, uint32_t seq) {
  int i;
  if (store->container == SEG_CONTAINER_TS) return;
  i = atomic_load_explicit(&store->lldash_sub_head, memory_order_acquire);
  while (i != -1) {
    dashchunk_sub_t *s = &g_lldash_subs[i];
    int next = atomic_load_explicit(&s->h.chain_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->h.alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE && sub_matches(s, store, seq)) {
      pthread_mutex_lock(&s->lock);
      if (s->attached) sub_end(s); /* unattached: catch-up sees segment in ring */
      pthread_mutex_unlock(&s->lock);
    }
    i = next;
  }
}

void on_store_closing(const hls_store_t *store) {
  int i = atomic_load_explicit(&store->lldash_sub_head, memory_order_acquire);
  while (i != -1) {
    dashchunk_sub_t *s = &g_lldash_subs[i];
    int next = atomic_load_explicit(&s->h.chain_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->h.alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE) {
      pthread_mutex_lock(&s->lock);
      if (!s->attached) s->aborted = 1;
      else if (s->h.proto == CONN_PROTO_H1) sub_end(s);
      else sub_abort(s);
      pthread_mutex_unlock(&s->lock);
    }
    i = next;
  }
}
