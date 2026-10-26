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
  int old_head;
  pthread_mutex_lock(&g_chain_mtx);
  old_head = atomic_load_explicit(&store->lldash_sub_head, memory_order_relaxed);
  atomic_store_explicit(&g_lldash_subs[idx].store_next, old_head, memory_order_relaxed);
  atomic_store_explicit(&store->lldash_sub_head, idx, memory_order_release);
  pthread_mutex_unlock(&g_chain_mtx);
}

void unlink_store_chain(hls_store_t *store, int idx) {
  const dashchunk_sub_t *s = &g_lldash_subs[idx];
  int next;
  int cur;
  pthread_mutex_lock(&g_chain_mtx);
  next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
  cur = atomic_load_explicit(&store->lldash_sub_head, memory_order_relaxed);
  if (cur == idx) {
    atomic_store_explicit(&store->lldash_sub_head, next, memory_order_release);
    pthread_mutex_unlock(&g_chain_mtx);
    return;
  }
  while (cur != -1) {
    dashchunk_sub_t *cs = &g_lldash_subs[cur];
    int cs_next = atomic_load_explicit(&cs->store_next, memory_order_relaxed);
    if (cs_next == idx) {
      atomic_store_explicit(&cs->store_next, next, memory_order_release);
      break;
    }
    cur = cs_next;
  }
  pthread_mutex_unlock(&g_chain_mtx);
}

void link_tid_chain(int idx, int tid) {
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  g_lldash_subs[idx].tid_next = g_lldash_tid_head[tid];
  g_lldash_tid_head[tid] = idx;
}

void unlink_tid_chain(int idx) {
  const dashchunk_sub_t *s = &g_lldash_subs[idx];
  int tid = s->reactor_tid;
  int cur;
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  if (g_lldash_tid_head[tid] == idx) {
    g_lldash_tid_head[tid] = s->tid_next;
    return;
  }
  cur = g_lldash_tid_head[tid];
  while (cur != -1) {
    dashchunk_sub_t *cs = &g_lldash_subs[cur];
    if (cs->tid_next == idx) {
      cs->tid_next = s->tid_next;
      return;
    }
    cur = cs->tid_next;
  }
}

void on_part_pushed(const hls_store_t *store, uint32_t seq, int part_idx, const uint8_t *data, size_t len) {
  int i;
  if (store->container == SEG_CONTAINER_TS) return;
  i = atomic_load_explicit(&store->lldash_sub_head, memory_order_acquire);
  while (i != -1) {
    dashchunk_sub_t *s = &g_lldash_subs[i];
    int next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE && sub_matches(s, store, seq)) {
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
    int next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE && sub_matches(s, store, seq)) {
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
    int next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE) {
      pthread_mutex_lock(&s->lock);
      if (!s->attached) s->aborted = 1;
      else if (s->proto == CONN_PROTO_H1) sub_end(s);
      else sub_abort(s);
      pthread_mutex_unlock(&s->lock);
    }
    i = next;
  }
}
