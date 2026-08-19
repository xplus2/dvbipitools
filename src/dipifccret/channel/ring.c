/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/demux/tspack.h"

#include "priv.h"

void ret_ring_store(channel_t *c, uint16_t seq, uint32_t timestamp, unsigned char dscp, const unsigned char *payload, size_t payload_len) {
  ret_ring_entry_t *slot;
  unsigned g;
  uint64_t words[RET_PAYLOAD_WORDS];

  memset(words, 0, sizeof words);
  memcpy(words, payload, payload_len);

  slot = &((ret_ring_entry_t *)c->ring)[seq % c->ring_size];
  g = seqlock_begin_write(&slot->gen);
  atomic_store_explicit(&slot->seq, seq, memory_order_relaxed);
  atomic_store_explicit(&slot->timestamp, timestamp, memory_order_relaxed);
  atomic_store_explicit(&slot->dscp, dscp, memory_order_relaxed);
  for (size_t i = 0; i < RET_PAYLOAD_WORDS; i++)
    atomic_store_explicit(&slot->payload[i], words[i], memory_order_relaxed);
  atomic_store_explicit(&slot->payload_len, payload_len, memory_order_relaxed);
  atomic_store_explicit(&slot->valid, 1, memory_order_relaxed);
  seqlock_commit_write(&slot->gen, g);
}

/* random_access_indicator: adaptation field byte0 bit6 (Annex I p.26 RAP def) */
static int ts_has_rai(const unsigned char *ts) {
  unsigned afc = (ts[3] >> 4) & 0x3;
  if (afc != 2 && afc != 3) /* no adaptation field */
    return 0;
  if (ts[4] == 0) /* adaptation_field_length 0, no flags byte */
    return 0;
  return (ts[5] >> 6) & 0x1;
}

/* payload assumed 188-aligned, bad sync byte skipped */
int scan_ts_packets(psi_t *psi, const unsigned char *payload, size_t payload_len) {
  int is_rap = 0;
  for (size_t off = 0; off + 188 <= payload_len; off += 188) {
    const unsigned char *ts = payload + off;
    unsigned pid;
    if (ts[0] != 0x47)
      continue;
    psi_feed(psi, ts);
    pid = tspack_pid(ts);
    if (psi_classify(psi, pid) == PID_VIDEO && ts_has_rai(ts))
      is_rap = 1;
  }
  return is_rap;
}

/* single writer only */
void rap_cache_append(rap_cache_t *rc, uint16_t seq, uint32_t timestamp, unsigned char dscp, const unsigned char *payload, size_t payload_len, int is_rap) {
  fcc_ring_entry_t *ring = (fcc_ring_entry_t *)rc->entries;
  fcc_ring_entry_t *e;
  uint64_t wc;
  uint64_t words[FCC_PAYLOAD_WORDS];
  unsigned g;

  wc = atomic_load_explicit(&rc->write_count, memory_order_relaxed);

  if (is_rap) {
    atomic_store_explicit(&rc->rap_write_count, wc, memory_order_relaxed);
    atomic_store_explicit(&rc->have_rap, 1, memory_order_release);
  }
  if (!atomic_load_explicit(&rc->have_rap, memory_order_relaxed))
    return; /* nothing cached until first RAP arrives */

  e = &ring[wc % rc->cap];
  g = seqlock_begin_write(&e->gen);
  atomic_store_explicit(&e->seq, seq, memory_order_relaxed);
  atomic_store_explicit(&e->timestamp, timestamp, memory_order_relaxed);
  atomic_store_explicit(&e->dscp, dscp, memory_order_relaxed);
  memset(words, 0, sizeof words);
  memcpy(words, payload, payload_len);
  for (size_t i = 0; i < FCC_PAYLOAD_WORDS; i++)
    atomic_store_explicit(&e->payload[i], words[i], memory_order_relaxed);
  atomic_store_explicit(&e->payload_len, payload_len, memory_order_relaxed);
  seqlock_commit_write(&e->gen, g);

  atomic_store_explicit(&rc->write_count, wc + 1, memory_order_release); /* publish last */
}

