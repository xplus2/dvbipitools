/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "wake.h"
#include "lib/helper/dateutil.h"
#include "lib/helper/seqlock.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  _Alignas(CACHELINE) _Atomic unsigned pending;
  _Atomic int efd;
  _Atomic int waiting;
} wake_slot_t;

static wake_slot_t g_wake[REACTOR_MAX_WORKERS] = {[0 ... REACTOR_MAX_WORKERS - 1] = {0, -1, 0}};
static _Atomic int g_wake_n;
static _Thread_local int64_t t_wait_next = INT64_MAX;

#define WAKE_BATCH_WORDS ((REACTOR_MAX_WORKERS + 63) / 64)

static _Thread_local uint64_t t_batch[WAKE_BATCH_WORDS];
static _Thread_local int t_batching;

static void wake_write(int tid) {
  int efd = atomic_load_explicit(&g_wake[tid].efd, memory_order_relaxed);
  if (efd >= 0) {
    uint64_t one = 1;
    ssize_t r = write(efd, &one, sizeof one);
    (void)r;
  }
}

void reactor_wake_register(int tid, int efd) {
  if (tid < 0 || tid >= REACTOR_MAX_WORKERS) return;
  atomic_store_explicit(&g_wake[tid].pending, 0, memory_order_relaxed);
  atomic_store_explicit(&g_wake[tid].efd, efd, memory_order_relaxed);
  for (int n = atomic_load_explicit(&g_wake_n, memory_order_relaxed); n <= tid;)
    if (atomic_compare_exchange_weak_explicit(&g_wake_n, &n, tid + 1, memory_order_relaxed, memory_order_relaxed)) break;
}

void reactor_wake_close_all(void) {
  for (int i = 0; i < REACTOR_MAX_WORKERS; i++) {
    int efd = atomic_exchange_explicit(&g_wake[i].efd, -1, memory_order_relaxed);
    if (efd >= 0) close(efd);
  }
}

void reactor_wake(int tid, wake_kind_t kind) {
  if (tid < 0 || tid >= REACTOR_MAX_WORKERS) return;
  if (atomic_fetch_or_explicit(&g_wake[tid].pending, 1u << kind, memory_order_release)) return;
  if (t_batching) t_batch[tid / 64] |= 1ULL << (tid % 64);
  else wake_write(tid);
}

void reactor_wake_batch_begin(void) {
  memset(t_batch, 0, sizeof t_batch);
  t_batching = 1;
}

void reactor_wake_batch_end(void) {
  t_batching = 0;
  for (int w = 0; w < WAKE_BATCH_WORDS; w++) {
    for (uint64_t m = t_batch[w]; m; m &= m - 1) wake_write(w * 64 + __builtin_ctzll(m));
  }
}

unsigned reactor_wake_take(int tid) {
  if (tid < 0 || tid >= REACTOR_MAX_WORKERS) return 0;
  return atomic_exchange_explicit(&g_wake[tid].pending, 0, memory_order_acquire);
}

void reactor_wait_note(int64_t deadline_ms) {
  if (deadline_ms < t_wait_next) t_wait_next = deadline_ms;
}

int reactor_wait_timeout_ms(int tid, int cap_ms) {
  int64_t next = t_wait_next;
  int waiting = next != INT64_MAX;
  int64_t left;
  t_wait_next = INT64_MAX;
  if (tid < 0 || tid >= REACTOR_MAX_WORKERS) return cap_ms;
  if (!atomic_exchange_explicit(&g_wake[tid].waiting, waiting, memory_order_seq_cst) && waiting) return 0;
  if (!waiting) return cap_ms;
  left = next - now_ms();
  if (left <= 0) return 0;
  return left < cap_ms ? (int)left : cap_ms;
}

void reactor_wake_waiters(void) {
  int n = atomic_load_explicit(&g_wake_n, memory_order_relaxed);
  atomic_thread_fence(memory_order_seq_cst);
  for (int tid = 0; tid < n; tid++)
    if (atomic_load_explicit(&g_wake[tid].waiting, memory_order_relaxed)) reactor_wake(tid, WAKE_WAITERS);
}
