/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ws_broadcast.h"
#include "ws_frame.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define WS_BROADCAST_MAX_SINKS 256

typedef struct {
  ws_sink_fn fn;
  void *ctx;
} sink_t;

typedef struct {
  int n;
  sink_t sinks[];
} sink_snapshot_t;

static _Atomic(sink_snapshot_t *) g_snapshot;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_active_publishers;

void ws_broadcast_register(ws_sink_fn fn, void *ctx) {
  sink_snapshot_t *old, *fresh;
  int n;
  pthread_mutex_lock(&g_mtx);
  old = atomic_load_explicit(&g_snapshot, memory_order_relaxed);
  n = old ? old->n : 0;
  if (n >= WS_BROADCAST_MAX_SINKS) {
    pthread_mutex_unlock(&g_mtx);
    return;
  }
  fresh = malloc(sizeof *fresh + sizeof(sink_t) * (size_t)(n + 1));
  if (!fresh) {
    pthread_mutex_unlock(&g_mtx);
    return;
  }
  if (n) memcpy(fresh->sinks, old->sinks, sizeof(sink_t) * (size_t)n);
  fresh->sinks[n].fn = fn;
  fresh->sinks[n].ctx = ctx;
  fresh->n = n + 1;
  atomic_store_explicit(&g_snapshot, fresh, memory_order_release);
  pthread_mutex_unlock(&g_mtx);
  if (old) {
    /* wait for publish() still reading it */
    while (atomic_load_explicit(&g_active_publishers, memory_order_acquire) != 0)
      sched_yield();
    free(old);
  }
}

void ws_broadcast_unregister(ws_sink_fn fn, void *ctx) {
  sink_snapshot_t *old, *fresh;
  int n, w = 0;
  pthread_mutex_lock(&g_mtx);
  old = atomic_load_explicit(&g_snapshot, memory_order_relaxed);
  if (!old) {
    pthread_mutex_unlock(&g_mtx);
    return;
  }
  n = old->n;
  fresh = malloc(sizeof *fresh + sizeof(sink_t) * (size_t)n);
  if (!fresh) {
    pthread_mutex_unlock(&g_mtx);
    return;
  }
  for (int i = 0; i < n; i++)
    if (!(old->sinks[i].fn == fn && old->sinks[i].ctx == ctx))
      fresh->sinks[w++] = old->sinks[i];
  fresh->n = w;
  atomic_store_explicit(&g_snapshot, fresh, memory_order_release);
  pthread_mutex_unlock(&g_mtx);
  /* caller frees/recycles ctx next: wait out any publish() already mid-flight */
  while (atomic_load_explicit(&g_active_publishers, memory_order_acquire) != 0)
    sched_yield();
  free(old);
}

void ws_broadcast_publish(const char *msg) {
  static _Thread_local uint8_t *frame;
  static _Thread_local size_t frame_cap;
  sink_snapshot_t *snap;
  int n;
  size_t mlen, flen;

  atomic_fetch_add_explicit(&g_active_publishers, 1, memory_order_acquire);
  snap = atomic_load_explicit(&g_snapshot, memory_order_acquire);
  n = snap ? snap->n : 0;
  if (!n) {
    atomic_fetch_sub_explicit(&g_active_publishers, 1, memory_order_release);
    return;
  }
  mlen = strlen(msg);
  flen = ws_frame_hdr_len(mlen) + mlen;
  if (flen > frame_cap || frame_cap > flen * 2) {
    uint8_t *p = realloc(frame, flen);
    if (!p) {
      if (flen > frame_cap) {
        atomic_fetch_sub_explicit(&g_active_publishers, 1, memory_order_release);
        return;
      }
    } else {
      frame = p;
      frame_cap = flen;
    }
  }
  ws_frame_encode(frame, WS_OP_TEXT, msg, mlen);
  for (int i = 0; i < n; i++)
    snap->sinks[i].fn(snap->sinks[i].ctx, frame, flen);
  atomic_fetch_sub_explicit(&g_active_publishers, 1, memory_order_release);
}

int ws_broadcast_has_sinks(void) {
  const sink_snapshot_t *snap = atomic_load_explicit(&g_snapshot, memory_order_acquire);
  return snap && snap->n > 0;
}