int channel_find(const channel_t *c, uint16_t seq, channel_slot_t *out) {
  const ret_ring_entry_t *slot;
  uint64_t words[RET_PAYLOAD_WORDS];

  if (c->ring_size == 0)
    return 0;
  slot = &((const ret_ring_entry_t *)c->ring)[seq % c->ring_size];

  for (int tries = 0; tries < 8; tries++) {
    unsigned g1 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    unsigned g2;
    if (g1 & 1u)
      continue; /* write in progress, retry */
    out->seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    out->timestamp = atomic_load_explicit(&slot->timestamp, memory_order_relaxed);
    out->dscp = atomic_load_explicit(&slot->dscp, memory_order_relaxed);
    for (size_t i = 0; i < RET_PAYLOAD_WORDS; i++)
      words[i] = atomic_load_explicit(&slot->payload[i], memory_order_relaxed);
    out->payload_len = atomic_load_explicit(&slot->payload_len, memory_order_relaxed);
    out->valid = atomic_load_explicit(&slot->valid, memory_order_relaxed);
    g2 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 == g2) {
      memcpy(out->payload, words, sizeof out->payload);
      return out->valid && out->seq == seq;
    }
  }
  return 0; /* repeated race treated as not-found */
}

int channel_cache_peek_meta(const channel_t *c, size_t index, rap_cache_meta_t *out) {
  const fcc_ring_entry_t *ring = (const fcc_ring_entry_t *)c->cache.entries;
  uint64_t wc, rwc, avail, start, abs_pos;
  const fcc_ring_entry_t *slot;

  if (!atomic_load_explicit(&c->cache.have_rap, memory_order_acquire))
    return 0;
  wc = atomic_load_explicit(&c->cache.write_count, memory_order_acquire);
  rwc = atomic_load_explicit(&c->cache.rap_write_count, memory_order_relaxed);
  avail = wc - rwc;
  if (avail > c->cache.cap)
    avail = c->cache.cap;
  if (index >= avail)
    return 0;

  start = wc - avail;
  abs_pos = start + index;
  slot = &ring[abs_pos % c->cache.cap];

  for (int tries = 0; tries < 8; tries++) {
    unsigned g1 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    unsigned g2;
    if (g1 & 1u)
      continue; /* write in progress, retry */
    out->seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    out->timestamp = atomic_load_explicit(&slot->timestamp, memory_order_relaxed);
    g2 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 == g2)
      return 1;
  }
  return 0; /* repeated race treated as not-found */
}

int channel_has_rap(const channel_t *c) {
  return atomic_load_explicit(&c->cache.have_rap, memory_order_acquire);
}

size_t channel_cache_count(const channel_t *c) {
  uint64_t wc, rwc, avail;

  if (!atomic_load_explicit(&c->cache.have_rap, memory_order_acquire))
    return 0;
  wc = atomic_load_explicit(&c->cache.write_count, memory_order_acquire);
  rwc = atomic_load_explicit(&c->cache.rap_write_count, memory_order_relaxed);
  avail = wc - rwc;
  if (avail > c->cache.cap)
    avail = c->cache.cap; /* safety-cap truncation: true RAP already overwritten */
  return (size_t)avail;
}

int channel_cache_get(const channel_t *c, size_t index, rap_cache_entry_t *out) {
  const fcc_ring_entry_t *ring = (const fcc_ring_entry_t *)c->cache.entries;
  uint64_t wc, rwc, avail, start, abs_pos;
  const fcc_ring_entry_t *slot;
  uint64_t words[FCC_PAYLOAD_WORDS];

  if (!atomic_load_explicit(&c->cache.have_rap, memory_order_acquire))
    return 0;
  wc = atomic_load_explicit(&c->cache.write_count, memory_order_acquire);
  rwc = atomic_load_explicit(&c->cache.rap_write_count, memory_order_relaxed);
  avail = wc - rwc;
  if (avail > c->cache.cap)
    avail = c->cache.cap;
  if (index >= avail)
    return 0;

  start = wc - avail;
  abs_pos = start + index;
  slot = &ring[abs_pos % c->cache.cap];

  for (int tries = 0; tries < 8; tries++) {
    unsigned g1 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    unsigned g2;
    if (g1 & 1u)
      continue; /* write in progress, retry */
    out->seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    out->timestamp = atomic_load_explicit(&slot->timestamp, memory_order_relaxed);
    out->dscp = atomic_load_explicit(&slot->dscp, memory_order_relaxed);
    for (size_t i = 0; i < FCC_PAYLOAD_WORDS; i++)
      words[i] = atomic_load_explicit(&slot->payload[i], memory_order_relaxed);
    out->payload_len = atomic_load_explicit(&slot->payload_len, memory_order_relaxed);
    g2 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 == g2) {
      memcpy(out->payload, words, sizeof out->payload);
      return 1;
    }
  }
  return 0; /* repeated race treated as not-found */
}
