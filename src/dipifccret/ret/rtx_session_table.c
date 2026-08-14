/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "rtx_session_table.h"

struct rtx_session_table {
  rtx_session_slot_t *slots; /* fixed array, size cap, preallocated once */
  size_t cap;

  size_t *hash; /* open-addr index: key->slot+1. 0=empty, TOMBSTONE=reaped/evicted. rebuild >75% load. */
  size_t hash_size;
  size_t hash_mask;
  size_t hash_used;

  size_t *free_list; /* stack of never-claimed slot indices, for O(1) claim */
  size_t free_count;

  pthread_mutex_t lock;
  size_t reap_cursor; /* reap_step()'s scan position, wraps at cap */
};

#define RTX_SESSION_HASH_TOMBSTONE SIZE_MAX

static size_t next_pow2(size_t n) {
  size_t p = 1;
  while (p < n)
    p <<= 1;
  return p;
}

/* canonicalizes a sockaddr into hashable/comparable fields; unknown family or
   NULL/zero-length address (unit tests) all collapse onto the same single key */
static void canon_key(const struct sockaddr *addr, socklen_t addrlen, int *family, const unsigned char **bytes, size_t *byteslen, unsigned short *port) {
  static const unsigned char none[1] = {0};
  if (addr && addrlen >= (socklen_t)sizeof(struct sockaddr_in) && addr->sa_family == AF_INET) {
    const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
    *family = AF_INET;
    *bytes = (const unsigned char *)&a->sin_addr;
    *byteslen = sizeof a->sin_addr;
    *port = (unsigned short)a->sin_port; /* network order, opaque key material, no conversion needed */
    return;
  }
  if (addr && addrlen >= (socklen_t)sizeof(struct sockaddr_in6) && addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)addr;
    *family = AF_INET6;
    *bytes = (const unsigned char *)&a->sin6_addr;
    *byteslen = sizeof a->sin6_addr;
    *port = (unsigned short)a->sin6_port;
    return;
  }
  *family = 0;
  *bytes = none;
  *byteslen = 0;
  *port = 0;
}

static size_t key_hash(int family, const unsigned char *bytes, size_t byteslen, unsigned short port) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
  size_t i;
  for (i = 0; i < byteslen; i++) {
    h ^= bytes[i];
    h *= 1099511628211ULL; /* FNV prime */
  }
  h ^= (unsigned)family;
  h *= 1099511628211ULL;
  h ^= (unsigned)port;
  h *= 1099511628211ULL;
  return (size_t)h;
}

static void slot_key(const rtx_session_slot_t *s, int *family, const unsigned char **bytes, size_t *byteslen, unsigned short *port) {
  canon_key((const struct sockaddr *)&s->addr, s->addrlen, family, bytes, byteslen, port);
}

rtx_session_table_t *rtx_session_table_new(size_t cap) {
  rtx_session_table_t *t;
  size_t i;

  if (cap == 0)
    return NULL;
  t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->slots = calloc(cap, sizeof *t->slots);
  t->free_list = calloc(cap, sizeof *t->free_list);
  t->hash_size = next_pow2(cap * 2);
  if (t->hash_size < 4)
    t->hash_size = 4;
  t->hash_mask = t->hash_size - 1;
  t->hash = calloc(t->hash_size, sizeof *t->hash);
  if (!t->slots || !t->free_list || !t->hash) {
    free(t->slots);
    free(t->free_list);
    free(t->hash);
    free(t);
    return NULL;
  }
  t->cap = cap;
  for (i = 0; i < cap; i++)
    t->free_list[i] = cap - 1 - i;
  t->free_count = cap;
  pthread_mutex_init(&t->lock, NULL);
  return t;
}

void rtx_session_table_free(rtx_session_table_t *t) {
  if (!t)
    return;
  pthread_mutex_destroy(&t->lock);
  free(t->hash);
  free(t->free_list);
  free(t->slots);
  free(t);
}

