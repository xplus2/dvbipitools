/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_SEQLOCK_H
#define DVBIPITOOLS_LIB_SEQLOCK_H

#include <stdatomic.h>

/* seqlock idiom: odd gen = write in progress, readers retry. commit leaves gen even.
   writer-vs-writer exclusion is caller's job, this only orders one writer against lock-free readers. */
static inline unsigned seqlock_begin_write(_Atomic unsigned *gen) {
  unsigned g = atomic_load_explicit(gen, memory_order_relaxed);
  atomic_store_explicit(gen, g + 1, memory_order_release);
  return g;
}

static inline void seqlock_commit_write(_Atomic unsigned *gen, unsigned g) {
  atomic_store_explicit(gen, g + 2, memory_order_release);
}

/* bounded-retry seqlock read loop, odd gen skipped, mismatch after body retried.
   fall-through after N tries: caller treats as not-found */
#define SEQLOCK_READ_TRIES 8

#define SEQLOCK_READ_LOOP(gen_ptr, g_var) \
  for (int _sl_i = 0; _sl_i < SEQLOCK_READ_TRIES; _sl_i++) \
    if (((g_var) = atomic_load_explicit((gen_ptr), memory_order_acquire)) & 1u) \
      continue; \
    else

#define SEQLOCK_READ_OK(gen_ptr, g_var) ((g_var) == atomic_load_explicit((gen_ptr), memory_order_acquire))

#endif
