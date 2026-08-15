/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "sockaddr_index.h"

#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SOCKADDR_INDEX_TOMBSTONE 2

typedef struct {
  int state; /* 0 empty, 1 occupied, SOCKADDR_INDEX_TOMBSTONE reaped */
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
  size_t used; /* occupied + tombstoned, rebuild threshold */
};

static size_t next_pow2(size_t n) {
  size_t p = 1;
  while (p < n)
    p <<= 1;
  return p;
}

/* canonicalizes sockaddr to hashable/comparable fields. unknown family, NULL/zero-length address collapse onto shared key */
static void canon_key(const struct sockaddr *addr, socklen_t addrlen, int *family, unsigned char bytes[16], size_t *byteslen, unsigned short *port) {
  if (addr && addrlen >= (socklen_t)sizeof(struct sockaddr_in) && addr->sa_family == AF_INET) {
    const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
    *family = AF_INET;
    memcpy(bytes, &a->sin_addr, sizeof a->sin_addr);
    *byteslen = sizeof a->sin_addr;
    *port = (unsigned short)a->sin_port; /* network order, opaque key material, no conversion needed */
    return;
  }
  if (addr && addrlen >= (socklen_t)sizeof(struct sockaddr_in6) && addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)addr;
    *family = AF_INET6;
    memcpy(bytes, &a->sin6_addr, sizeof a->sin6_addr);
    *byteslen = sizeof a->sin6_addr;
    *port = (unsigned short)a->sin6_port;
    return;
  }
  *family = 0;
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
static int entry_key_eq(const sockaddr_index_entry_t *e, int family, const unsigned char *bytes, size_t byteslen, unsigned short port) {
  return e->family == family && e->byteslen == byteslen && e->port == port && (byteslen == 0 || memcmp(e->bytes, bytes, byteslen) == 0);
}

sockaddr_index_t *sockaddr_index_new(size_t cap) {
  sockaddr_index_t *idx = calloc(1, sizeof *idx);
  if (!idx)
    return NULL;
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
  if (!idx)
    return;
  free(idx->entries);
  free(idx);
}

/* drops tombstones, reinserts live entries into zeroed table same size.
   keys live in entries, no caller data needed */
static void rebuild(sockaddr_index_t *idx) {
  sockaddr_index_entry_t *old = idx->entries;
  size_t old_size = idx->size, i, live = 0;

  idx->entries = calloc(idx->size, sizeof *idx->entries);
  if (!idx->entries) {
    /* OOM mid-rebuild: keep old table, oversized but correct */
    idx->entries = old;
    return;
  }
  for (i = 0; i < old_size; i++) {
    size_t h;
    if (old[i].state != 1)
      continue;
    h = key_hash(old[i].family, old[i].bytes, old[i].byteslen, old[i].port) & idx->mask;
    while (idx->entries[h].state == 1)
      h = (h + 1) & idx->mask;
    idx->entries[h] = old[i];
    live++;
  }
  free(old);
  idx->used = live;
}

size_t sockaddr_index_find(const sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen) {
  int family;
  unsigned char bytes[16];
  size_t byteslen, h;
  unsigned short port;

  canon_key(addr, addrlen, &family, bytes, &byteslen, &port);
  h = key_hash(family, bytes, byteslen, port) & idx->mask;

  for (;;) {
    const sockaddr_index_entry_t *e = &idx->entries[h];
    if (e->state == 0)
      return SIZE_MAX;
    if (e->state == 1 && entry_key_eq(e, family, bytes, byteslen, port))
      return e->slot;
    h = (h + 1) & idx->mask;
  }
}

void sockaddr_index_insert(sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen, size_t slot_idx) {
  int family;
  unsigned char bytes[16];
  size_t byteslen, h;
  unsigned short port;
  int was_empty;

  canon_key(addr, addrlen, &family, bytes, &byteslen, &port);
  h = key_hash(family, bytes, byteslen, port) & idx->mask;
  while (idx->entries[h].state == 1)
    h = (h + 1) & idx->mask;

  was_empty = idx->entries[h].state == 0;
  idx->entries[h].state = 1;
  idx->entries[h].family = family;
  memcpy(idx->entries[h].bytes, bytes, byteslen);
  idx->entries[h].byteslen = byteslen;
  idx->entries[h].port = port;
  idx->entries[h].slot = slot_idx;

  if (was_empty && ++idx->used > (idx->size / 4) * 3)
    rebuild(idx);
}

void sockaddr_index_remove(sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen) {
  int family;
  unsigned char bytes[16];
  size_t byteslen, h;
  unsigned short port;

  canon_key(addr, addrlen, &family, bytes, &byteslen, &port);
  h = key_hash(family, bytes, byteslen, port) & idx->mask;

  for (;;) {
    sockaddr_index_entry_t *e = &idx->entries[h];
    if (e->state == 0)
      return;
    if (e->state == 1 && entry_key_eq(e, family, bytes, byteslen, port)) {
      e->state = SOCKADDR_INDEX_TOMBSTONE;
      return;
    }
    h = (h + 1) & idx->mask;
  }
}
