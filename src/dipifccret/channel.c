/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/tspack.h"
#include "lib/log.h"

#include "channel.h"
#include "version.h"

/* single writer (capture thread) for lookup/store/reap; lock-free atomic reads elsewhere.
   two separate rings on purpose: RET is seq-keyed, FCC is RAP-anchored linear-from-RAP. */

#define RET_PAYLOAD_WORDS (CHANNEL_MAX_PAYLOAD / 8)
#define FCC_PAYLOAD_WORDS (CHANNEL_MAX_PAYLOAD / 8)

typedef struct {
  _Atomic unsigned gen; /* seqlock: odd = write in progress, even = stable */
  _Atomic uint16_t seq;
  _Atomic uint32_t timestamp;
  _Atomic uint64_t payload[RET_PAYLOAD_WORDS];
  _Atomic size_t payload_len;
  _Atomic int valid;
} ret_ring_entry_t;

typedef struct {
  _Atomic unsigned gen; /* odd = write in progress, even = stable */
  _Atomic uint16_t seq;
  _Atomic uint32_t timestamp;
  _Atomic uint64_t payload[FCC_PAYLOAD_WORDS];
  _Atomic size_t payload_len;
} fcc_ring_entry_t;

struct channel_table {
  channel_t *chan; /* fixed array, size max_channels, preallocated once */
  size_t max_channels;
  size_t ring_slots;
  size_t cache_cap;

  /* open-addr index: (family,group,port)->slot+1. 0=empty, TOMBSTONE=reaped. rebuild >75% load. */
  size_t *hash;
  size_t hash_size;
  size_t hash_mask;
  size_t hash_used;

  /* open-addr index: ssrc->slot+1, same conventions. channel_find_by_ssrc's NACK-path lookup. */
  size_t *ssrc_hash;
  size_t ssrc_hash_size;
  size_t ssrc_hash_mask;
  size_t ssrc_hash_used;

  pthread_mutex_t lock; /* guards hash/hash_used and ssrc_hash/ssrc_hash_used above */

  size_t reap_cursor; /* channel_table_reap_step()'s scan position, wraps at max_channels */
};

#define CHANNEL_HASH_TOMBSTONE SIZE_MAX

static size_t next_pow2(size_t n) {
  size_t p = 1;
  while (p < n)
    p <<= 1;
  return p;
}

