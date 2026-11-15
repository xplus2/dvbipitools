/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

#include "lib/helper/growutil.h"

#include "ratelimit.h"
#include "lib/helper/dateutil.h"

#define TOKEN_Q 10
#define TOKEN_MAX ((1u << (32 - TOKEN_Q)) - 1)

/* st: tokens Q10 << 32 | last_ms low 32 bits */
typedef struct {
  _Atomic uint64_t id;
  _Atomic uint64_t st;
} bucket_t;

struct ratelimit {
  bucket_t *slots;
  size_t mask;
  uint64_t seed;
};

ratelimit_t *ratelimit_new(size_t slots) {
  ratelimit_t *rl = calloc(1, sizeof *rl);
  size_t n = next_pow2(slots);
  if (!rl) return NULL;
  rl->slots = calloc(n, sizeof *rl->slots);
  if (!rl->slots) {
    free(rl);
    return NULL;
  }
  rl->mask = n - 1;
  if (getrandom(&rl->seed, sizeof rl->seed, 0) != (ssize_t)sizeof rl->seed) {
    rl->seed = now_ns() ^ (uint64_t)(uintptr_t)rl;
  }
  return rl;
}

void ratelimit_free(ratelimit_t *rl) {
  if (!rl) return;
  free(rl->slots);
  free(rl);
}

static uint64_t key_id(const ratelimit_t *rl, const struct sockaddr *addr) {
  uint64_t k = 0;
  uint64_t h;
  if (addr->sa_family == AF_INET) memcpy(&k, &((const struct sockaddr_in *)addr)->sin_addr, 4);
  else if (addr->sa_family == AF_INET6) memcpy(&k, &((const struct sockaddr_in6 *)addr)->sin6_addr, 8);
  else return 0;
  h = (k ^ rl->seed ^ (uint64_t)addr->sa_family << 56) * 0x9E3779B97F4A7C15ull;
  h ^= h >> 29;
  return h | 1;
}

static uint64_t refill(uint64_t tok, uint32_t dt_ms, unsigned rate, uint64_t full) {
  uint64_t r = (uint64_t)(dt_ms > 86400000u ? 86400000u : dt_ms) * rate;
  tok += (r / 1000) << TOKEN_Q | ((r % 1000) << TOKEN_Q) / 1000;
  return tok > full ? full : tok;
}

unsigned ratelimit_take(ratelimit_t *rl, const struct sockaddr *addr, unsigned rate, unsigned burst, unsigned want, int64_t now_ms) {
  uint64_t id;
  uint64_t cur_id;
  uint64_t full;
  uint64_t s;
  uint32_t now32 = (uint32_t)now_ms;
  bucket_t *b;

  if (rate == 0 || !rl) return want;
  if (!addr || !(id = key_id(rl, addr))) return 0;
  if (burst < 1) burst = 1;
  if (burst > TOKEN_MAX) burst = TOKEN_MAX;
  full = (uint64_t)burst << TOKEN_Q;
  b = &rl->slots[id & rl->mask];
  cur_id = atomic_load_explicit(&b->id, memory_order_acquire);
  if (cur_id != id && atomic_compare_exchange_strong_explicit(&b->id, &cur_id, id, memory_order_acq_rel, memory_order_acquire))
    atomic_store_explicit(&b->st, full << 32 | now32, memory_order_release);
  s = atomic_load_explicit(&b->st, memory_order_acquire);
  for (;;) {
    uint64_t tok = s >> 32;
    uint32_t last = (uint32_t)s;
    unsigned granted;
    if ((int32_t)(now32 - last) > 0) {
      tok = refill(tok, now32 - last, rate, full);
      last = now32;
    }
    granted = (unsigned)(tok >> TOKEN_Q);
    if (granted > want) granted = want;
    tok -= (uint64_t)granted << TOKEN_Q;
    if (atomic_compare_exchange_weak_explicit(&b->st, &s, tok << 32 | last, memory_order_acq_rel, memory_order_acquire)) return granted;
  }
}
