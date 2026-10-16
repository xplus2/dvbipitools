/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "../version.h"

#include "mcsend.h"

/* keys come from a fixed preallocated array: bounded key space, no tombstones needed. */
struct mcsend_slot {
  unsigned generation; /* see channel_t.generation */
  mcast_t *sock;
};

struct mcsend_entry {
  _Atomic(channel_t *) key;
  _Atomic(struct mcsend_slot *) cur; /* immutable once published */
  struct mcsend_slot *retired; /* writer only: replaced slot, closed on next replace */
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

static void slot_free(struct mcsend_slot *s) {
  if (!s)
    return;
  if (s->sock)
    mcast_close(s->sock);
  free(s);
}

void mcsend_table_free(mcsend_table_t *t) {
  if (!t)
    return;
  for (size_t i = 0; i < t->hash_size; i++) {
    struct mcsend_slot *cur = atomic_load_explicit(&t->entries[i].cur, memory_order_relaxed);
    slot_free(cur);
    slot_free(t->entries[i].retired);
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
  struct mcsend_slot *ns;
  struct mcsend_slot *old;

  for (;;) {
    channel_t *cur = atomic_load_explicit(&t->entries[h].key, memory_order_acquire);
    if (cur == c) {
      const struct mcsend_slot *sl = atomic_load_explicit(&t->entries[h].cur, memory_order_relaxed);
      if (sl && sl->generation == gen)
        return;   /* already have a current socket for this channel */
      break;      /* stale entry from a reaped/reclaimed slot: reopen below, same bucket */
    }
    if (cur == NULL)
      break; /* free bucket: not tracked yet */
    h = (h + 1) & t->hash_mask;
    if (h == start) {
      log_code("FW700137", TOOL_NAME ": mcsend table full, dropping RET multicast for a channel - accounting invariant violated");
      return; /* can't happen, table 2x key space */
    }
  }

  port = ff_port ? ff_port : c->port;
  m = mcast_open_send(c->family, c->group, port, t->iface, t->ttl);
  if (!m)
    return;
  ns = malloc(sizeof *ns);
  if (!ns) {
    mcast_close(m);
    return;
  }
  ns->sock = m;
  ns->generation = gen;

  old = atomic_load_explicit(&t->entries[h].cur, memory_order_relaxed);
  atomic_store_explicit(&t->entries[h].cur, ns, memory_order_release);
  atomic_store_explicit(&t->entries[h].key, c, memory_order_release);
  /* readers may still hold old: defer its close by one replacement */
  slot_free(t->entries[h].retired);
  t->entries[h].retired = old;
}

mcast_t *mcsend_get(const mcsend_table_t *t, const channel_t *c) {
  size_t h = ptr_hash(c) & t->hash_mask;
  size_t start = h;
  unsigned gen = atomic_load_explicit(&c->generation, memory_order_relaxed);

  for (;;) {
    channel_t *cur = atomic_load_explicit(&t->entries[h].key, memory_order_acquire);
    if (cur == c) {
      const struct mcsend_slot *sl = atomic_load_explicit(&t->entries[h].cur, memory_order_acquire);
      return (sl && sl->generation == gen) ? sl->sock : NULL;
    }
    if (cur == NULL)
      return NULL;
    h = (h + 1) & t->hash_mask;
    if (h == start)
      return NULL;
  }
}
