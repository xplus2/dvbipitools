/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_REACTOR_SUBTAB_H
#define DIPIXY_REACTOR_SUBTAB_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "conn.h"
#include "../ts/capture/capture.h"
#include "lib/helper/idxstack.h"

/* 1st member of every push subscriber. tid_next: owning reactor thread only */
typedef struct {
  _Atomic int alive;
  _Atomic(capture_ctx_t *) cap_ctx;
  conn_proto_t proto;
  int fd;
  uint64_t conn_gen;
  _Atomic int reactor_tid;
  int tid_next;
  _Atomic int chain_next;
  int ws_handle;
} push_sub_t;

typedef struct {
  idxstack_t free;
  char *slots;
  size_t stride;
  int head[REACTOR_MAX_WORKERS];
} subtab_t;

int subtab_init(subtab_t *t, int n, void *slots, size_t stride);
int subtab_alloc(subtab_t *t);
void subtab_free(subtab_t *t, int idx);

static inline push_sub_t *subtab_hdr(const subtab_t *t, int idx) { return (push_sub_t *)(t->slots + (size_t)idx * t->stride); }

void subtab_link(subtab_t *t, int idx, int tid);
void subtab_unlink(subtab_t *t, int idx);
static inline int subtab_first(const subtab_t *t, int tid) { return tid >= 0 && tid < REACTOR_MAX_WORKERS ? t->head[tid] : -1; }
static inline int subtab_next(const subtab_t *t, int idx) { return subtab_hdr(t, idx)->tid_next; }

/* caller serializes writers of head. readers walk lock-free, unlinked idx keeps its link */
void subtab_chain_link(const subtab_t *t, _Atomic int *head, int idx);
void subtab_chain_unlink(const subtab_t *t, _Atomic int *head, int idx);

#endif
