/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/demux/rtp.h"

#include <sched.h>
#include <string.h>
#include <time.h>

#define CAPTURE_TSSRC_DRAIN_BUDGET_MS 5

static const capture_snapshot_t *g_pump_last_snap[CAPTURE_PUMP_MAX_THREADS];
static int g_pump_resume_idx[CAPTURE_PUMP_MAX_THREADS];

/* set once by capture_pump_set_thread_count() before any pump/worker thread starts, read-only after: plain int safe */
static int g_n_pump_threads = 1;

/* odd=mid-sweep, even=idle. thread writes only its own slot. waiters wait for all even before freeing r-unlocked mem */
static _Atomic uint64_t g_pump_gen[CAPTURE_PUMP_MAX_THREADS];

static _Atomic int g_shard_ctr;

int next_pump_shard(void) {
  return atomic_fetch_add_explicit(&g_shard_ctr, 1, memory_order_relaxed) % g_n_pump_threads;
}

void capture_pump_set_thread_count(int n) {
  if (n < 1)
    n = 1;
  if (n > CAPTURE_PUMP_MAX_THREADS)
    n = CAPTURE_PUMP_MAX_THREADS;
  g_n_pump_threads = n;
}

void capture_wait_pumps_quiescent(void) {
  for (int i = 0; i < g_n_pump_threads; i++) {
    uint64_t g = atomic_load_explicit(&g_pump_gen[i], memory_order_acquire);
    if (g & 1) while (atomic_load_explicit(&g_pump_gen[i], memory_order_acquire) == g) sched_yield();
  }
}

static int try_pin(capture_ctx_t *c) {
  int cur = atomic_load_explicit(&c->refcount, memory_order_relaxed);
  for (;;) {
    if (cur <= 0) return 0;
    if (atomic_compare_exchange_weak_explicit(&c->refcount, &cur, cur + 1, memory_order_acquire, memory_order_relaxed)) return 1;
  }
}

static int64_t monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* stdin/http aren't packet-aligned: reassembles 188B packets across calls */
static int tssrc_drain(capture_ctx_t *ctx, void (*sink)(void *user, const unsigned char *pkt), void *user) {
  int64_t deadline = monotonic_ms() + CAPTURE_TSSRC_DRAIN_BUDGET_MS;
  /* caps g_lock hold per tick: continuous source never returns 0 on its own */
  for (;;) {
    unsigned char buf[CAPTURE_RECV_BUF];
    size_t len, off;
    ssize_t n;
    if (monotonic_ms() >= deadline) return 0;
    n = tssrc_read(ctx->ts, buf, sizeof buf, NULL);
    if (n < 0) return -1;
    if (n == 0) return 0;
    atomic_fetch_add_explicit(&ctx->write_total, (uint64_t)n, memory_order_relaxed);
    len = (size_t)n;
    off = 0;
    if (ctx->leftover_len) {
      size_t need = 188 - ctx->leftover_len;
      size_t take = need < len ? need : len;
      memcpy(ctx->leftover + ctx->leftover_len, buf, take);
      ctx->leftover_len += take;
      off += take;
      if (ctx->leftover_len < 188) continue;
      sink(user, ctx->leftover);
      ctx->leftover_len = 0;
    }
    for (; off + 188 <= len; off += 188) sink(user, buf + off);
    if (off < len) {
      ctx->leftover_len = len - off;
      memcpy(ctx->leftover, buf + off, ctx->leftover_len);
    }
  }
}