static size_t chan_key_hash(int family, const void *addr, size_t addr_len, unsigned port) {
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
static void chan_hash_rebuild(channel_table_t *t) {
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
static channel_t *chan_hash_probe(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port, size_t *avail) {
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

static size_t chan_ssrc_hash(uint32_t ssrc) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64-bit offset basis */
  h ^= ssrc;
  h *= 1099511628211ULL; /* FNV prime */
  return (size_t)h;
}

/* drops all tombstones, reinserts every ssrc-known live entry */
static void ssrc_hash_rebuild(channel_table_t *t) {
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
static void ssrc_hash_remove(channel_table_t *t, size_t slot_idx, uint32_t ssrc) {
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
static void ssrc_hash_insert(channel_table_t *t, size_t slot_idx, uint32_t ssrc) {
  size_t h = chan_ssrc_hash(ssrc) & t->ssrc_hash_mask;
  int was_empty;

  while (t->ssrc_hash[h] != 0 && t->ssrc_hash[h] != CHANNEL_HASH_TOMBSTONE)
    h = (h + 1) & t->ssrc_hash_mask;
  was_empty = (t->ssrc_hash[h] == 0);
  t->ssrc_hash[h] = slot_idx + 1;
  if (was_empty && ++t->ssrc_hash_used > (t->ssrc_hash_size / 4) * 3)
    ssrc_hash_rebuild(t);
}

static void ret_ring_store(channel_t *c, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len) {
  ret_ring_entry_t *slot;
  unsigned g;
  uint64_t words[RET_PAYLOAD_WORDS];
  size_t i;

  memset(words, 0, sizeof words);
  memcpy(words, payload, payload_len);

  slot = &((ret_ring_entry_t *)c->ring)[seq % c->ring_size];
  g = atomic_load_explicit(&slot->gen, memory_order_relaxed);
  atomic_store_explicit(&slot->gen, g + 1, memory_order_relaxed); /* odd: write starting */
  atomic_store_explicit(&slot->seq, seq, memory_order_relaxed);
  atomic_store_explicit(&slot->timestamp, timestamp, memory_order_relaxed);
  for (i = 0; i < RET_PAYLOAD_WORDS; i++)
    atomic_store_explicit(&slot->payload[i], words[i], memory_order_relaxed);
  atomic_store_explicit(&slot->payload_len, payload_len, memory_order_relaxed);
  atomic_store_explicit(&slot->valid, 1, memory_order_relaxed);
  atomic_store_explicit(&slot->gen, g + 2, memory_order_release); /* even: write done, publishes everything above */
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
static int scan_ts_packets(psi_t *psi, const unsigned char *payload, size_t payload_len) {
  size_t off;
  int is_rap = 0;
  for (off = 0; off + 188 <= payload_len; off += 188) {
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
static void rap_cache_append(rap_cache_t *rc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, int is_rap) {
  fcc_ring_entry_t *ring = (fcc_ring_entry_t *)rc->entries;
  fcc_ring_entry_t *e;
  uint64_t wc;
  uint64_t words[FCC_PAYLOAD_WORDS];
  unsigned g;
  size_t i;

  wc = atomic_load_explicit(&rc->write_count, memory_order_relaxed);

  if (is_rap) {
    atomic_store_explicit(&rc->rap_write_count, wc, memory_order_relaxed);
    atomic_store_explicit(&rc->have_rap, 1, memory_order_release);
  }
  if (!atomic_load_explicit(&rc->have_rap, memory_order_relaxed))
    return; /* nothing cached until first RAP arrives */

  e = &ring[wc % rc->cap];
  g = atomic_load_explicit(&e->gen, memory_order_relaxed);
  atomic_store_explicit(&e->gen, g + 1, memory_order_relaxed); /* odd: write starting */
  atomic_store_explicit(&e->seq, seq, memory_order_relaxed);
  atomic_store_explicit(&e->timestamp, timestamp, memory_order_relaxed);
  memset(words, 0, sizeof words);
  memcpy(words, payload, payload_len);
  for (i = 0; i < FCC_PAYLOAD_WORDS; i++)
    atomic_store_explicit(&e->payload[i], words[i], memory_order_relaxed);
  atomic_store_explicit(&e->payload_len, payload_len, memory_order_relaxed);
  atomic_store_explicit(&e->gen, g + 2, memory_order_release); /* even: write done, publishes everything above */

  atomic_store_explicit(&rc->write_count, wc + 1, memory_order_release); /* publish last */
}

channel_table_t *channel_table_new(size_t max_channels, size_t ring_slots, size_t cache_cap) {
  channel_table_t *t;
  size_t i;

  if (max_channels == 0)
    max_channels = CHANNEL_DEFAULT_MAX;
  t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->max_channels = max_channels;
  t->ring_slots = ring_slots;
  t->cache_cap = cache_cap;
  t->chan = calloc(max_channels, sizeof(channel_t));
  if (!t->chan) {
    free(t);
    return NULL;
  }
  t->hash_size = next_pow2(max_channels * 2);
  if (t->hash_size < 4)
    t->hash_size = 4;
  t->hash_mask = t->hash_size - 1;
  t->hash = calloc(t->hash_size, sizeof *t->hash);
  if (!t->hash) {
    free(t->chan);
    free(t);
    return NULL;
  }
  t->ssrc_hash_size = t->hash_size;
  t->ssrc_hash_mask = t->hash_mask;
  t->ssrc_hash = calloc(t->ssrc_hash_size, sizeof *t->ssrc_hash);
  if (!t->ssrc_hash) {
    free(t->hash);
    free(t->chan);
    free(t);
    return NULL;
  }
  for (i = 0; i < max_channels; i++) {
    if (ring_slots > 0) {
      t->chan[i].ring = calloc(ring_slots, sizeof(ret_ring_entry_t));
      if (!t->chan[i].ring)
        goto fail;
    }
    t->chan[i].ring_size = ring_slots;
    if (cache_cap > 0) {
      t->chan[i].cache.entries = calloc(cache_cap, sizeof(fcc_ring_entry_t));
      if (!t->chan[i].cache.entries)
        goto fail;
    }
    t->chan[i].cache.cap = cache_cap;
  }
  pthread_mutex_init(&t->lock, NULL);
  return t;

fail: {
    size_t j;
    for (j = 0; j <= i; j++) {
      free(t->chan[j].ring);
      free(t->chan[j].cache.entries);
    }
    free(t->ssrc_hash);
    free(t->hash);
    free(t->chan);
    free(t);
    return NULL;
  }
}

void channel_table_free(channel_table_t *t) {
  size_t i;
  if (!t)
    return;
  for (i = 0; i < t->max_channels; i++) {
    if (t->chan[i].psi)
      psi_free(t->chan[i].psi);
    free(t->chan[i].ring);
    free(t->chan[i].cache.entries);
  }
  pthread_mutex_destroy(&t->lock);
  free(t->ssrc_hash);
  free(t->hash);
  free(t->chan);
  free(t);
}

/* hash[] is non-atomic shared state: lock covers probe+claim+insert */
channel_t *channel_lookup(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port) {
  size_t i, avail;
  channel_t *result;

  pthread_mutex_lock(&t->lock);
  result = chan_hash_probe(t, family, addr, addr_len, port, &avail);

  for (i = 0; !result && i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&c->in_use, &expected, 2, memory_order_acq_rel, memory_order_relaxed))
      continue;

    {
      void *saved_ring = c->ring;
      size_t saved_ring_size = c->ring_size;
      void *saved_cache_entries = c->cache.entries;
      size_t saved_cache_cap = c->cache.cap;
      unsigned saved_generation = atomic_load_explicit(&c->generation, memory_order_relaxed);
      int was_empty = (t->hash[avail] == 0);

      if (c->psi)
        psi_free(c->psi);
      memset(c, 0, sizeof *c);
      c->ring = saved_ring;
      c->ring_size = saved_ring_size;
      c->cache.entries = saved_cache_entries;
      c->cache.cap = saved_cache_cap;
      atomic_store_explicit(&c->generation, saved_generation + 1, memory_order_relaxed);

      if (saved_ring) {
        ret_ring_entry_t *ring = (ret_ring_entry_t *)saved_ring;
        size_t j;
        for (j = 0; j < saved_ring_size; j++) {
          atomic_store_explicit(&ring[j].valid, 0, memory_order_relaxed);
          atomic_store_explicit(&ring[j].gen, 0, memory_order_relaxed);
        }
      }
      /* FCC cache needs no explicit reset: have_rap/write_count zeroed by memset above */

      c->family = family;
      c->addr_len = addr_len;
      memcpy(c->addr, addr, addr_len);
      if (!inet_ntop(family, addr, c->group, sizeof c->group))
        c->group[0] = '\0';
      c->port = port;
      atomic_store_explicit(&c->last_seen, time(NULL), memory_order_relaxed);
      atomic_store_explicit(&c->in_use, 1, memory_order_release);

      t->hash[avail] = i + 1;
      if (was_empty && ++t->hash_used > (t->hash_size / 4) * 3)
        chan_hash_rebuild(t);

      result = c;
    }
  }

  if (!result) {
    char addrbuf[64];
    if (!inet_ntop(family, addr, addrbuf, sizeof addrbuf))
      snprintf(addrbuf, sizeof addrbuf, "?");
    log_line(TOOL_NAME ": max-channels (%zu) reached, rejecting %s:%u", t->max_channels, addrbuf, port);
  }
  pthread_mutex_unlock(&t->lock);
  return result;
}

channel_t *channel_find_by_ssrc(channel_table_t *t, uint32_t ssrc) {
  size_t h;
  channel_t *result = NULL;

  pthread_mutex_lock(&t->lock);
  h = chan_ssrc_hash(ssrc) & t->ssrc_hash_mask;
  for (;;) {
    size_t slot_plus1 = t->ssrc_hash[h];
    if (slot_plus1 == 0)
      break;
    if (slot_plus1 != CHANNEL_HASH_TOMBSTONE) {
      channel_t *c = &t->chan[slot_plus1 - 1];
      if (atomic_load_explicit(&c->in_use, memory_order_acquire) == 1 && atomic_load_explicit(&c->ssrc_known, memory_order_acquire) && atomic_load_explicit(&c->ssrc, memory_order_acquire) == ssrc) {
        result = c;
        break;
      }
    }
    h = (h + 1) & t->ssrc_hash_mask;
  }
  pthread_mutex_unlock(&t->lock);
  return result;
}

void channel_store(channel_table_t *t, channel_t *c, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len) {
  time_t now = time(NULL);
  time_t elapsed;
  uint32_t old_ssrc = atomic_load_explicit(&c->ssrc, memory_order_relaxed);
  int had_ssrc = atomic_load_explicit(&c->ssrc_known, memory_order_relaxed);

  if (payload_len > CHANNEL_MAX_PAYLOAD) {
    if (!c->oversized_logged) {
      log_line(TOOL_NAME ": %s:%u payload %zu exceeds cap (%d), dropping until it fits again", c->group, c->port, payload_len, CHANNEL_MAX_PAYLOAD);
      c->oversized_logged = 1;
    }
    return;
  }
  c->oversized_logged = 0;

  if (!had_ssrc || old_ssrc != ssrc) {
    size_t slot_idx = (size_t)(c - t->chan);
    pthread_mutex_lock(&t->lock);
    if (had_ssrc)
      ssrc_hash_remove(t, slot_idx, old_ssrc);
    ssrc_hash_insert(t, slot_idx, ssrc);
    pthread_mutex_unlock(&t->lock);
  }

  atomic_store_explicit(&c->ssrc, ssrc, memory_order_relaxed);
  atomic_store_explicit(&c->ssrc_known, 1, memory_order_release);
  atomic_store_explicit(&c->last_seen, now, memory_order_relaxed);

  if (c->bitrate_window_start == 0)
    c->bitrate_window_start = now;
  c->bitrate_window_bytes += payload_len;

  elapsed = now - c->bitrate_window_start;
  if (elapsed >= CHANNEL_BITRATE_WINDOW_S) {
    double bps = (double)c->bitrate_window_bytes * 8.0 / (double)elapsed;
    atomic_store_explicit(&c->nominal_bps, bps, memory_order_relaxed);
    c->bitrate_window_bytes = 0;
    c->bitrate_window_start = now;
  }

  if (c->ring_size > 0)
    ret_ring_store(c, seq, timestamp, payload, payload_len);

  if (c->cache.cap > 0) {
    if (!c->psi) {
      c->psi = psi_new();
      if (!c->psi)
        log_line(TOOL_NAME ": out of memory allocating psi state for %s:%u, RAP detection disabled for this packet", c->group, c->port);
    }
    if (c->psi) {
      int is_rap = scan_ts_packets(c->psi, payload, payload_len);
      rap_cache_append(&c->cache, seq, timestamp, payload, payload_len, is_rap);
    }
  }
}

int channel_find(const channel_t *c, uint16_t seq, channel_slot_t *out) {
  const ret_ring_entry_t *slot;
  unsigned g1, g2;
  int tries;
  uint64_t words[RET_PAYLOAD_WORDS];
  size_t i;

  if (c->ring_size == 0)
    return 0;
  slot = &((const ret_ring_entry_t *)c->ring)[seq % c->ring_size];

  for (tries = 0; tries < 8; tries++) {
    g1 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 & 1u)
      continue; /* write in progress, retry */
    out->seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    out->timestamp = atomic_load_explicit(&slot->timestamp, memory_order_relaxed);
    for (i = 0; i < RET_PAYLOAD_WORDS; i++)
      words[i] = atomic_load_explicit(&slot->payload[i], memory_order_relaxed);
    out->payload_len = atomic_load_explicit(&slot->payload_len, memory_order_relaxed);
    out->valid = atomic_load_explicit(&slot->valid, memory_order_relaxed);
    g2 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 == g2) {
      memcpy(out->payload, words, sizeof out->payload);
      return out->valid && out->seq == seq;
    }
  }
  return 0; /* repeatedly raced a concurrent write; treat as not-found, same as a real miss */
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
  unsigned g1, g2;
  int tries;
  uint64_t words[FCC_PAYLOAD_WORDS];
  size_t i;

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

  for (tries = 0; tries < 8; tries++) {
    g1 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 & 1u)
      continue; /* write in progress, retry */
    out->seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    out->timestamp = atomic_load_explicit(&slot->timestamp, memory_order_relaxed);
    for (i = 0; i < FCC_PAYLOAD_WORDS; i++)
      words[i] = atomic_load_explicit(&slot->payload[i], memory_order_relaxed);
    out->payload_len = atomic_load_explicit(&slot->payload_len, memory_order_relaxed);
    g2 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 == g2) {
      memcpy(out->payload, words, sizeof out->payload);
      return 1;
    }
  }
  return 0; /* repeatedly raced concurrent write; treat as not-found, same as real miss */
}

/* checks+reclaims one slot if stale. caller holds t->lock */
static void reap_slot(channel_table_t *t, size_t i, time_t now, time_t max_age_s) {
  channel_t *c = &t->chan[i];
  time_t last;

  if (atomic_load_explicit(&c->in_use, memory_order_acquire) != 1)
    return;
  last = atomic_load_explicit(&c->last_seen, memory_order_relaxed);
  if (now - last <= max_age_s)
    return;
  {
    size_t h = chan_key_hash(c->family, c->addr, c->addr_len, c->port) & t->hash_mask;
    for (;;) {
      size_t slot_plus1 = t->hash[h];
      if (slot_plus1 == 0)
        break; /* shouldn't happen: an in-use channel always has a hash entry */
      if (slot_plus1 == i + 1) {
        t->hash[h] = CHANNEL_HASH_TOMBSTONE;
        break;
      }
      h = (h + 1) & t->hash_mask;
    }
  }
  if (atomic_load_explicit(&c->ssrc_known, memory_order_relaxed))
    ssrc_hash_remove(t, i, atomic_load_explicit(&c->ssrc, memory_order_relaxed));
  atomic_store_explicit(&c->in_use, 0, memory_order_release);
}

void channel_table_reap(channel_table_t *t, time_t max_age_s) {
  time_t now = time(NULL);
  size_t i;
  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->max_channels; i++)
    reap_slot(t, i, now, max_age_s);
  pthread_mutex_unlock(&t->lock);
}

/* amortized reap: at most max_scan slots per call, from an internal wrapping cursor.
   caller drives it often (e.g. once per packet) with a small max_scan, so no single call ever costs O(max_channels).  */
void channel_table_reap_step(channel_table_t *t, time_t max_age_s, size_t max_scan) {
  time_t now = time(NULL);
  size_t i, n;
  pthread_mutex_lock(&t->lock);
  n = max_scan < t->max_channels ? max_scan : t->max_channels;
  for (i = 0; i < n; i++) {
    reap_slot(t, t->reap_cursor, now, max_age_s);
    t->reap_cursor = (t->reap_cursor + 1) % t->max_channels;
  }
  pthread_mutex_unlock(&t->lock);
}
