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
  int n;
  sink_t sinks[];
} sink_snapshot_t;

static _Atomic(sink_snapshot_t *) g_snapshot;
static _Atomic int g_nsinks;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_inflight[2];
static _Atomic int g_epoch;

static void snapshot_nap(void) {
  struct timespec ts = {0, 20000};
  nanosleep(&ts, NULL);
}

/* g_mtx held. returns once no publisher can still see the old snapshot */
static void snapshot_replace(sink_snapshot_t *fresh) {
  sink_snapshot_t *old = atomic_exchange_explicit(&g_snapshot, fresh, memory_order_seq_cst);
  int drain = atomic_fetch_xor_explicit(&g_epoch, 1, memory_order_seq_cst);
  atomic_store_explicit(&g_nsinks, fresh->n, memory_order_seq_cst);
  while (atomic_load_explicit(&g_inflight[drain], memory_order_seq_cst) > 0) snapshot_nap();
  free(old);
}

static sink_snapshot_t *snapshot_alloc(int n) {
  sink_snapshot_t *s;
  while (!(s = malloc(sizeof *s + sizeof(sink_t) * (size_t)n))) snapshot_nap();
  return s;
}

void ws_broadcast_register(ws_sink_fn fn, void *ctx) {
  const sink_snapshot_t *old;
  sink_snapshot_t *fresh;
  int n;
  pthread_mutex_lock(&g_mtx);
  old = atomic_load_explicit(&g_snapshot, memory_order_relaxed);
  n = old ? old->n : 0;
  fresh = n < WS_BROADCAST_MAX_SINKS ? malloc(sizeof *fresh + sizeof(sink_t) * (size_t)(n + 1)) : NULL;
  if (fresh) {
    if (n) memcpy(fresh->sinks, old->sinks, sizeof(sink_t) * (size_t)n);
    fresh->sinks[n] = (sink_t){fn, ctx};
    fresh->n = n + 1;
    snapshot_replace(fresh);
  }
  pthread_mutex_unlock(&g_mtx);
}

/* barrier: no publisher touches ctx after return. caller holds no lock sinks take */
void ws_broadcast_unregister(ws_sink_fn fn, void *ctx) {
  const sink_snapshot_t *old;
  sink_snapshot_t *fresh;
  int w = 0;
  pthread_mutex_lock(&g_mtx);
  old = atomic_load_explicit(&g_snapshot, memory_order_relaxed);
  if (old) {
    fresh = snapshot_alloc(old->n);
    for (int i = 0; i < old->n; i++)
      if (!(old->sinks[i].fn == fn && old->sinks[i].ctx == ctx)) fresh->sinks[w++] = old->sinks[i];
    fresh->n = w;
    snapshot_replace(fresh);
  }
  pthread_mutex_unlock(&g_mtx);
}

typedef struct {
  uint8_t *p;
  size_t cap;
} frame_buf_t;

static pthread_key_t g_frame_key;
static pthread_once_t g_frame_key_once = PTHREAD_ONCE_INIT;

static void frame_buf_free(void *v) {
  frame_buf_t *b = v;
  free(b->p);
  free(b);
}

static void frame_key_init(void) { pthread_key_create(&g_frame_key, frame_buf_free); }

/* grow-only, freed at thread exit */
static uint8_t *frame_get(size_t need) {
  frame_buf_t *b;
  pthread_once(&g_frame_key_once, frame_key_init);
  b = pthread_getspecific(g_frame_key);
  if (!b) {
    b = calloc(1, sizeof *b);
    if (!b) return NULL;
    pthread_setspecific(g_frame_key, b);
  }
  if (need > b->cap) {
    size_t cap = need > b->cap * 2 ? need : b->cap * 2;
    uint8_t *p = realloc(b->p, cap);
    if (!p) return NULL;
    b->p = p;
    b->cap = cap;
  }
  return b->p;
}

void ws_broadcast_publish(const char *msg) {
  const sink_snapshot_t *snap;
  uint8_t *frame;
  int epoch;
  size_t mlen;
  size_t flen;
  for (;;) {
    epoch = atomic_load_explicit(&g_epoch, memory_order_seq_cst);
    atomic_fetch_add_explicit(&g_inflight[epoch], 1, memory_order_seq_cst);
    if (atomic_load_explicit(&g_epoch, memory_order_seq_cst) == epoch) break;
    atomic_fetch_sub_explicit(&g_inflight[epoch], 1, memory_order_seq_cst);
  }
  snap = atomic_load_explicit(&g_snapshot, memory_order_seq_cst);
  if (snap && snap->n) {
    mlen = strlen(msg);
    flen = ws_frame_hdr_len(mlen) + mlen;
    frame = frame_get(flen);
    if (frame) {
      ws_frame_encode(frame, WS_OP_TEXT, msg, mlen);
      for (int i = 0; i < snap->n; i++) snap->sinks[i].fn(snap->sinks[i].ctx, frame, flen);
    }
  }
  atomic_fetch_sub_explicit(&g_inflight[epoch], 1, memory_order_release);
}

int ws_broadcast_has_sinks(void) { return atomic_load_explicit(&g_nsinks, memory_order_relaxed) > 0; }
