/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "priv.h"

size_t chan_key_hash(int family, const void *addr, size_t addr_len, unsigned port) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
  const unsigned char *p = (const unsigned char *)addr;
  for (size_t i = 0; i < addr_len; i++) {
    h ^= p[i];
    h *= 1099511628211ULL; /* FNV prime */
  }
  h ^= (unsigned)family;
  h *= 1099511628211ULL;
  h ^= port;
  h *= 1099511628211ULL;
  return (size_t)h;
}

/* rebuild vs single-bucket CAS exclusion (find_or_claim, reap_slot): busy-wait, rebuild rare */
void hash_writer_enter(channel_table_t *t) {
  for (;;) {
    atomic_fetch_add_explicit(&t->hash_active_writers, 1, memory_order_acq_rel);
    if (!atomic_load_explicit(&t->hash_rebuild_active, memory_order_acquire))
      return;
    atomic_fetch_sub_explicit(&t->hash_active_writers, 1, memory_order_acq_rel);
    while (atomic_load_explicit(&t->hash_rebuild_active, memory_order_acquire))
      ;
  }
}

void hash_writer_exit(channel_table_t *t) {
  atomic_fetch_sub_explicit(&t->hash_active_writers, 1, memory_order_acq_rel);
}

/* drops tombstones, reinserts live entries. single entrant via CAS below, others no-op.
   drains in-flight single-bucket ops first. */
void chan_hash_rebuild(channel_table_t *t) {
  int expected = 0;
  size_t live = 0;

  if (!atomic_compare_exchange_strong_explicit(&t->hash_rebuild_active, &expected, 1, memory_order_acq_rel, memory_order_relaxed))
    return;
  while (atomic_load_explicit(&t->hash_active_writers, memory_order_acquire) != 0)
    ;

  for (size_t h = 0; h < t->hash_size; h++)
    atomic_store_explicit(&t->hash[h], 0, memory_order_relaxed);
  for (size_t i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    size_t h;
    if (atomic_load_explicit(&c->in_use, memory_order_relaxed) != 1)
      continue;
    h = chan_key_hash(c->family, c->addr, c->addr_len, c->port) & t->hash_mask;
    while (atomic_load_explicit(&t->hash[h], memory_order_relaxed) != 0)
      h = (h + 1) & t->hash_mask;
    atomic_store_explicit(&t->hash[h], i + 1, memory_order_relaxed);
    live++;
  }
  atomic_store_explicit(&t->hash_used, live, memory_order_relaxed);
  atomic_store_explicit(&t->hash_rebuild_active, 0, memory_order_release);
}

/* full restart on lost claim CAS: bucket state may have moved meanwhile */
channel_t *chan_hash_find_or_claim(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port, size_t *claimed_h, int *was_tombstone) {
  size_t start = chan_key_hash(family, addr, addr_len, port) & t->hash_mask;
  size_t h = start;
  size_t avail = 0;
  int have_avail = 0;

  for (;;) {
    size_t v = atomic_load_explicit(&t->hash[h], memory_order_acquire);
    if (v == 0) {
      size_t claim_h = have_avail ? avail : h;
      size_t expected = have_avail ? CHANNEL_HASH_TOMBSTONE : 0;
      if (atomic_compare_exchange_strong_explicit(&t->hash[claim_h], &expected, CHANNEL_HASH_CLAIMING, memory_order_acq_rel, memory_order_relaxed)) {
        *claimed_h = claim_h;
        *was_tombstone = have_avail;
        return NULL;
      }
      h = start; /* claim CAS lost, restart: bucket state may have moved */
      avail = 0;
      have_avail = 0;
      continue;
    }
    if (v == CHANNEL_HASH_TOMBSTONE) {
      if (!have_avail) {
        avail = h;
        have_avail = 1;
      }
    } else if (v == CHANNEL_HASH_CLAIMING) {
      continue; /* spin same bucket till resolved */
    } else {
      channel_t *c = &t->chan[v - 1];
      if (atomic_load_explicit(&c->in_use, memory_order_acquire) == 1 && c->family == family && c->port == port && c->addr_len == addr_len && memcmp(c->addr, addr, addr_len) == 0)
        return c;
    }
    h = (h + 1) & t->hash_mask;
  }
}

