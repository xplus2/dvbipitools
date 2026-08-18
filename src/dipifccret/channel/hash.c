/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "priv.h"

size_t chan_key_hash(int family, const void *addr, size_t addr_len, unsigned port) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
  const unsigned char *p = (const unsigned char *)addr;
  size_t i;
  for (i = 0; i < addr_len; i++) {
    h ^= p[i];
    h *= 1099511628211ULL; /* FNV prime */
  }
  h ^= (unsigned)family;
  h *= 1099511628211ULL;
  h ^= port;
  h *= 1099511628211ULL;
  return (size_t)h;
}

/* drops all tombstones, reinserts live entries only */
void chan_hash_rebuild(channel_table_t *t) {
  size_t i, live = 0;

  memset(t->hash, 0, t->hash_size * sizeof *t->hash);
  for (i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    size_t h;
    if (atomic_load_explicit(&c->in_use, memory_order_relaxed) != 1)
      continue;
    h = chan_key_hash(c->family, c->addr, c->addr_len, c->port) & t->hash_mask;
    while (t->hash[h] != 0)
      h = (h + 1) & t->hash_mask;
    t->hash[h] = i + 1;
    live++;
  }
  t->hash_used = live;
}

/* match, or NULL + *avail = insertion bucket. stops only at empty, not tombstone. */
channel_t *chan_hash_probe(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port, size_t *avail) {
  size_t h = chan_key_hash(family, addr, addr_len, port) & t->hash_mask;
  int have_avail = 0;

  for (;;) {
    size_t slot_plus1 = t->hash[h];
    if (slot_plus1 == 0) {
      if (!have_avail)
        *avail = h;
      return NULL;
    }
    if (slot_plus1 == CHANNEL_HASH_TOMBSTONE) {
      if (!have_avail) {
        *avail = h;
        have_avail = 1;
      }
    } else {
      channel_t *c = &t->chan[slot_plus1 - 1];
      if (atomic_load_explicit(&c->in_use, memory_order_acquire) == 1 && c->family == family && c->port == port && c->addr_len == addr_len && memcmp(c->addr, addr, addr_len) == 0)
        return c;
    }
    h = (h + 1) & t->hash_mask;
  }
}

size_t chan_ssrc_hash(uint32_t ssrc) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
  h ^= ssrc;
  h *= 1099511628211ULL; /* FNV prime */
  return (size_t)h;
}

/* drops all tombstones, reinserts every ssrc-known live entry */
void ssrc_hash_rebuild(channel_table_t *t) {
  size_t i, live = 0;

  memset(t->ssrc_hash, 0, t->ssrc_hash_size * sizeof *t->ssrc_hash);
  for (i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    size_t h;
    if (atomic_load_explicit(&c->in_use, memory_order_relaxed) != 1 || !atomic_load_explicit(&c->ssrc_known, memory_order_relaxed))
      continue;
    h = chan_ssrc_hash(atomic_load_explicit(&c->ssrc, memory_order_relaxed)) & t->ssrc_hash_mask;
    while (t->ssrc_hash[h] != 0)
      h = (h + 1) & t->ssrc_hash_mask;
    t->ssrc_hash[h] = i + 1;
    live++;
  }
  t->ssrc_hash_used = live;
}

/* caller holds t->lock. tombstones slot_idx's entry for ssrc, if still present */
void ssrc_hash_remove(channel_table_t *t, size_t slot_idx, uint32_t ssrc) {
  size_t h = chan_ssrc_hash(ssrc) & t->ssrc_hash_mask;
  for (;;) {
    size_t slot_plus1 = t->ssrc_hash[h];
    if (slot_plus1 == 0)
      return; /* already gone, e.g. a rebuild happened since */
    if (slot_plus1 == slot_idx + 1) {
      t->ssrc_hash[h] = CHANNEL_HASH_TOMBSTONE;
      return;
    }
    h = (h + 1) & t->ssrc_hash_mask;
  }
}

/* caller holds t->lock */
void ssrc_hash_insert(channel_table_t *t, size_t slot_idx, uint32_t ssrc) {
  size_t h = chan_ssrc_hash(ssrc) & t->ssrc_hash_mask;
  int was_empty;

  while (t->ssrc_hash[h] != 0 && t->ssrc_hash[h] != CHANNEL_HASH_TOMBSTONE)
    h = (h + 1) & t->ssrc_hash_mask;
  was_empty = (t->ssrc_hash[h] == 0);
  t->ssrc_hash[h] = slot_idx + 1;
  if (was_empty) {
    t->ssrc_hash_used++;
    if (t->ssrc_hash_used > (t->ssrc_hash_size / 4) * 3)
      ssrc_hash_rebuild(t);
  }
}
