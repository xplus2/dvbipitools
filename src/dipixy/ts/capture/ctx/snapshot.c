/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/ts/capture/priv.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "dipixy/version.h"

#define SNAPSHOT_SLACK 8

capture_snapshot_t *snapshot_alloc(int need) {
  capture_snapshot_t *snap;
  int cap = need + SNAPSHOT_SLACK;
  snap = malloc(sizeof *snap + sizeof(capture_ctx_t *) * (size_t)cap);
  if (snap) snap->cap = cap;
  return snap;
}

int open_count(void) {
  int n = 0;
  for (capture_ctx_t *c = g_open; c; c = c->next) n++;
  return n;
}

/* caller holds g_lock. snap->cap >= open_count(). consumes snap */
void publish_snapshot(capture_snapshot_t *snap) {
  capture_ctx_t *c;
  capture_snapshot_t *old;
  int shard_n[CAPTURE_PUMP_MAX_THREADS] = {0};
  int cursor[CAPTURE_PUMP_MAX_THREADS];
  int n = 0;
  int off = 0;

  for (c = g_open; c; c = c->next) {
    n++;
    shard_n[c->pump_shard]++;
  }
  snap->n = n;
  for (int i = 0; i < CAPTURE_PUMP_MAX_THREADS; i++) {
    snap->shard_start[i] = off;
    snap->shard_count[i] = shard_n[i];
    cursor[i] = off;
    off += shard_n[i];
  }
  for (c = g_open; c; c = c->next) snap->ctxs[cursor[c->pump_shard]++] = c;
  old = atomic_exchange_explicit(&g_snapshot, snap, memory_order_acq_rel);
  if (old) {
    old->retired_next = g_retired;
    g_retired = old;
  }
}

void free_snapshots(capture_snapshot_t *list) {
  capture_snapshot_t *next;
  for (; list; list = next) {
    next = list->retired_next;
    free(list);
  }
}

/* not on a pump thread, no lock held: may block on capture_wait_pumps_quiescent() */
void reclaim_retired_snapshots(void) {
  capture_snapshot_t *list;
  pthread_mutex_lock(&g_lock);
  list = g_retired;
  g_retired = NULL;
  pthread_mutex_unlock(&g_lock);
  if (!list) return;
  if (!pumps_joined()) capture_wait_pumps_quiescent();
  free_snapshots(list);
}
