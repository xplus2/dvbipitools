/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "qsbr.h"

#include <stdatomic.h>
#include <stdlib.h>

static _Atomic uint64_t *g_epoch;
static int g_nworkers;

void qsbr_init(int nworkers) {
  if (nworkers < 1) nworkers = 1;
  g_epoch = calloc((size_t)nworkers, sizeof *g_epoch);
  g_nworkers = g_epoch ? nworkers : 0;
}

void qsbr_worker_quiescent(int tid) {
  if (!g_epoch || tid < 0 || tid >= g_nworkers) return;
  atomic_fetch_add_explicit(&g_epoch[tid], 1, memory_order_release);
}

int qsbr_worker_count(void) { return g_nworkers; }

void qsbr_mark(uint64_t *out) {
  for (int i = 0; i < g_nworkers; i++) out[i] = atomic_load_explicit(&g_epoch[i], memory_order_acquire);
}

int qsbr_mark_passed(const uint64_t *mark) {
  for (int i = 0; i < g_nworkers; i++) if (atomic_load_explicit(&g_epoch[i], memory_order_acquire) == mark[i]) return 0;
  return 1;
}
