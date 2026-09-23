/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdlib.h>

#include "priv.h"

static tsinspect_t *new_inspector(metrics_inspect_ts_t level, int light, int relay) {
  tsinspect_t *t;
  if (level == METRICS_INSPECT_TS_OFF) return NULL;
  t = calloc(1, sizeof *t);
  if (!t) return NULL;
  t->packet = level >= METRICS_INSPECT_TS_FULL ? packet_full : packet_base;
  if (!light) {
    t->x = calloc(1, sizeof *t->x);
    if (!t->x) {
      free(t);
      return NULL;
    }
    psi_obs_init(&t->x->obs, &t->now);
    t->x->obs.crc_bat = level >= METRICS_INSPECT_TS_MEDIUM;
  }
  t->level = level;
  t->relay = relay;
  t->pcr_pid = PID_NONE;
  pthread_mutex_init(&t->pub_lock, NULL);
  return t;
}

tsinspect_t *tsinspect_new(metrics_inspect_ts_t level) {
  return new_inspector(level, 0, 0);
}

tsinspect_t *tsinspect_new_light(metrics_inspect_ts_t level) {
  return new_inspector(level, 1, 0);
}

tsinspect_t *tsinspect_new_relay(metrics_inspect_ts_t level) {
  return new_inspector(level, 1, 1);
}

int tsinspect_enable_own_psi(tsinspect_t *t, int multi) {
  psi_t *psi;
  if (!t->x || t->psi) return -1;
  psi = psi_new();
  if (!psi) return -1;
  if (multi) psi_enable_multi_program(psi);
  t->own_psi = 1;
  tsinspect_bind_psi(t, psi);
  return 0;
}

void tsinspect_free(tsinspect_t *t) {
  if (!t) return;
  if (t->own_psi) psi_free(t->psi);
  pthread_mutex_destroy(&t->pub_lock);
  if (t->x) free(t->x->d);
  free(t->x);
  free(t);
}

const tsinspect_counters_t *tsinspect_counters(const tsinspect_t *t) {
  return &t->counters;
}

tspack_sync_t *tsinspect_sync(tsinspect_t *t) {
  t->sync_used = 1;
  return &t->sync;
}

const psi_obs_t *tsinspect_psi(const tsinspect_t *t) {
  return t->x ? &t->x->obs : NULL;
}

double tsinspect_pcr_max_interval(const tsinspect_t *t) {
  return t->pcr_max_cur > t->pcr_max_prev ? t->pcr_max_cur : t->pcr_max_prev;
}

double tsinspect_max_gap_ms(const tsinspect_t *t) {
  return (t->gap_max_cur > t->gap_max_prev ? t->gap_max_cur : t->gap_max_prev) * 1000.0;
}

double tsinspect_last_packet(const tsinspect_t *t) {
  return t->last_packet;
}

int tsinspect_wants_rx_ns(const tsinspect_t *t) {
  return t && t->level >= METRICS_INSPECT_TS_MEDIUM;
}

void tsinspect_set_rx_ns(tsinspect_t *t, uint64_t ns) {
  t->rx_ns = ns;
}

void tsinspect_bind_psi(tsinspect_t *t, psi_t *psi) {
  if (!t->x) return;
  t->psi = psi;
  if (psi) psi_set_observer(psi, &t->x->obs);
}

void tsinspect_set_known_pids(tsinspect_t *t, const unsigned *pids, unsigned n) {
  if (!t->x) return;
  for (unsigned i = 0; i < n; i++) if (pids[i] < 8192) t->x->known[pids[i] >> 3] |= (unsigned char)(1u << (pids[i] & 7));
  t->x->known_set = n > 0;
  if (n > 0 && !t->x->d) t->x->d = calloc(1, sizeof *t->x->d);
}
