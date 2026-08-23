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

#endif
