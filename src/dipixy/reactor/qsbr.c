/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "qsbr.h"

#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

#define QSBR_CACHELINE 64
#define QSBR_ONLINE 1u

typedef struct {
  _Alignas(QSBR_CACHELINE) _Atomic uint64_t v; /* odd: online, even: offline; quiescent adds 2 */
} qsbr_slot_t;

struct qsbr_domain {
  qsbr_slot_t *slot;
  int nworkers;
};

static int slot_passed(const qsbr_slot_t *s, uint64_t mark) {
  return !(mark & QSBR_ONLINE) || atomic_load_explicit(&s->v, memory_order_acquire) != mark;
}

qsbr_domain_t *qsbr_domain_create(int nworkers) {
  qsbr_domain_t *d = calloc(1, sizeof *d);
  if (!d) return NULL;
  if (nworkers < 1) nworkers = 1;
  d->slot = aligned_alloc(QSBR_CACHELINE, (size_t)nworkers * sizeof *d->slot);
  if (!d->slot) {
    free(d);
    return NULL;
  }
  for (int i = 0; i < nworkers; i++) atomic_init(&d->slot[i].v, QSBR_ONLINE);
  d->nworkers = nworkers;
  return d;
}

void qsbr_domain_destroy(qsbr_domain_t *d) {
  if (!d) return;
  free(d->slot);
  free(d);
}

void qsbr_worker_quiescent(qsbr_domain_t *d, int tid) {
  if (!d || tid < 0 || tid >= d->nworkers) return;
  atomic_fetch_add_explicit(&d->slot[tid].v, 2, memory_order_release);
}

void qsbr_worker_offline(qsbr_domain_t *d, int tid) {
  if (!d || tid < 0 || tid >= d->nworkers) return;
  uint64_t v = atomic_load_explicit(&d->slot[tid].v, memory_order_relaxed);
  if (v & QSBR_ONLINE) atomic_store_explicit(&d->slot[tid].v, v + 1, memory_order_release);
}

void qsbr_worker_online(qsbr_domain_t *d, int tid) {
  if (!d || tid < 0 || tid >= d->nworkers) return;
  uint64_t v = atomic_load_explicit(&d->slot[tid].v, memory_order_relaxed);
  if (v & QSBR_ONLINE) return;
  atomic_store_explicit(&d->slot[tid].v, v + 1, memory_order_relaxed);
  /* order store before later shared loads (ldapr, arm64) */
  atomic_thread_fence(memory_order_seq_cst);
}

int qsbr_worker_count(const qsbr_domain_t *d) { return d ? d->nworkers : 0; }

void qsbr_mark(const qsbr_domain_t *d, uint64_t *out) {
  if (!d) return;
  atomic_thread_fence(memory_order_seq_cst);
  for (int i = 0; i < d->nworkers; i++) out[i] = atomic_load_explicit(&d->slot[i].v, memory_order_acquire);
}

int qsbr_mark_passed(const qsbr_domain_t *d, const uint64_t *mark) {
  if (!d) return 1;
  for (int i = 0; i < d->nworkers; i++) {
    if (!slot_passed(&d->slot[i], mark[i])) return 0;
  }
  return 1;
}

/* caller can't tick own epoch while blocked here */
int qsbr_mark_passed_excl(const qsbr_domain_t *d, const uint64_t *mark, int excl) {
  if (!d) return 1;
  for (int i = 0; i < d->nworkers; i++) {
    if (i == excl) continue;
    if (!slot_passed(&d->slot[i], mark[i])) return 0;
  }
  return 1;
}

void qsbr_backoff(int spins) {
  struct timespec ts;
  if (spins < 100) {
    sched_yield();
    return;
  }
  ts.tv_sec = 0;
  ts.tv_nsec = spins < 1000 ? 50000 : 1000000;
  nanosleep(&ts, NULL);
}
