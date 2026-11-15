/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TS_STREAMKEY_H
#define DIPIXY_TS_STREAMKEY_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include "capture/capture.h"
#include "lcevcselect.h"
#include "pidfilter.h"

typedef enum { SEG_CONTAINER_TS, SEG_CONTAINER_FMP4, SEG_CONTAINER_FMP4_VIDEO, SEG_CONTAINER_FMP4_AUDIO } seg_container_t;

/* borrowed: filter + lcevc outlive call */
typedef struct {
  capture_ctx_t *ctx;
  const pid_filter_t *filter;
  unsigned pmt_pid;
  const lcevc_select_t *lcevc;
  seg_container_t container;
} stream_key_t;

#define STREAM_KEY_AS(k, c) (&(stream_key_t){(k)->ctx, (k)->filter, (k)->pmt_pid, (k)->lcevc, (c)})

static inline uint32_t stream_key_hash(const stream_key_t *k) {
  uint64_t h = (uint64_t)(uintptr_t)k->ctx * 0x9E3779B97F4A7C15ull;
  h ^= (uint64_t)k->pmt_pid << 8 ^ (uint64_t)k->container;
  for (int i = 0; i < k->filter->count; i++) h = (h ^ k->filter->pids[i]) * 0x100000001B3ull;
  return (uint32_t)(h ^ h >> 29);
}

/* slot chains per hash bucket. readers walk lock-free, writers hold lock.
   unlinked slot keeps its next: caller reuses slot only after readers r done */
typedef struct {
  _Atomic int *head;
  _Atomic int *next;
  uint32_t mask;
  int cap;
  pthread_mutex_t lock;
} stream_key_index_t;

static inline int stream_key_index_init(stream_key_index_t *ix, int cap) {
  uint32_t nb = 16;
  while (nb < (uint32_t)cap * 2u) nb <<= 1;
  ix->head = malloc(nb * sizeof *ix->head);
  ix->next = malloc((size_t)cap * sizeof *ix->next);
  if (!ix->head || !ix->next) {
    free(ix->head);
    free(ix->next);
    ix->head = ix->next = NULL;
    return -1;
  }
  for (uint32_t i = 0; i < nb; i++) atomic_init(&ix->head[i], -1);
  for (int i = 0; i < cap; i++) atomic_init(&ix->next[i], -1);
  ix->mask = nb - 1;
  ix->cap = cap;
  pthread_mutex_init(&ix->lock, NULL);
  return 0;
}

static inline void stream_key_index_insert(stream_key_index_t *ix, int slot, uint32_t h) {
  _Atomic int *b = &ix->head[h & ix->mask];
  pthread_mutex_lock(&ix->lock);
  atomic_store_explicit(&ix->next[slot], atomic_load_explicit(b, memory_order_relaxed), memory_order_relaxed);
  atomic_store_explicit(b, slot, memory_order_release);
  pthread_mutex_unlock(&ix->lock);
}

static inline void stream_key_index_remove(stream_key_index_t *ix, int slot, uint32_t h) {
  _Atomic int *pp = &ix->head[h & ix->mask];
  pthread_mutex_lock(&ix->lock);
  for (int i = atomic_load_explicit(pp, memory_order_relaxed); i >= 0; i = atomic_load_explicit(pp, memory_order_relaxed)) {
    if (i == slot) {
      atomic_store_explicit(pp, atomic_load_explicit(&ix->next[i], memory_order_relaxed), memory_order_release);
      break;
    }
    pp = &ix->next[i];
  }
  pthread_mutex_unlock(&ix->lock);
}

static inline int stream_key_index_first(const stream_key_index_t *ix, uint32_t h) { return ix->head ? atomic_load_explicit(&ix->head[h & ix->mask], memory_order_acquire) : -1; }
static inline int stream_key_index_next(const stream_key_index_t *ix, int slot) { return atomic_load_explicit(&ix->next[slot], memory_order_acquire); }

#endif
