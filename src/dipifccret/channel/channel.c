/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/tspack.h"
#include "lib/log.h"

#include "../version.h"
#include "priv.h"

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
  t->resolve_hash = calloc(max_channels, sizeof *t->resolve_hash);
  if (!t->resolve_hash) {
    free(t->ssrc_hash);
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
    pthread_mutex_init(&t->chan[i].hned_lock, NULL);
  }
  return t;

fail: {
    for (size_t j = 0; j <= i; j++) {
      free(t->chan[j].ring);
      free(t->chan[j].cache.entries);
      if (j < i)
        pthread_mutex_destroy(&t->chan[j].hned_lock); /* slot i: hned_lock not yet init'd */
    }
    free(t->resolve_hash);
    free(t->ssrc_hash);
    free(t->hash);
    free(t->chan);
    free(t);
    return NULL;
  }
}

void channel_table_free(channel_table_t *t) {
  if (!t)
    return;
  for (size_t i = 0; i < t->max_channels; i++) {
    if (t->chan[i].psi)
      psi_free(t->chan[i].psi);
    free(t->chan[i].ring);
    free(t->chan[i].cache.entries);
    pthread_mutex_destroy(&t->chan[i].hned_lock);
  }
  free(t->resolve_hash);
  free(t->ssrc_hash);
  free(t->hash);
  free(t->chan);
  free(t);
}

