/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdlib.h>
#include "priv.h"

struct tsinspect_agg {
  metrics_inspect_ts_t level;
  pthread_mutex_t lock;
  tsinspect_t **list;
  unsigned n;
  unsigned cap;
  tsinspect_counters_t retired;
  tspack_sync_t retired_sync;
  int sync_used;
};

static void add_counters(tsinspect_counters_t *dst, const tsinspect_counters_t *src) {
  dst->packets += src->packets;
  dst->null_packets += src->null_packets;
  dst->scrambled_packets += src->scrambled_packets;
  dst->clear_packets += src->clear_packets;
  dst->transport_errors += src->transport_errors;
  dst->continuity_errors += src->continuity_errors;
  dst->duplicate_packets += src->duplicate_packets;
  dst->discontinuity_indicators += src->discontinuity_indicators;
  dst->pat_errors += src->pat_errors;
  dst->pmt_errors += src->pmt_errors;
  dst->referenced_pid_missing += src->referenced_pid_missing;
  dst->cat_errors += src->cat_errors;
  dst->sdt_errors += src->sdt_errors;
  dst->nit_errors += src->nit_errors;
  dst->sdt_other_errors += src->sdt_other_errors;
  dst->nit_other_errors += src->nit_other_errors;
  dst->pcr_repetition_errors += src->pcr_repetition_errors;
  dst->pcr_discontinuity_errors += src->pcr_discontinuity_errors;
  dst->pts_errors += src->pts_errors;
  dst->pcr_accuracy_errors += src->pcr_accuracy_errors;
  dst->eit_errors += src->eit_errors;
  dst->eit_other_errors += src->eit_other_errors;
  dst->rst_errors += src->rst_errors;
  dst->tdt_errors += src->tdt_errors;
  dst->si_crc_errors += src->si_crc_errors;
  dst->unreferenced_pids += src->unreferenced_pids;
  dst->unreferenced_packets += src->unreferenced_packets;
  dst->unreferenced_unlisted_pids += src->unreferenced_unlisted_pids;
  dst->pid_added += src->pid_added;
  dst->pid_removed += src->pid_removed;
  dst->stalls += src->stalls;
  dst->stall_ms += src->stall_ms;
}

tsinspect_agg_t *tsinspect_agg_new(metrics_inspect_ts_t level) {
  tsinspect_agg_t *a;
  if (level == METRICS_INSPECT_TS_OFF) return NULL;
  a = calloc(1, sizeof *a);
  if (!a) return NULL;
  a->level = level;
  pthread_mutex_init(&a->lock, NULL);
  return a;
}

void tsinspect_agg_free(tsinspect_agg_t *a) {
  if (!a) return;
  for (unsigned i = 0; i < a->n; i++) tsinspect_free(a->list[i]);
  pthread_mutex_destroy(&a->lock);
  free(a->list);
  free(a);
}

tsinspect_t *tsinspect_agg_add(tsinspect_agg_t *a) {
  tsinspect_t *t;
  if (!a) return NULL;
  t = tsinspect_new_light(a->level);
  if (!t) return NULL;
  pthread_mutex_lock(&a->lock);
  if (a->n == a->cap) {
    unsigned ncap = a->cap ? a->cap * 2 : 16;
    tsinspect_t **nl = realloc(a->list, ncap * sizeof *nl);
    if (!nl) {
      pthread_mutex_unlock(&a->lock);
      tsinspect_free(t);
      return NULL;
    }
    a->list = nl;
    a->cap = ncap;
  }
  a->list[a->n++] = t;
  pthread_mutex_unlock(&a->lock);
  return t;
}

void tsinspect_agg_remove(tsinspect_agg_t *a, tsinspect_t *t) {
  if (!a || !t) return;
  publish(t);
  pthread_mutex_lock(&a->lock);
  add_counters(&a->retired, &t->pub.c);
  a->retired_sync.byte_errors += t->pub.sync.byte_errors;
  a->retired_sync.losses += t->pub.sync.losses;
  a->sync_used |= t->pub.sync_used;
  for (unsigned i = 0; i < a->n; i++) {
    if (a->list[i] == t) {
      a->list[i] = a->list[--a->n];
      break;
    }
  }
  pthread_mutex_unlock(&a->lock);
  tsinspect_free(t);
}

void tsinspect_agg_put(tsinspect_agg_t *a, metrics_writer_t *w, const char *stream) {
  tsinspect_counters_t sum;
  tspack_sync_t sync;
  int sync_used;
  if (!a) return;
  pthread_mutex_lock(&a->lock);
  sum = a->retired;
  sync = a->retired_sync;
  sync_used = a->sync_used;
  for (unsigned i = 0; i < a->n; i++) {
    tsinspect_t *t = a->list[i];
    pthread_mutex_lock(&t->pub_lock);
    add_counters(&sum, &t->pub.c);
    sync.byte_errors += t->pub.sync.byte_errors;
    sync.losses += t->pub.sync.losses;
    sync_used |= t->pub.sync_used;
    pthread_mutex_unlock(&t->pub_lock);
  }
  pthread_mutex_unlock(&a->lock);
  put_header_series(w, stream, &sum, &sync, sync_used);
}

void tsinspect_agg_put_cb(metrics_writer_t *w, void *ctx) {
  tsinspect_agg_put(ctx, w, "input0");
}