/* drops all tombstones, reinserts live entries only. caller holds t->lock */
static void hash_rebuild(rtx_session_table_t *t) {
  size_t i, live = 0;

  memset(t->hash, 0, t->hash_size * sizeof *t->hash);
  for (i = 0; i < t->cap; i++) {
    int family;
    const unsigned char *bytes;
    size_t byteslen;
    unsigned short port;
    size_t h;
    if (!t->slots[i].valid)
      continue;
    slot_key(&t->slots[i], &family, &bytes, &byteslen, &port);
    h = key_hash(family, bytes, byteslen, port) & t->hash_mask;
    while (t->hash[h] != 0)
      h = (h + 1) & t->hash_mask;
    t->hash[h] = i + 1;
    live++;
  }
  t->hash_used = live;
}

/* tombstones slot_idx's hash entry, if still present. caller holds t->lock */
static void hash_remove(rtx_session_table_t *t, size_t slot_idx) {
  int family;
  const unsigned char *bytes;
  size_t byteslen;
  unsigned short port;
  size_t h;

  slot_key(&t->slots[slot_idx], &family, &bytes, &byteslen, &port);
  h = key_hash(family, bytes, byteslen, port) & t->hash_mask;
  for (;;) {
    size_t slot_plus1 = t->hash[h];
    if (slot_plus1 == 0)
      return;
    if (slot_plus1 == slot_idx + 1) {
      t->hash[h] = RTX_SESSION_HASH_TOMBSTONE;
      return;
    }
    h = (h + 1) & t->hash_mask;
  }
}

rtx_session_slot_t *rtx_session_table_get(rtx_session_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  int family;
  const unsigned char *bytes;
  size_t byteslen;
  unsigned short port;
  size_t h, avail = 0, idx;
  int have_avail = 0;
  time_t now = time(NULL);
  rtx_session_slot_t *result = NULL;

  canon_key(addr, addrlen, &family, &bytes, &byteslen, &port);
  h = key_hash(family, bytes, byteslen, port) & t->hash_mask;

  pthread_mutex_lock(&t->lock);

  for (;;) {
    size_t slot_plus1 = t->hash[h];
    if (slot_plus1 == 0) {
      if (!have_avail)
        avail = h;
      break;
    }
    if (slot_plus1 == RTX_SESSION_HASH_TOMBSTONE) {
      if (!have_avail) {
        avail = h;
        have_avail = 1;
      }
    } else {
      rtx_session_slot_t *s = &t->slots[slot_plus1 - 1];
      int sfamily;
      const unsigned char *sbytes;
      size_t sbyteslen;
      unsigned short sport;
      slot_key(s, &sfamily, &sbytes, &sbyteslen, &sport);
      if (sfamily == family && sbyteslen == byteslen && sport == port && (byteslen == 0 || memcmp(sbytes, bytes, byteslen) == 0)) {
        s->last_seen = now;
        result = s;
        break;
      }
    }
    h = (h + 1) & t->hash_mask;
  }

  if (result) {
    pthread_mutex_unlock(&t->lock);
    return result;
  }

  if (t->free_count > 0) {
    idx = t->free_list[--t->free_count];
  } else {
    /* table full: evict coldest session (O(cap), rare path), making room. */
    size_t i, oldest = 0;
    time_t oldest_time = 0;
    int found = 0;
    for (i = 0; i < t->cap; i++) {
      if (!t->slots[i].valid)
        continue;
      if (!found || t->slots[i].last_seen <= oldest_time) {
        oldest_time = t->slots[i].last_seen;
        oldest = i;
        found = 1;
      }
    }
    if (!found) {
      pthread_mutex_unlock(&t->lock);
      return NULL; /* cap == 0, shouldn't happen: rtx_session_table_new rejects it */
    }
    hash_remove(t, oldest);
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

  {
    int was_empty = (t->hash[avail] == 0);
    t->hash[avail] = idx + 1;
    if (was_empty && ++t->hash_used > (t->hash_size / 4) * 3)
      hash_rebuild(t);
  }

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
  hash_remove(t, i);
  t->slots[i].valid = 0;
  t->free_list[t->free_count++] = i;
}

void rtx_session_table_reap_step(rtx_session_table_t *t, time_t max_age_s, size_t max_scan) {
  time_t now = time(NULL);
  size_t i, n;
  pthread_mutex_lock(&t->lock);
  n = max_scan < t->cap ? max_scan : t->cap;
  for (i = 0; i < n; i++) {
    reap_one(t, t->reap_cursor, now, max_age_s);
    t->reap_cursor = (t->reap_cursor + 1) % t->cap;
  }
  pthread_mutex_unlock(&t->lock);
}