int capture_drain(capture_ctx_t *ctx, void (*sink)(void *user, const unsigned char *pkt), void *user) {
  if (ctx->backend == CAP_BACKEND_TSSRC) return tssrc_drain(ctx, sink, user);
  for (;;) {
    unsigned char buf[CAPTURE_RECV_BUF];
    const unsigned char *payload;
    size_t len;
    ssize_t n;
    int unwrapped = 0;
    if (ctx->fcc) {
      unwrapped = 1;
      n = fcc_client_read(ctx->fcc, ctx->m, buf, sizeof buf);
      if (fcc_client_done(ctx->fcc)) {
        fcc_client_close(ctx->fcc);
        ctx->fcc = NULL;
      }
    } else if (ctx->ret) {
      unwrapped = 1;
      n = ret_client_read(ctx->ret, ctx->m, buf, sizeof buf);
    } else {
      n = mcast_recv(ctx->m, buf, sizeof buf, NULL);
    }
    if (n < 0)
      return -1;
    if (n == 0)
      return 0;
    atomic_fetch_add_explicit(&ctx->write_total, (uint64_t)n, memory_order_relaxed);
    if (unwrapped) {
      payload = buf;
      len = (size_t)n;
    } else if (ctx->rtp) {
      size_t roff = rtp_payload_offset(buf, (size_t)n);
      if (roff == 0) continue;
      payload = buf + roff;
      len = (size_t)n - roff;
    } else {
      payload = buf;
      len = (size_t)n;
    }
    for (size_t off = 0; off + 188 <= len; off += 188) sink(user, payload + off);
  }
}

typedef struct {
  capture_ctx_t *ctx;
  void (*sink)(capture_ctx_t *ctx, void *user, const unsigned char *pkt);
  void *user;
  int *count;
} pump_trampoline_t;

static void pump_trampoline(void *user, const unsigned char *pkt) {
  pump_trampoline_t *t = user;
  t->sink(t->ctx, t->user, pkt);
  (*t->count)++;
}

/* generous bound on one pump thread's shard of open sources: excess just wait for next tick */
#define CAPTURE_PUMP_SNAPSHOT_MAX 4096

static void pump_release_pin(capture_ctx_t *c) {
  if (atomic_fetch_sub_explicit(&c->refcount, 1, memory_order_acq_rel) != 1) return;
  unlink_ctx(c);
  free_ctx_resources(c);
}

int capture_pump_tick(int pid, void (*sink)(capture_ctx_t *ctx, void *user, const unsigned char *pkt), void *user) {
  capture_ctx_t *snap_pin[CAPTURE_PUMP_SNAPSHOT_MAX];
  int n = 0, i, total = 0, idx, start;
  const capture_snapshot_t *snap;

  atomic_fetch_add_explicit(&g_pump_gen[pid], 1, memory_order_release);
  snap = atomic_load_explicit(&g_snapshot, memory_order_acquire);
  if (snap != g_pump_last_snap[pid]) {
    g_pump_last_snap[pid] = snap;
    g_pump_resume_idx[pid] = 0;
  }
  if (snap) {
    start = g_pump_resume_idx[pid];
    for (idx = start; idx < snap->n && n < CAPTURE_PUMP_SNAPSHOT_MAX; idx++) {
      capture_ctx_t *c = snap->ctxs[idx];
      if (c->pump_shard != pid || !try_pin(c)) continue;
      snap_pin[n++] = c;
    }
    if (n < CAPTURE_PUMP_SNAPSHOT_MAX) /* end reached b4 cap: wrap to head */
      for (idx = 0; idx < start && n < CAPTURE_PUMP_SNAPSHOT_MAX; idx++) {
        capture_ctx_t *c = snap->ctxs[idx];
        if (c->pump_shard != pid || !try_pin(c)) continue;
        snap_pin[n++] = c;
      }
    g_pump_resume_idx[pid] = idx >= snap->n ? 0 : idx;
  }

  for (i = 0; i < n; i++) {
    pump_trampoline_t t;
    t.ctx = snap_pin[i];
    t.sink = sink;
    t.user = user;
    t.count = &total;
    capture_drain(snap_pin[i], pump_trampoline, &t);
  }
  for (i = 0; i < n; i++) pump_release_pin(snap_pin[i]);
  atomic_fetch_add_explicit(&g_pump_gen[pid], 1, memory_order_release);
  return total;
}
