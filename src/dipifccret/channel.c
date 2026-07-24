/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

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
};

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

static unsigned ts_pid(const unsigned char *ts) {
  return (((unsigned)ts[1] & 0x1F) << 8) | ts[2];
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
    pid = ts_pid(ts);
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
  return t;

fail: {
    size_t j;
    for (j = 0; j <= i; j++) {
      free(t->chan[j].ring);
      free(t->chan[j].cache.entries);
    }
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
  free(t->chan);
  free(t);
}

channel_t *channel_lookup(channel_table_t *t, int family, const char *group, unsigned port) {
  size_t i;

  for (i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    if (atomic_load_explicit(&c->in_use, memory_order_acquire) == 1 && c->family == family && c->port == port && strcmp(c->group, group) == 0)
      return c;
  }

  for (i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(&c->in_use, &expected, 2, memory_order_acq_rel, memory_order_relaxed)) {
      void *saved_ring = c->ring;
      size_t saved_ring_size = c->ring_size;
      void *saved_cache_entries = c->cache.entries;
      size_t saved_cache_cap = c->cache.cap;

      if (c->psi)
        psi_free(c->psi);
      memset(c, 0, sizeof *c);
      c->ring = saved_ring;
      c->ring_size = saved_ring_size;
      c->cache.entries = saved_cache_entries;
      c->cache.cap = saved_cache_cap;

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
      strncpy(c->group, group, sizeof c->group - 1);
      c->group[sizeof c->group - 1] = '\0';
      c->port = port;
      atomic_store_explicit(&c->last_seen, time(NULL), memory_order_relaxed);
      atomic_store_explicit(&c->in_use, 1, memory_order_release);
      return c;
    }
  }

  log_line(TOOL_NAME ": max-channels (%zu) reached, rejecting %s:%u", t->max_channels, group, port);
  return NULL;
}

channel_t *channel_find_by_ssrc(channel_table_t *t, uint32_t ssrc) {
  size_t i;
  for (i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    if (atomic_load_explicit(&c->in_use, memory_order_acquire) == 1 && atomic_load_explicit(&c->ssrc_known, memory_order_acquire) && atomic_load_explicit(&c->ssrc, memory_order_acquire) == ssrc)
      return c;
  }
  return NULL;
}

void channel_store(channel_t *c, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len) {
  time_t now = time(NULL);
  time_t elapsed;

  if (payload_len > CHANNEL_MAX_PAYLOAD)
    return;

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
    int is_rap;
    if (!c->psi)
      c->psi = psi_new();
    is_rap = scan_ts_packets(c->psi, payload, payload_len);
    rap_cache_append(&c->cache, seq, timestamp, payload, payload_len, is_rap);
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

void channel_table_reap(channel_table_t *t, time_t max_age_s) {
  time_t now = time(NULL);
  size_t i;
  for (i = 0; i < t->max_channels; i++) {
    channel_t *c = &t->chan[i];
    if (atomic_load_explicit(&c->in_use, memory_order_acquire) == 1) {
      time_t last = atomic_load_explicit(&c->last_seen, memory_order_relaxed);
      if (now - last > max_age_s)
        atomic_store_explicit(&c->in_use, 0, memory_order_release);
    }
  }
}
