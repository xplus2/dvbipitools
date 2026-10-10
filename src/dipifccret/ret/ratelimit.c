/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

#include "lib/sys/ioutil.h"

#include "ratelimit.h"

#define RATELIMIT_LOCKS 16

typedef struct {
  int family; /* 0 = empty */
  unsigned char key[8]; /* v4: 4 bytes, v6: /64 prefix */
  double tokens;
  int64_t last_ms;
} bucket_t;

struct ratelimit {
  bucket_t *slots;
  size_t mask;
  uint64_t seed;
  pthread_mutex_t locks[RATELIMIT_LOCKS];
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
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    rl->seed = (uint64_t)t.tv_nsec ^ ((uint64_t)t.tv_sec << 32) ^ (uint64_t)(uintptr_t)rl;
  }
  for (size_t i = 0; i < RATELIMIT_LOCKS; i++)
    pthread_mutex_init(&rl->locks[i], NULL);
  return rl;
}

void ratelimit_free(ratelimit_t *rl) {
  if (!rl) return;
  for (size_t i = 0; i < RATELIMIT_LOCKS; i++)
    pthread_mutex_destroy(&rl->locks[i]);
  free(rl->slots);
  free(rl);
}

static int key_of(const struct sockaddr *addr, int *family, unsigned char key[8]) {
  memset(key, 0, 8);
  if (addr->sa_family == AF_INET) {
    memcpy(key, &((const struct sockaddr_in *)addr)->sin_addr, 4);
    *family = AF_INET;
    return 1;
  }
  if (addr->sa_family == AF_INET6) {
    memcpy(key, &((const struct sockaddr_in6 *)addr)->sin6_addr, 8);
    *family = AF_INET6;
    return 1;
  }
  return 0;
}

static size_t hash_key(const ratelimit_t *rl, int family, const unsigned char key[8]) {
  uint64_t h = rl->seed ^ 1469598103934665603ULL;
  h ^= (uint64_t)family;
  h *= 1099511628211ULL;
  for (size_t i = 0; i < 8; i++) {
    h ^= key[i];
    h *= 1099511628211ULL;
  }
  h ^= h >> 32;
  return (size_t)h;
}

unsigned ratelimit_take(ratelimit_t *rl, const struct sockaddr *addr, unsigned rate, unsigned burst, unsigned want, int64_t now_ms) {
  unsigned char key[8];
  int family;
  size_t idx;
  bucket_t *b;
  pthread_mutex_t *lock;
  unsigned granted;

  if (rate == 0 || !rl) return want;
  if (!addr || !key_of(addr, &family, key)) return 0;
  if (burst < 1) burst = 1;

  idx = hash_key(rl, family, key) & rl->mask;
  b = &rl->slots[idx];
  lock = &rl->locks[idx % RATELIMIT_LOCKS];

  pthread_mutex_lock(lock);
  if (b->family != family || memcmp(b->key, key, 8) != 0) {
    b->family = family;
    memcpy(b->key, key, 8);
    b->tokens = (double)burst;
    b->last_ms = now_ms;
  } else if (now_ms > b->last_ms) {
    b->tokens += (double)(now_ms - b->last_ms) * (double)rate / 1000.0;
    if (b->tokens > (double)burst) b->tokens = (double)burst;
    b->last_ms = now_ms;
  }
  granted = (unsigned)b->tokens;
  if (granted > want) granted = want;
  b->tokens -= (double)granted;
  pthread_mutex_unlock(lock);
  return granted;
}
