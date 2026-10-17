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

size_t g_capture_ring_cap = 4 * 1024 * 1024;
capture_ctx_t *g_open;
tsinspect_agg_t *g_insp_agg;
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
_Atomic(capture_snapshot_t *) g_snapshot;
capture_snapshot_t *g_retired;

static int iface_eq(const char *a, const char *b) {
  if (!a && !b)
    return 1;
  if (!a || !b)
    return 0;
  return strcmp(a, b) == 0;
}

capture_ctx_t *find_existing(int family, const char *group, const char *source, unsigned port, const char *iface) {
  for (capture_ctx_t *c = g_open; c; c = c->next)
    if (c->backend == CAP_BACKEND_MCAST && c->family == family && c->port == port &&
        strcmp(c->group, group) == 0 && strcmp(c->source, source) == 0 && iface_eq(c->iface, iface))
      return c;
  return NULL;
}

capture_ctx_t *find_existing_tssrc(const char *key) {
  for (capture_ctx_t *c = g_open; c; c = c->next)
    if (c->backend == CAP_BACKEND_TSSRC && c->key && !atomic_load_explicit(&c->dead, memory_order_relaxed) && !strcmp(c->key, key))
      return c;
  return NULL;
}

static capture_ctx_t *find_dup(const capture_ctx_t *n) {
  if (n->backend == CAP_BACKEND_TSSRC) return n->key ? find_existing_tssrc(n->key) : NULL;
  return find_existing(n->family, n->group, n->source, n->port, n->iface);
}

/* c if linked, existing dup (ref taken) if raced, NULL on oom. caller frees c unless returned */
capture_ctx_t *link_ctx(capture_ctx_t *c) {
  capture_snapshot_t *snap = NULL;
  capture_ctx_t *dup;
  int need;
  for (;;) {
    pthread_mutex_lock(&g_lock);
    dup = find_dup(c);
    if (dup) {
      atomic_fetch_add_explicit(&dup->refcount, 1, memory_order_relaxed);
      pthread_mutex_unlock(&g_lock);
      free(snap);
      return dup;
    }
    need = open_count() + 1;
    if (snap && snap->cap >= need) break;
    pthread_mutex_unlock(&g_lock);
    free(snap);
    snap = snapshot_alloc(need);
    if (!snap) return NULL;
  }
  c->next = g_open;
  g_open = c;
  publish_snapshot(snap);
  pthread_mutex_unlock(&g_lock);
  return c;
}

/* drop to 0 under g_lock: lookups inc under g_lock, cannot resurrect.
 * oom: ref kept and ctx stays linked, leak beats dangling snapshot entry */
int unref_unlink_ctx(capture_ctx_t *ctx) {
  capture_snapshot_t *snap = NULL;
  int need;
  int cur = atomic_load_explicit(&ctx->refcount, memory_order_relaxed);
  while (cur > 1) { 
    if (atomic_compare_exchange_weak_explicit(&ctx->refcount, &cur, cur - 1, memory_order_acq_rel, memory_order_relaxed)) return 0;
  }
  for (;;) {
    pthread_mutex_lock(&g_lock);
    if (atomic_load_explicit(&ctx->refcount, memory_order_relaxed) > 1) {
      atomic_fetch_sub_explicit(&ctx->refcount, 1, memory_order_acq_rel);
      pthread_mutex_unlock(&g_lock);
      free(snap);
      return 0;
    }
    need = open_count();
    if (snap && snap->cap >= need) break;
    pthread_mutex_unlock(&g_lock);
    free(snap);
    snap = snapshot_alloc(need);
    if (!snap) return 0;
  }
  if (atomic_fetch_sub_explicit(&ctx->refcount, 1, memory_order_acq_rel) != 1) {
    pthread_mutex_unlock(&g_lock);
    free(snap);
    return 0;
  }
  for (capture_ctx_t **pp = &g_open; *pp; pp = &(*pp)->next) {
    if (*pp == ctx) {
      *pp = ctx->next;
      break;
    }
  }
  publish_snapshot(snap);
  pthread_mutex_unlock(&g_lock);
  return 1;
}

void capture_set_inspect(tsinspect_agg_t *agg) {
  g_insp_agg = agg;
}

void free_ctx_resources(capture_ctx_t *ctx) {
  if (ctx->insp) tsinspect_agg_remove(g_insp_agg, ctx->insp);
  if (ctx->backend == CAP_BACKEND_TSSRC) {
    tssrc_close(ctx->ts);
    free(ctx->key);
  } else {
    if (ctx->fcc) fcc_client_close(ctx->fcc);
    if (ctx->ret) ret_client_close(ctx->ret);
    if (ctx->fec_dec) fec2022_dec_free(ctx->fec_dec);
    if (ctx->fec_m) mcast_close(ctx->fec_m);
    mcast_close(ctx->m);
    free(ctx->iface);
  }
  free(ctx->ring);
  free(ctx);
}

capture_ctx_t *capture_try_ref(int family, const char *group, const char *source, unsigned port, const char *iface) {
  capture_ctx_t *c;
  pthread_mutex_lock(&g_lock);
  c = find_existing(family, group, source ? source : "", port, iface);
  if (c) atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
  pthread_mutex_unlock(&g_lock);
  return c;
}

void capture_close(capture_ctx_t *ctx) {
  if (!ctx) return;
  if (!unref_unlink_ctx(ctx)) return;
  if (retire_ctx(ctx)) return;
  if (!pumps_joined()) capture_wait_pumps_quiescent();
  reclaim_retired_snapshots();
  free_ctx_resources(ctx);
}

int capture_active_count(void) {
  int n = 0;
  pthread_mutex_lock(&g_lock);
  for (capture_ctx_t *c = g_open; c; c = c->next) n++;
  pthread_mutex_unlock(&g_lock);
  return n;
}

uint64_t capture_bytes_total(void) {
  uint64_t total = 0;
  pthread_mutex_lock(&g_lock);
  for (capture_ctx_t *c = g_open; c; c = c->next) total += atomic_load_explicit(&c->write_total, memory_order_relaxed);
  pthread_mutex_unlock(&g_lock);
  return total;
}

uint64_t capture_ctx_bytes(const capture_ctx_t *ctx) {
  return atomic_load_explicit(&ctx->write_total, memory_order_acquire);
}

capture_ctx_t *capture_ref(capture_ctx_t *ctx) {
  atomic_fetch_add_explicit(&ctx->refcount, 1, memory_order_relaxed);
  return ctx;
}

_Atomic int *capture_ts_push_head_ptr(capture_ctx_t *ctx) { return &ctx->ts_push_head; }

_Atomic(void *) *capture_hls_seg_head_ptr(capture_ctx_t *ctx) { return &ctx->hls_seg_head; }
