/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/ioutil.h"
#include "lib/log.h"

#include "../version.h"

#include "mcsend.h"

/* keys come from a fixed preallocated array: bounded key space, no tombstones needed. */
struct mcsend_entry {
  _Atomic(channel_t *) key;
  unsigned generation; /* published alongside key: see channel_t.generation */
  mcast_t *sock;
};

struct mcsend_table {
  struct mcsend_entry *entries;
  size_t hash_size;
  size_t hash_mask;
  const char *iface;
  int ttl;
};

static size_t ptr_hash(const void *p) {
  size_t h = (size_t)(uintptr_t)p >> 4; /* channel_t entries are array-strided, low bits are dead weight */
  h *= 1099511628211ULL; /* FNV prime, cheap avalanche */
  return h;
}

mcsend_table_t *mcsend_table_new(size_t max_channels, const char *iface, int ttl) {
  mcsend_table_t *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->hash_size = next_pow2(max_channels * 2);
  if (t->hash_size < 4)
    t->hash_size = 4;
  t->hash_mask = t->hash_size - 1;
  t->entries = calloc(t->hash_size, sizeof *t->entries);
  if (!t->entries) {
    free(t);
    return NULL;
  }
  t->iface = iface;
  t->ttl = ttl;
  return t;
}

void mcsend_table_free(mcsend_table_t *t) {
  if (!t)
    return;
  for (size_t i = 0; i < t->hash_size; i++) {
    if (t->entries[i].sock)
      mcast_close(t->entries[i].sock);
  }
  free(t->entries);
  free(t);
}

void mcsend_ensure(mcsend_table_t *t, channel_t *c, unsigned ff_port) {
  size_t h = ptr_hash(c) & t->hash_mask;
  size_t start = h;
  unsigned gen = atomic_load_explicit(&c->generation, memory_order_relaxed);
  unsigned port;
  mcast_t *m;

  for (;;) {
    channel_t *cur = atomic_load_explicit(&t->entries[h].key, memory_order_acquire);
    if (cur == c) {
      if (t->entries[h].generation == gen)
        return;   /* already have a current socket for this channel */
      break;      /* stale entry from a reaped/reclaimed slot: reopen below, same bucket */
    }
    if (cur == NULL)
      break; /* free bucket: not tracked yet */
    h = (h + 1) & t->hash_mask;
    if (h == start) {
      log_line(TOOL_NAME ": mcsend table full, dropping RET multicast for a channel - accounting invariant violated");
      return; /* can't happen, table 2x key space */
    }
  }

  port = ff_port ? ff_port : c->port;
  m = mcast_open_send(c->family, c->group, port, t->iface, t->ttl);
  if (!m)
    return;

  if (t->entries[h].sock)
    mcast_close(t->entries[h].sock); /* stale socket from a reaped/reclaimed channel */
  t->entries[h].sock = m; /* plain write: single writer, not yet published */
  t->entries[h].generation = gen;
  atomic_store_explicit(&t->entries[h].key, c, memory_order_release);
}

mcast_t *mcsend_get(mcsend_table_t *t, channel_t *c) {
  size_t h = ptr_hash(c) & t->hash_mask;
  size_t start = h;
  unsigned gen = atomic_load_explicit(&c->generation, memory_order_relaxed);

  for (;;) {
    channel_t *cur = atomic_load_explicit(&t->entries[h].key, memory_order_acquire);
    if (cur == c)
      return (t->entries[h].generation == gen) ? t->entries[h].sock : NULL;
    if (cur == NULL)
      return NULL;
    h = (h + 1) & t->hash_mask;
    if (h == start)
      return NULL;
  }
}
