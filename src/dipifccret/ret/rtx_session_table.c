/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "lib/net/sockaddr_index.h"

#include "rtx_session_table.h"

#define RTX_STRIPE_COUNT_MAX 8 /* client addr hashed to one of these, independent lock each */

typedef struct {
  rtx_session_slot_t *slots; /* fixed array, size cap, preallocated once */
  size_t cap;

  sockaddr_index_t *index; /* addr->slot, O(1) avg */

  size_t *free_list; /* stack of never-claimed slot indices, for O(1) claim */
  size_t free_count;

  pthread_mutex_t lock;
  size_t reap_cursor; /* reap_step()'s scan position within stripe, wrap at cap */
} rtx_session_stripe_t;

struct rtx_session_table {
  rtx_session_stripe_t *stripes;
  size_t stripe_count; /* min(RTX_STRIPE_COUNT_MAX, cap): never a stripe with 0 slots */
};

static int stripe_init(rtx_session_stripe_t *s, size_t cap) {
  memset(s, 0, sizeof *s);
  s->slots = calloc(cap, sizeof *s->slots);
  s->free_list = calloc(cap, sizeof *s->free_list);
  s->index = sockaddr_index_new(cap);
  if (!s->slots || !s->free_list || !s->index) {
    free(s->slots);
    free(s->free_list);
    sockaddr_index_free(s->index);
    return 0;
  }
  s->cap = cap;
  for (size_t i = 0; i < cap; i++)
    s->free_list[i] = cap - 1 - i;
  s->free_count = cap;
  pthread_mutex_init(&s->lock, NULL);
  return 1;
}

static void stripe_destroy(rtx_session_stripe_t *s) {
  pthread_mutex_destroy(&s->lock);
  sockaddr_index_free(s->index);
  free(s->free_list);
  free(s->slots);
}

rtx_session_table_t *rtx_session_table_new(size_t cap) {
  rtx_session_table_t *t;
  size_t n, base, extra;

  if (cap == 0)
    return NULL;
  t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  n = cap < RTX_STRIPE_COUNT_MAX ? cap : RTX_STRIPE_COUNT_MAX;
  t->stripes = calloc(n, sizeof *t->stripes);
  if (!t->stripes) {
    free(t);
    return NULL;
  }
  t->stripe_count = n;
  base = cap / n; /* remainder spread over first `extra` stripes: total always == cap exactly */
  extra = cap % n;
  for (size_t i = 0; i < n; i++) {
    if (!stripe_init(&t->stripes[i], base + (i < extra ? 1 : 0))) {
      for (size_t j = 0; j < i; j++)
        stripe_destroy(&t->stripes[j]);
      free(t->stripes);
      free(t);
      return NULL;
    }
  }
  return t;
}

void rtx_session_table_free(rtx_session_table_t *t) {
  if (!t)
    return;
  for (size_t i = 0; i < t->stripe_count; i++)
    stripe_destroy(&t->stripes[i]);
  free(t->stripes);
  free(t);
}

static rtx_session_stripe_t *pick_stripe(rtx_session_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  return &t->stripes[sockaddr_stripe_of(addr, addrlen, t->stripe_count)];
}

static rtx_session_slot_t *stripe_get(rtx_session_stripe_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  time_t now = time(NULL);
  size_t idx, found;
  rtx_session_slot_t *result;

  pthread_mutex_lock(&t->lock);

  found = sockaddr_index_find(t->index, addr, addrlen);
  if (found != SIZE_MAX) {
    t->slots[found].last_seen = now;
    result = &t->slots[found];
    pthread_mutex_unlock(&t->lock);
    return result;
  }

  if (t->free_count > 0) {
    idx = t->free_list[--t->free_count];
  } else {
    /* stripe full: evict its own coldest session, O(cap/stripe_count) rare path.
       coldest-within-stripe, see rtx_session_table_get for full contract */
    size_t oldest = 0;
    time_t oldest_time = 0;
    int have = 0;
    for (size_t i = 0; i < t->cap; i++) {
      if (!t->slots[i].valid)
        continue;
      if (!have || t->slots[i].last_seen <= oldest_time) {
        oldest_time = t->slots[i].last_seen;
        oldest = i;
        have = 1;
      }
    }
    if (!have) {
      pthread_mutex_unlock(&t->lock);
      return NULL; /* cap == 0, shouldn't happen: rtx_session_table_new rejects it */
    }
    sockaddr_index_remove(t->index, (const struct sockaddr *)&t->slots[oldest].addr, t->slots[oldest].addrlen);
    idx = oldest;
  }

  memset(&t->slots[idx], 0, sizeof t->slots[idx]);
  if (addr && addrlen) {
    socklen_t cl = addrlen < (socklen_t)sizeof t->slots[idx].addr ? addrlen : (socklen_t)sizeof t->slots[idx].addr;
    memcpy(&t->slots[idx].addr, addr, cl);
    t->slots[idx].addrlen = cl;
  }
  t->slots[idx].valid = 1;
  t->slots[idx].last_seen = now;

  sockaddr_index_insert(t->index, addr, addrlen, idx);

  result = &t->slots[idx];
  pthread_mutex_unlock(&t->lock);
  return result;
}

/* no protocol path exists to reject a repair: full stripe evicts its own coldest session */
rtx_session_slot_t *rtx_session_table_get(rtx_session_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  return stripe_get(pick_stripe(t, addr, addrlen), addr, addrlen);
}

/* checks+reclaims one slot if stale. caller holds stripe lock */
static void reap_one(rtx_session_stripe_t *t, size_t i, time_t now, time_t max_age_s) {
  if (!t->slots[i].valid)
    return;
  if (now - t->slots[i].last_seen <= max_age_s)
    return;
  sockaddr_index_remove(t->index, (const struct sockaddr *)&t->slots[i].addr, t->slots[i].addrlen);
  t->slots[i].valid = 0;
  t->free_list[t->free_count++] = i;
}

size_t rtx_session_table_active_count(rtx_session_table_t *t) {
  size_t n = 0;
  for (size_t si = 0; si < t->stripe_count; si++) {
    rtx_session_stripe_t *s = &t->stripes[si];
    pthread_mutex_lock(&s->lock);
    n += s->cap - s->free_count;
    pthread_mutex_unlock(&s->lock);
  }
  return n;
}

/* max_scan split across every stripe, each locked only for its own share */
void rtx_session_table_reap_step(rtx_session_table_t *t, time_t max_age_s, size_t max_scan) {
  time_t now = time(NULL);
  size_t per_stripe = max_scan / t->stripe_count;

  if (per_stripe == 0)
    per_stripe = 1;
  for (size_t si = 0; si < t->stripe_count; si++) {
    rtx_session_stripe_t *s = &t->stripes[si];
    size_t n;

    pthread_mutex_lock(&s->lock);
    n = per_stripe < s->cap ? per_stripe : s->cap;
    for (size_t i = 0; i < n; i++) {
      reap_one(s, s->reap_cursor, now, max_age_s);
      s->reap_cursor = (s->reap_cursor + 1) % s->cap;
    }
    pthread_mutex_unlock(&s->lock);
  }
}
