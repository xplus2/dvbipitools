/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "qsbr.h"

#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

struct qsbr_domain {
  _Atomic uint64_t *epoch;
  int nworkers;
};

qsbr_domain_t *qsbr_domain_create(int nworkers) {
  qsbr_domain_t *d = calloc(1, sizeof *d);
  if (!d) return NULL;
  if (nworkers < 1) nworkers = 1;
  d->epoch = calloc((size_t)nworkers, sizeof *d->epoch);
  if (!d->epoch) {
    free(d);
    return NULL;
  }
  d->nworkers = nworkers;
  return d;
}

void qsbr_worker_quiescent(qsbr_domain_t *d, int tid) {
  if (!d || tid < 0 || tid >= d->nworkers) return;
  atomic_fetch_add_explicit(&d->epoch[tid], 1, memory_order_release);
}

int qsbr_worker_count(const qsbr_domain_t *d) { return d ? d->nworkers : 0; }

void qsbr_mark(const qsbr_domain_t *d, uint64_t *out) {
  if (!d) return;
  for (int i = 0; i < d->nworkers; i++) out[i] = atomic_load_explicit(&d->epoch[i], memory_order_acquire);
}

int qsbr_mark_passed(const qsbr_domain_t *d, const uint64_t *mark) {
  if (!d) return 1;
  for (int i = 0; i < d->nworkers; i++) if (atomic_load_explicit(&d->epoch[i], memory_order_acquire) == mark[i]) return 0;
  return 1;
}

/* caller can't tick own epoch while blocked here */
int qsbr_mark_passed_excl(const qsbr_domain_t *d, const uint64_t *mark, int excl) {
  if (!d) return 1;
  for (int i = 0; i < d->nworkers; i++) {
    if (i == excl) continue;
    if (atomic_load_explicit(&d->epoch[i], memory_order_acquire) == mark[i]) return 0;
  }
  return 1;
}

void qsbr_backoff(int spins) {
  if (spins < 100) {
    sched_yield();
    return;
  }
  {
    struct timespec ts = {0, spins < 1000 ? 50000 : 1000000};
    nanosleep(&ts, NULL);
  }
}