void chan_hash_publish_claim(channel_table_t *t, size_t h, int was_tombstone, size_t slot_idx) {
  atomic_store_explicit(&t->hash[h], slot_idx + 1, memory_order_release);
  if (!was_tombstone)
    atomic_fetch_add_explicit(&t->hash_used, 1, memory_order_relaxed);
}

void chan_hash_rollback_claim(channel_table_t *t, size_t h, int was_tombstone) {
  atomic_store_explicit(&t->hash[h], was_tombstone ? CHANNEL_HASH_TOMBSTONE : 0, memory_order_release);
}

size_t chan_ssrc_hash(uint32_t ssrc) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
  h ^= ssrc;
  h *= 1099511628211ULL; /* FNV prime */
  return (size_t)h;
}

/* drops all tombstones, reinserts every ssrc-known live entry */
void ssrc_hash_rebuild(channel_table_t *t) {
  size_t live = 0;

  for (size_t h = 0; h < t->ssrc_hash_size; h++)
    atomic_store_explicit(&t->ssrc_hash[h], 0, memory_order_relaxed);
  for (size_t i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    size_t h;
    if (atomic_load_explicit(&c->in_use, memory_order_relaxed) != 1 || !atomic_load_explicit(&c->ssrc_known, memory_order_relaxed))
      continue;
    h = chan_ssrc_hash(atomic_load_explicit(&c->ssrc, memory_order_relaxed)) & t->ssrc_hash_mask;
    while (atomic_load_explicit(&t->ssrc_hash[h], memory_order_relaxed) != 0)
      h = (h + 1) & t->ssrc_hash_mask;
    atomic_store_explicit(&t->ssrc_hash[h], i + 1, memory_order_relaxed);
    live++;
  }
  t->ssrc_hash_used = live;
}

/* caller wraps in ssrc_gen seqlock. tombstones slot_idx's entry for ssrc, if present */
void ssrc_hash_remove(channel_table_t *t, size_t slot_idx, uint32_t ssrc) {
  size_t h = chan_ssrc_hash(ssrc) & t->ssrc_hash_mask;
  for (;;) {
    size_t slot_plus1 = atomic_load_explicit(&t->ssrc_hash[h], memory_order_relaxed);
    if (slot_plus1 == 0)
      return; /* already gone, e.g. a rebuild happened since */
    if (slot_plus1 == slot_idx + 1) {
      atomic_store_explicit(&t->ssrc_hash[h], CHANNEL_HASH_TOMBSTONE, memory_order_relaxed);
      return;
    }
    h = (h + 1) & t->ssrc_hash_mask;
  }
}

/* caller wraps in ssrc_gen seqlock */
void ssrc_hash_insert(channel_table_t *t, size_t slot_idx, uint32_t ssrc) {
  size_t h = chan_ssrc_hash(ssrc) & t->ssrc_hash_mask;
  int was_empty;
  size_t cur;

  for (;;) {
    cur = atomic_load_explicit(&t->ssrc_hash[h], memory_order_relaxed);
    if (cur == 0 || cur == CHANNEL_HASH_TOMBSTONE)
      break;
    h = (h + 1) & t->ssrc_hash_mask;
  }
  was_empty = (cur == 0);
  atomic_store_explicit(&t->ssrc_hash[h], slot_idx + 1, memory_order_relaxed);
  if (was_empty) {
    t->ssrc_hash_used++;
    if (t->ssrc_hash_used > (t->ssrc_hash_size / 4) * 3)
      ssrc_hash_rebuild(t);
  }
}
