/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "subtab.h"

int subtab_init(subtab_t *t, int n, void *slots, size_t stride) {
  t->slots = slots;
  t->stride = stride;
  for (int i = 0; i < REACTOR_MAX_WORKERS; i++) t->head[i] = -1;
  if (idxstack_init(&t->free, n)) return -1;
  for (int i = n - 1; i >= 0; i--) {
    push_sub_t *h = subtab_hdr(t, i);
    atomic_init(&h->reactor_tid, -1);
    h->tid_next = -1;
    atomic_init(&h->chain_next, -1);
    idxstack_push(&t->free, i);
  }
  return 0;
}

int subtab_alloc(subtab_t *t) {
  int idx = t->slots ? idxstack_pop(&t->free) : -1;
  if (idx >= 0) {
    push_sub_t *h = subtab_hdr(t, idx);
    atomic_store_explicit(&h->reactor_tid, -1, memory_order_relaxed);
    h->tid_next = -1;
  }
  return idx;
}

void subtab_free(subtab_t *t, int idx) { idxstack_push(&t->free, idx); }

void subtab_link(subtab_t *t, int idx, int tid) {
  push_sub_t *h;
  if (tid < 0 || tid >= REACTOR_MAX_WORKERS) return;
  h = subtab_hdr(t, idx);
  atomic_store_explicit(&h->reactor_tid, tid, memory_order_release);
  h->tid_next = t->head[tid];
  t->head[tid] = idx;
}

void subtab_unlink(subtab_t *t, int idx) {
  int tid = atomic_load_explicit(&subtab_hdr(t, idx)->reactor_tid, memory_order_relaxed);
  if (tid < 0 || tid >= REACTOR_MAX_WORKERS) return;
  for (int *pp = &t->head[tid]; *pp != -1; pp = &subtab_hdr(t, *pp)->tid_next) {
    if (*pp == idx) {
      *pp = subtab_hdr(t, idx)->tid_next;
      return;
    }
  }
}

void subtab_chain_link(const subtab_t *t, _Atomic int *head, int idx) {
  atomic_store_explicit(&subtab_hdr(t, idx)->chain_next, atomic_load_explicit(head, memory_order_relaxed), memory_order_relaxed);
  atomic_store_explicit(head, idx, memory_order_release);
}

void subtab_chain_unlink(const subtab_t *t, _Atomic int *head, int idx) {
  int next = atomic_load_explicit(&subtab_hdr(t, idx)->chain_next, memory_order_relaxed);
  for (_Atomic int *pp = head;;) {
    int cur = atomic_load_explicit(pp, memory_order_relaxed);
    if (cur == -1) return;
    if (cur == idx) {
      atomic_store_explicit(pp, next, memory_order_release);
      return;
    }
    pp = &subtab_hdr(t, cur)->chain_next;
  }
}
