/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "idxstack.h"

int idxstack_init(idxstack_t *st, int cap) {
  atomic_init(&st->head, 0);
  st->next = NULL;
  st->cap = 0;
  if (cap < 1 || cap >= INT32_MAX) return -1;
  st->next = calloc((size_t)cap, sizeof *st->next);
  if (!st->next) return -1;
  st->cap = cap;
  return 0;
}

void idxstack_free(idxstack_t *st) {
  free(st->next);
  st->next = NULL;
  st->cap = 0;
  atomic_store_explicit(&st->head, 0, memory_order_relaxed);
}

void idxstack_push(idxstack_t *st, int idx) {
  uint64_t old = atomic_load_explicit(&st->head, memory_order_relaxed);
  uint64_t nh;
  if (idx < 0 || idx >= st->cap) return;
  do {
    atomic_store_explicit(&st->next[idx], (uint32_t)old, memory_order_relaxed);
    nh = ((old >> 32) + 1) << 32 | (uint64_t)(uint32_t)(idx + 1);
  } while (!atomic_compare_exchange_weak_explicit(&st->head, &old, nh, memory_order_release, memory_order_relaxed));
}

int idxstack_pop(idxstack_t *st) {
  uint64_t old = atomic_load_explicit(&st->head, memory_order_acquire);
  for (;;) {
    uint32_t top = (uint32_t)old;
    uint64_t nh;
    if (!top) return -1;
    nh = ((old >> 32) + 1) << 32 | atomic_load_explicit(&st->next[top - 1], memory_order_relaxed);
    if (atomic_compare_exchange_weak_explicit(&st->head, &old, nh, memory_order_acquire, memory_order_acquire)) return (int)top - 1;
  }
}