/* any thread: find-or-claim below resolves concurrent same-key callers to one winner */
channel_t *channel_lookup(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port) {
  size_t claimed_h = 0;
  int was_tombstone = 0;
  int have_claim;
  channel_t *result;

  hash_writer_enter(t);
  result = chan_hash_find_or_claim(t, family, addr, addr_len, port, &claimed_h, &was_tombstone);
  have_claim = !result; /* NULL result always means a claim was made */

  for (size_t i = 0; !result && i < t->max_channels; i++) {
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

      if (c->psi)
        psi_free(c->psi);
      memset(c, 0, sizeof *c);
      c->ring = saved_ring;
      c->ring_size = saved_ring_size;
      c->cache.entries = saved_cache_entries;
      c->cache.cap = saved_cache_cap;
      atomic_store_explicit(&c->generation, saved_generation + 1, memory_order_relaxed);
      pthread_mutex_init(&c->hned_lock, NULL); /* hned_lock zeroed by memset above, reinit before use */

      if (saved_ring) {
        ret_ring_entry_t *ring = (ret_ring_entry_t *)saved_ring;
        for (size_t j = 0; j < saved_ring_size; j++) {
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
      c->resolve_slot = chan_key_hash(family, addr, addr_len, port) % t->max_channels;
      {
        size_t rb = c->resolve_slot;
        size_t cur = atomic_load_explicit(&t->resolve_hash[rb], memory_order_relaxed);
        if (cur == 0 || atomic_load_explicit(&t->chan[cur - 1].in_use, memory_order_relaxed) != 1)
          atomic_store_explicit(&t->resolve_hash[rb], i + 1, memory_order_release); /* published to resolve-by-port readers */
      }
      atomic_store_explicit(&c->last_seen, time(NULL), memory_order_relaxed);
      atomic_store_explicit(&c->in_use, 1, memory_order_release);

      chan_hash_publish_claim(t, claimed_h, was_tombstone, i);
      result = c;
      have_claim = 0;
    }
  }

  if (have_claim) { /* no free channel_t slot, revert claim */
    chan_hash_rollback_claim(t, claimed_h, was_tombstone);
    char addrbuf[64];
    if (!inet_ntop(family, addr, addrbuf, sizeof addrbuf))
      snprintf(addrbuf, sizeof addrbuf, "?");
    log_line(TOOL_NAME ": max-channels (%zu) reached, rejecting %s:%u", t->max_channels, addrbuf, port);
  }
  hash_writer_exit(t);

  if (atomic_load_explicit(&t->hash_used, memory_order_relaxed) > (t->hash_size / 4) * 3)
    chan_hash_rebuild(t);

  return result;
}

size_t channel_table_capacity(const channel_table_t *t) {
  return t->max_channels;
}
/* i must be < channel_table_capacity(t). NULL if slot isn't in use */
channel_t *channel_table_at(channel_table_t *t, size_t i) {
  if (atomic_load_explicit(&t->chan[i].in_use, memory_order_acquire) != 1)
    return NULL;
  return &t->chan[i];
}

channel_t *channel_lookup_by_resolve_slot(channel_table_t *t, size_t slot) {
  size_t slot_plus1;
  channel_t *c;

  if (slot >= t->max_channels)
    return NULL;
  slot_plus1 = atomic_load_explicit(&t->resolve_hash[slot], memory_order_acquire);
  if (slot_plus1 == 0)
    return NULL;
  c = &t->chan[slot_plus1 - 1];
  return atomic_load_explicit(&c->in_use, memory_order_acquire) == 1 ? c : NULL;
}

static channel_t *ssrc_scan_bucket(channel_table_t *t, size_t base, uint32_t ssrc) {
  size_t h = base;

  for (;;) {
    size_t slot_plus1 = atomic_load_explicit(&t->ssrc_hash[h], memory_order_relaxed);
    if (slot_plus1 == 0)
      return NULL;
    if (slot_plus1 != CHANNEL_HASH_TOMBSTONE) {
      channel_t *c = &t->chan[slot_plus1 - 1];
      if (atomic_load_explicit(&c->in_use, memory_order_acquire) == 1 && atomic_load_explicit(&c->ssrc_known, memory_order_acquire) && atomic_load_explicit(&c->ssrc, memory_order_acquire) == ssrc)
        return c;
    }
    h = (h + 1) & t->ssrc_hash_mask;
  }
}

/* seqlock reader, bounded retry: repeated overlap = plain miss, like ring.c */
channel_t *channel_find_by_ssrc(channel_table_t *t, uint32_t ssrc) {
  size_t base = chan_ssrc_hash(ssrc) & t->ssrc_hash_mask;

  for (int tries = 0; tries < 8; tries++) {
    unsigned g1 = atomic_load_explicit(&t->ssrc_gen, memory_order_acquire);
    unsigned g2;
    channel_t *result;

    if (g1 & 1u)
      continue; /* write in progress, retry */
    result = ssrc_scan_bucket(t, base, ssrc);
    g2 = atomic_load_explicit(&t->ssrc_gen, memory_order_acquire);
    if (g1 == g2)
      return result;
  }
  return NULL; /* repeated race treated as not-found */
}

void channel_store(channel_table_t *t, channel_t *c, uint32_t ssrc, uint16_t seq, uint32_t timestamp, unsigned char dscp, const unsigned char *payload, size_t payload_len) {
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
    unsigned g = seqlock_begin_write(&t->ssrc_gen);
    if (had_ssrc)
      ssrc_hash_remove(t, slot_idx, old_ssrc);
    ssrc_hash_insert(t, slot_idx, ssrc);
    seqlock_commit_write(&t->ssrc_gen, g);
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
    ret_ring_store(c, seq, timestamp, dscp, payload, payload_len);

  if (c->cache.cap > 0) {
    if (!c->psi) {
      c->psi = psi_new();
      if (!c->psi)
        log_line(TOOL_NAME ": out of memory allocating psi state for %s:%u, RAP detection disabled for this packet", c->group, c->port);
    }
    if (c->psi) {
      int is_rap = scan_ts_packets(c->psi, payload, payload_len);
      rap_cache_append(&c->cache, seq, timestamp, dscp, payload, payload_len, is_rap);
    }
  }
}

static void hned_collision_note(channel_t *c, uint32_t ssrc, time_t now) {
  size_t oldest = 0;
  time_t oldest_time = now;

  for (size_t i = 0; i < CHANNEL_HNED_COLLISION_MAX; i++) {
    if (c->hned_collisions[i].valid && c->hned_collisions[i].ssrc == ssrc) {
      c->hned_collisions[i].last_detected = now;
      return;
    }
    if (!c->hned_collisions[i].valid) {
      c->hned_collisions[i].valid = 1;
      c->hned_collisions[i].ssrc = ssrc;
      c->hned_collisions[i].last_detected = now;
      return;
    }
    if (c->hned_collisions[i].last_detected <= oldest_time) {
      oldest_time = c->hned_collisions[i].last_detected;
      oldest = i;
    }
  }
  /* table full, all still active: evict longest-untouched entry */
  c->hned_collisions[oldest].ssrc = ssrc;
  c->hned_collisions[oldest].last_detected = now;
}

void channel_hned_seen(channel_t *c, uint32_t ssrc, const struct sockaddr *from, socklen_t fromlen, const char *cname, size_t cname_len) {
  size_t i, free_slot = CHANNEL_HNED_TRACK_MAX, oldest = 0;
  time_t now = time(NULL);
  time_t oldest_time = now;
  int have_cname = cname && cname_len > 0;

  if (have_cname && cname_len >= RTCP_CNAME_MAX)
    cname_len = RTCP_CNAME_MAX - 1;

  pthread_mutex_lock(&c->hned_lock);

  for (i = 0; i < CHANNEL_HNED_TRACK_MAX; i++) {
    if (c->hned[i].valid && c->hned[i].ssrc == ssrc) {
      int collided;
      if (have_cname && c->hned[i].has_cname)
        collided = c->hned[i].cname_len != cname_len || memcmp(c->hned[i].cname, cname, cname_len) != 0;
      else
        collided = !(c->hned[i].addrlen == fromlen && memcmp(&c->hned[i].addr, from, fromlen) == 0);
      if (collided)
        hned_collision_note(c, ssrc, now);

      memcpy(&c->hned[i].addr, from, fromlen);
      c->hned[i].addrlen = fromlen;
      if (have_cname) {
        memcpy(c->hned[i].cname, cname, cname_len);
        c->hned[i].cname_len = cname_len;
        c->hned[i].has_cname = 1;
      }
      c->hned[i].last_seen = now;
      pthread_mutex_unlock(&c->hned_lock);
      return;
    }
    if (!c->hned[i].valid && free_slot == CHANNEL_HNED_TRACK_MAX)
      free_slot = i;
    if (c->hned[i].valid && c->hned[i].last_seen <= oldest_time) {
      oldest_time = c->hned[i].last_seen;
      oldest = i;
    }
  }

  i = (free_slot != CHANNEL_HNED_TRACK_MAX) ? free_slot : oldest;
  c->hned[i].valid = 1;
  c->hned[i].ssrc = ssrc;
  memcpy(&c->hned[i].addr, from, fromlen);
  c->hned[i].addrlen = fromlen;
  c->hned[i].has_cname = have_cname;
  if (have_cname) {
    memcpy(c->hned[i].cname, cname, cname_len);
    c->hned[i].cname_len = cname_len;
  }
  c->hned[i].last_seen = now;

  pthread_mutex_unlock(&c->hned_lock);
}

size_t channel_hned_collisions(channel_t *c, uint32_t *out, size_t cap, time_t max_age_s) {
  size_t n = 0;
  time_t now = time(NULL);

  pthread_mutex_lock(&c->hned_lock);
  for (size_t i = 0; i < CHANNEL_HNED_COLLISION_MAX && n < cap; i++) {
    if (c->hned_collisions[i].valid && now - c->hned_collisions[i].last_detected <= max_age_s)
      out[n++] = c->hned_collisions[i].ssrc;
  }
  pthread_mutex_unlock(&c->hned_lock);
  return n;
}

/* checks+reclaims one slot if stale. capture thread only in production, safe from any thread */
static void reap_slot(channel_table_t *t, size_t i, time_t now, time_t max_age_s) {
  channel_t *c = &t->chan[i];
  time_t last;

  if (atomic_load_explicit(&c->in_use, memory_order_acquire) != 1)
    return;
  last = atomic_load_explicit(&c->last_seen, memory_order_relaxed);
  if (now - last <= max_age_s)
    return;
  hash_writer_enter(t);
  {
    size_t h = chan_key_hash(c->family, c->addr, c->addr_len, c->port) & t->hash_mask;
    for (;;) {
      size_t v = atomic_load_explicit(&t->hash[h], memory_order_acquire);
      if (v == 0)
        break; /* shouldn't happen: an in-use channel always has a hash entry */
      if (v == i + 1) {
        atomic_store_explicit(&t->hash[h], CHANNEL_HASH_TOMBSTONE, memory_order_release);
        break;
      }
      h = (h + 1) & t->hash_mask;
    }
  }
  hash_writer_exit(t);
  if (atomic_load_explicit(&c->ssrc_known, memory_order_relaxed)) {
    unsigned g = seqlock_begin_write(&t->ssrc_gen);
    ssrc_hash_remove(t, i, atomic_load_explicit(&c->ssrc, memory_order_relaxed));
    seqlock_commit_write(&t->ssrc_gen, g);
  }
  atomic_store_explicit(&c->in_use, 0, memory_order_release);
}

void channel_table_reap(channel_table_t *t, time_t max_age_s) {
  time_t now = time(NULL);
  for (size_t i = 0; i < t->max_channels; i++)
    reap_slot(t, i, now, max_age_s);
}

/* amortized reap: at most max_scan slots per call, from internal wrapping cursor.
   caller drives it often (~once per packet) with small max_scan.
   no single call ever costs O(max_channels). */
void channel_table_reap_step(channel_table_t *t, time_t max_age_s, size_t max_scan) {
  time_t now = time(NULL);
  size_t n = max_scan < t->max_channels ? max_scan : t->max_channels;
  for (size_t i = 0; i < n; i++) {
    reap_slot(t, t->reap_cursor, now, max_age_s);
    t->reap_cursor = (t->reap_cursor + 1) % t->max_channels;
  }
}
