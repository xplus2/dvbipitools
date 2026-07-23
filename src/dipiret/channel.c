/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

#include "channel.h"

/* single writer only: channel_lookup, channel_store, channel_table_reap must all run on the
 * same one thread (the capture thread). channel_find_by_ssrc/channel_find are lock-free reads,
 * safe from any number of threads, scattered across any ring slots concurrently with the
 * writer - every field touched by more than one thread is _Atomic (including the payload,
 * word at a time), not just gen, so there is no plain-memory data race for TSan to find. */

#define CHANNEL_PAYLOAD_WORDS (CHANNEL_MAX_PAYLOAD / 8) /* 1472 / 8 = 184 exactly */

typedef struct {
  _Atomic unsigned gen; /* seqlock: odd = write in progress, even = stable */
  _Atomic uint16_t seq;
  _Atomic uint32_t timestamp;
  _Atomic uint64_t payload[CHANNEL_PAYLOAD_WORDS];
  _Atomic size_t payload_len;
  _Atomic int valid;
} ring_entry_t;

struct channel_table {
  channel_t *chan; /* fixed array, size max_channels, preallocated once */
  size_t max_channels;
  size_t ring_slots;
};

channel_table_t *channel_table_new(size_t ring_slots, size_t max_channels) {
  channel_table_t *t;
  size_t i;

  if (max_channels == 0)
    max_channels = CHANNEL_DEFAULT_MAX;
  t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->max_channels = max_channels;
  t->ring_slots = ring_slots;
  t->chan = calloc(max_channels, sizeof(channel_t));
  if (!t->chan) {
    free(t);
    return NULL;
  }
  for (i = 0; i < max_channels; i++) {
    t->chan[i].ring = calloc(ring_slots, sizeof(ring_entry_t));
    t->chan[i].ring_size = ring_slots;
    if (!t->chan[i].ring) {
      size_t j;
      for (j = 0; j < i; j++)
        free(t->chan[j].ring);
      free(t->chan);
      free(t);
      return NULL;
    }
  }
  return t;
}

void channel_table_free(channel_table_t *t) {
  size_t i;
  if (!t)
    return;
  for (i = 0; i < t->max_channels; i++)
    free(t->chan[i].ring);
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
      ring_entry_t *ring = (ring_entry_t *)c->ring;
      size_t j;
      c->family = family;
      strncpy(c->group, group, sizeof c->group - 1);
      c->group[sizeof c->group - 1] = '\0';
      c->port = port;
      atomic_store_explicit(&c->ssrc_known, 0, memory_order_relaxed);
      for (j = 0; j < c->ring_size; j++) {
        atomic_store_explicit(&ring[j].valid, 0, memory_order_relaxed);
        atomic_store_explicit(&ring[j].gen, 0, memory_order_relaxed);
      }
      atomic_store_explicit(&c->last_seen, time(NULL), memory_order_relaxed);
      atomic_store_explicit(&c->in_use, 1, memory_order_release);
      return c;
    }
  }

  log_line("dipiret: max-channels (%zu) reached, rejecting %s:%u", t->max_channels, group, port);
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
  ring_entry_t *slot;
  unsigned g;
  uint64_t words[CHANNEL_PAYLOAD_WORDS];
  size_t i;

  if (payload_len > CHANNEL_MAX_PAYLOAD)
    return;
  atomic_store_explicit(&c->ssrc, ssrc, memory_order_relaxed);
  atomic_store_explicit(&c->ssrc_known, 1, memory_order_release);
  atomic_store_explicit(&c->last_seen, time(NULL), memory_order_relaxed);

  memset(words, 0, sizeof words);
  memcpy(words, payload, payload_len);

  slot = &((ring_entry_t *)c->ring)[seq % c->ring_size];
  g = atomic_load_explicit(&slot->gen, memory_order_relaxed);
  atomic_store_explicit(&slot->gen, g + 1, memory_order_relaxed); /* odd: write starting */
  atomic_store_explicit(&slot->seq, seq, memory_order_relaxed);
  atomic_store_explicit(&slot->timestamp, timestamp, memory_order_relaxed);
  for (i = 0; i < CHANNEL_PAYLOAD_WORDS; i++)
    atomic_store_explicit(&slot->payload[i], words[i], memory_order_relaxed);
  atomic_store_explicit(&slot->payload_len, payload_len, memory_order_relaxed);
  atomic_store_explicit(&slot->valid, 1, memory_order_relaxed);
  atomic_store_explicit(&slot->gen, g + 2, memory_order_release); /* even: write done, publishes everything above */
}

int channel_find(const channel_t *c, uint16_t seq, channel_slot_t *out) {
  const ring_entry_t *slot = &((const ring_entry_t *)c->ring)[seq % c->ring_size];
  unsigned g1, g2;
  int tries;
  uint64_t words[CHANNEL_PAYLOAD_WORDS];
  size_t i;

  for (tries = 0; tries < 8; tries++) {
    g1 = atomic_load_explicit(&slot->gen, memory_order_acquire);
    if (g1 & 1u)
      continue; /* write in progress, retry */
    out->seq = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    out->timestamp = atomic_load_explicit(&slot->timestamp, memory_order_relaxed);
    for (i = 0; i < CHANNEL_PAYLOAD_WORDS; i++)
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
