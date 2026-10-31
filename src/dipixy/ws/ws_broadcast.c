/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ws_broadcast.h"
#include "ws_frame.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WS_BROADCAST_MAX_SINKS 256

typedef struct {
  ws_sink_fn fn;
  void *ctx;
} sink_t;

typedef struct {
  _Atomic int refs;
  int n;
  sink_t sinks[];
} sink_snapshot_t;

static _Atomic(sink_snapshot_t *) g_snapshot;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_drain_mtx = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_inflight[2];
static int g_epoch; /* g_mtx */

static void snapshot_release(sink_snapshot_t *s) {
  if (atomic_fetch_sub_explicit(&s->refs, 1, memory_order_acq_rel) == 1) free(s);
}

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
  atomic_init(&fresh->refs, 1);
  if (n) memcpy(fresh->sinks, old->sinks, sizeof(sink_t) * (size_t)n);
  fresh->sinks[n].fn = fn;
  fresh->sinks[n].ctx = ctx;
  fresh->n = n + 1;
  atomic_store_explicit(&g_snapshot, fresh, memory_order_release);
  pthread_mutex_unlock(&g_mtx);
  if (old) snapshot_release(old);
}

static void snapshot_nap(void) {
  struct timespec ts = {0, 20000};
  nanosleep(&ts, NULL);
}

/* barrier: no publisher touches ctx after return. caller holds no lock sinks take.
   epoch flip: publishers started before swap may hold any older snapshot */
void ws_broadcast_unregister(ws_sink_fn fn, void *ctx) {
  sink_snapshot_t *old, *fresh;
  int n;
  int w = 0;
  int drain;
  pthread_mutex_lock(&g_drain_mtx);
  for (;;) {
    pthread_mutex_lock(&g_mtx);
    old = atomic_load_explicit(&g_snapshot, memory_order_relaxed);
    if (!old) {
      pthread_mutex_unlock(&g_mtx);
      pthread_mutex_unlock(&g_drain_mtx);
      return;
    }
    n = old->n;
    fresh = malloc(sizeof *fresh + sizeof(sink_t) * (size_t)n);
    if (fresh) break;
    pthread_mutex_unlock(&g_mtx);
    snapshot_nap(); /* may not fail: ctx dies on return */
  }
  atomic_init(&fresh->refs, 1);
  for (int i = 0; i < n; i++)
    if (!(old->sinks[i].fn == fn && old->sinks[i].ctx == ctx))
      fresh->sinks[w++] = old->sinks[i];
  fresh->n = w;
  atomic_store_explicit(&g_snapshot, fresh, memory_order_release);
  drain = g_epoch;
  g_epoch ^= 1;
  pthread_mutex_unlock(&g_mtx);
  while (atomic_load_explicit(&g_inflight[drain], memory_order_acquire) > 0) snapshot_nap();
  pthread_mutex_unlock(&g_drain_mtx);
  snapshot_release(old);
}

static pthread_key_t g_frame_key;
static pthread_once_t g_frame_key_once = PTHREAD_ONCE_INIT;

static void publish_done(sink_snapshot_t *snap, int epoch) {
  snapshot_release(snap);
  atomic_fetch_sub_explicit(&g_inflight[epoch], 1, memory_order_release);
}

/* thread exit frees publish buffer */
static void frame_key_init(void) {
  pthread_key_create(&g_frame_key, free);
}

void ws_broadcast_publish(const char *msg) {
  static _Thread_local uint8_t *frame;
  static _Thread_local size_t frame_cap;
  sink_snapshot_t *snap;
  int n;
  int epoch;
  size_t mlen, flen;

  pthread_mutex_lock(&g_mtx);
  snap = atomic_load_explicit(&g_snapshot, memory_order_relaxed);
  epoch = g_epoch;
  if (snap) {
    atomic_fetch_add_explicit(&snap->refs, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_inflight[epoch], 1, memory_order_relaxed);
  }
  pthread_mutex_unlock(&g_mtx);

  if (!snap) return;
  n = snap->n;
  if (!n) {
    publish_done(snap, epoch);
    return;
  }
  mlen = strlen(msg);
  flen = ws_frame_hdr_len(mlen) + mlen;
  if (flen > frame_cap || frame_cap > flen * 2) {
    uint8_t *p = realloc(frame, flen);
    if (!p) {
      if (flen > frame_cap) {
        publish_done(snap, epoch);
        return;
      }
    } else {
      frame = p;
      frame_cap = flen;
      pthread_once(&g_frame_key_once, frame_key_init);
      pthread_setspecific(g_frame_key, frame);
    }
  }
  ws_frame_encode(frame, WS_OP_TEXT, msg, mlen);
  for (int i = 0; i < n; i++)
    snap->sinks[i].fn(snap->sinks[i].ctx, frame, flen);
  publish_done(snap, epoch);
}

int ws_broadcast_has_sinks(void) {
  int result;
  const sink_snapshot_t *snap;
  pthread_mutex_lock(&g_mtx);
  snap = atomic_load_explicit(&g_snapshot, memory_order_relaxed);
  result = snap && snap->n > 0;
  pthread_mutex_unlock(&g_mtx);
  return result;
}
