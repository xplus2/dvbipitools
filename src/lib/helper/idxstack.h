/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_IDXSTACK_H
#define DVBIPITOOLS_LIB_HELPER_IDXSTACK_H

#include <stdatomic.h>
#include <stdint.h>

/* lock-free LIFO of slot indices 0..cap-1. head: gen << 32 | (idx + 1), 0 empty */
typedef struct {
  _Atomic uint64_t head;
  _Atomic uint32_t *next;
  int cap;
} idxstack_t;

/* empty stack. 0 ok, -1 OOM or cap OOR */
int idxstack_init(idxstack_t *st, int cap);
void idxstack_free(idxstack_t *st);
void idxstack_push(idxstack_t *st, int idx);
int idxstack_pop(idxstack_t *st);

#endif
