/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "lib/net/sockaddr_index.h"

#include "rtx_session_table.h"

struct rtx_session_table {
  rtx_session_slot_t *slots; /* fixed array, size cap, preallocated once */
  size_t cap;

  sockaddr_index_t *index; /* addr->slot, O(1) avg */

  size_t *free_list; /* stack of never-claimed slot indices, for O(1) claim */
  size_t free_count;

  pthread_mutex_t lock;
  size_t reap_cursor; /* reap_step()'s scan position, wraps at cap */
};

rtx_session_table_t *rtx_session_table_new(size_t cap) {
  rtx_session_table_t *t;

  if (cap == 0)
    return NULL;
  t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->slots = calloc(cap, sizeof *t->slots);
  t->free_list = calloc(cap, sizeof *t->free_list);
  t->index = sockaddr_index_new(cap);
  if (!t->slots || !t->free_list || !t->index) {
    free(t->slots);
    free(t->free_list);
    sockaddr_index_free(t->index);
    free(t);
    return NULL;
  }
  t->cap = cap;
  for (size_t i = 0; i < cap; i++)
    t->free_list[i] = cap - 1 - i;
  t->free_count = cap;
  pthread_mutex_init(&t->lock, NULL);
  return t;
}

void rtx_session_table_free(rtx_session_table_t *t) {
  if (!t)
    return;
  pthread_mutex_destroy(&t->lock);
  sockaddr_index_free(t->index);
  free(t->free_list);
  free(t->slots);
  free(t);
}

rtx_session_slot_t *rtx_session_table_get(rtx_session_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
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
    /* table full: evict coldest session, O(cap) rare path */
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

/* checks+reclaims one slot if stale. caller holds t->lock */
static void reap_one(rtx_session_table_t *t, size_t i, time_t now, time_t max_age_s) {
  if (!t->slots[i].valid)
    return;
  if (now - t->slots[i].last_seen <= max_age_s)
    return;
  sockaddr_index_remove(t->index, (const struct sockaddr *)&t->slots[i].addr, t->slots[i].addrlen);
  t->slots[i].valid = 0;
  t->free_list[t->free_count++] = i;
}

void rtx_session_table_reap_step(rtx_session_table_t *t, time_t max_age_s, size_t max_scan) {
  time_t now = time(NULL);
  size_t n;
  pthread_mutex_lock(&t->lock);
  n = max_scan < t->cap ? max_scan : t->cap;
  for (size_t i = 0; i < n; i++) {
    reap_one(t, t->reap_cursor, now, max_age_s);
    t->reap_cursor = (t->reap_cursor + 1) % t->cap;
  }
  pthread_mutex_unlock(&t->lock);
}
