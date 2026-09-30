/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "sockaddr_index.h"

#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../helper/ioutil.h"

typedef enum { SOCKADDR_INDEX_EMPTY, SOCKADDR_INDEX_OCCUPIED, SOCKADDR_INDEX_TOMBSTONE } sockaddr_index_state_t;

typedef struct {
  sockaddr_index_state_t state;
  int family;
  unsigned char bytes[16];
  size_t byteslen;
  unsigned short port;
  size_t slot;
} sockaddr_index_entry_t;

struct sockaddr_index {
  sockaddr_index_entry_t *entries;
  size_t size; /* power of two */
  size_t mask;
  size_t used;       /* occupied entries */
  size_t tombstones; /* reaped entries pending a rebuild */
};

typedef struct {
  int family;
  unsigned char bytes[16];
  size_t byteslen;
  unsigned short port;
} sockaddr_key_t;

/* canonicalizes sockaddr to hashable/comparable fields. unknown family, NULL/zero-length address collapse onto shared key */
static void canon_key(const struct sockaddr *addr, socklen_t addrlen, sockaddr_key_t *k) {
  if (addr && addrlen >= (socklen_t)sizeof(struct sockaddr_in) && addr->sa_family == AF_INET) {
    const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
    k->family = AF_INET;
    memcpy(k->bytes, &a->sin_addr, sizeof a->sin_addr);
    k->byteslen = sizeof a->sin_addr;
    k->port = (unsigned short)a->sin_port; /* network order, opaque key material, no conversion needed */
    return;
  }
  if (addr && addrlen >= (socklen_t)sizeof(struct sockaddr_in6) && addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)addr;
    k->family = AF_INET6;
    memcpy(k->bytes, &a->sin6_addr, sizeof a->sin6_addr);
    k->byteslen = sizeof a->sin6_addr;
    k->port = (unsigned short)a->sin6_port;
    return;
  }
  k->family = 0;
  k->byteslen = 0;
  k->port = 0;
}

static size_t key_hash(const sockaddr_key_t *k) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
  for (size_t i = 0; i < k->byteslen; i++) {
    h ^= k->bytes[i];
    h *= 1099511628211ULL; /* FNV prime */
  }
  h ^= (unsigned)k->family;
  h *= 1099511628211ULL;
  h ^= (unsigned char)(k->port & 0xff); /* byte-at-a-time, matches bytes[] loop above: keeps
    avalanche when only one port byte varies (sequential client ports differ in just one) */
  h *= 1099511628211ULL;
  h ^= (unsigned char)(k->port >> 8);
  h *= 1099511628211ULL;
  return (size_t)h;
}
static int entry_key_eq(const sockaddr_index_entry_t *e, const sockaddr_key_t *k) {
  return e->family == k->family && e->byteslen == k->byteslen && e->port == k->port && (k->byteslen == 0 || memcmp(e->bytes, k->bytes, k->byteslen) == 0);
}

sockaddr_index_t *sockaddr_index_new(size_t cap) {
  sockaddr_index_t *idx = calloc(1, sizeof *idx);
  if (!idx) return NULL;
  idx->size = next_pow2(cap * 2);
  if (idx->size < 4)
    idx->size = 4;
  idx->mask = idx->size - 1;
  idx->entries = calloc(idx->size, sizeof *idx->entries);
  if (!idx->entries) {
    free(idx);
    return NULL;
  }
  return idx;
}

void sockaddr_index_free(sockaddr_index_t *idx) {
  if (!idx) return;
  free(idx->entries);
  free(idx);
}

/* drops tombstones, reinserts live entries into zeroed table same size.
   keys live in entries, no caller data needed */
static void rebuild(sockaddr_index_t *idx) {
  sockaddr_index_entry_t *old = idx->entries;
  size_t old_size = idx->size;
  size_t live = 0;

  idx->entries = calloc(idx->size, sizeof *idx->entries);
  if (!idx->entries) {
    /* OOM mid-rebuild: keep old table, oversized but correct */
    idx->entries = old;
    return;
  }
  for (size_t i = 0; i < old_size; i++) {
    sockaddr_key_t k;
    size_t h;
    if (old[i].state != SOCKADDR_INDEX_OCCUPIED) continue;
    k.family = old[i].family;
    memcpy(k.bytes, old[i].bytes, old[i].byteslen);
    k.byteslen = old[i].byteslen;
    k.port = old[i].port;
    h = key_hash(&k) & idx->mask;
    while (idx->entries[h].state == SOCKADDR_INDEX_OCCUPIED) h = (h + 1) & idx->mask;
    idx->entries[h] = old[i];
    live++;
  }
  free(old);
  idx->used = live;
  idx->tombstones = 0;
}

size_t sockaddr_index_find(const sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen) {
  sockaddr_key_t k;
  size_t h;

  canon_key(addr, addrlen, &k);
  h = key_hash(&k) & idx->mask;

  for (;;) {
    const sockaddr_index_entry_t *e = &idx->entries[h];
    if (e->state == SOCKADDR_INDEX_EMPTY) return SIZE_MAX;
    if (e->state == SOCKADDR_INDEX_OCCUPIED && entry_key_eq(e, &k)) return e->slot;
    h = (h + 1) & idx->mask;
  }
}

void sockaddr_index_insert(sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen, size_t slot_idx) {
  sockaddr_key_t k;
  size_t h;

  canon_key(addr, addrlen, &k);
  h = key_hash(&k) & idx->mask;
  while (idx->entries[h].state == SOCKADDR_INDEX_OCCUPIED) h = (h + 1) & idx->mask;
  if (idx->entries[h].state == SOCKADDR_INDEX_TOMBSTONE) idx->tombstones--;
  else idx->used++;
  idx->entries[h].state = SOCKADDR_INDEX_OCCUPIED;
  idx->entries[h].family = k.family;
  memcpy(idx->entries[h].bytes, k.bytes, k.byteslen);
  idx->entries[h].byteslen = k.byteslen;
  idx->entries[h].port = k.port;
  idx->entries[h].slot = slot_idx;
  if (idx->used + idx->tombstones > (idx->size / 4) * 3) rebuild(idx);
}

size_t sockaddr_stripe_of(const struct sockaddr *addr, socklen_t addrlen, size_t stripe_count) {
  sockaddr_key_t k;
  if (!addr || addrlen == 0) return 0;
  canon_key(addr, addrlen, &k);
  return key_hash(&k) % stripe_count;
}

void sockaddr_index_remove(sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen) {
  sockaddr_key_t k;
  size_t h;

  canon_key(addr, addrlen, &k);
  h = key_hash(&k) & idx->mask;

  for (;;) {
    sockaddr_index_entry_t *e = &idx->entries[h];
    if (e->state == SOCKADDR_INDEX_EMPTY) return;
    if (e->state == SOCKADDR_INDEX_OCCUPIED && entry_key_eq(e, &k)) {
      e->state = SOCKADDR_INDEX_TOMBSTONE;
      idx->used--;
      if (++idx->tombstones > idx->size / 4) rebuild(idx);
      return;
    }
    h = (h + 1) & idx->mask;
  }
}
