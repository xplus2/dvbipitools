/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CHANNEL_H
#define DIPIFCCRET_CHANNEL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "lib/demux/psi/psi.h"

#define CHANNEL_DEFAULT_MAX 384 /* max_channels 0 default */
#define CHANNEL_BITRATE_WINDOW_S 2
#define CHANNEL_MAX_PAYLOAD 1472 /* Ethernet-MTU-bound RTP/UDP payload ceiling */

/* RET ring snapshot, returned by channel_find */
typedef struct {
  uint16_t seq;
  uint32_t timestamp;
  unsigned char payload[CHANNEL_MAX_PAYLOAD];
  size_t payload_len;
  int valid;
} channel_slot_t;

/* FCC cache entry, returned by channel_cache_get */
typedef struct {
  uint16_t seq;
  uint32_t timestamp;
  unsigned char payload[CHANNEL_MAX_PAYLOAD];
  size_t payload_len;
} rap_cache_entry_t;

/* cap 0 = FCC disabled, entries not allocated */
typedef struct {
  void *entries;
  size_t cap;
  _Atomic uint64_t write_count;
  _Atomic uint64_t rap_write_count;
  _Atomic int have_rap;
} rap_cache_t;

/* single writer (capture thread); lock-free readers elsewhere, every shared field atomic.
 * ring_size 0 = RET disabled here, cache.cap 0 = FCC disabled here; either or both may be active */
typedef struct {
  _Atomic int in_use;
  int family;
  unsigned char addr[16]; /* raw dst bytes, 4 (v4) or 16 (v6), see addr_len - hash/lookup key */
  size_t addr_len;
  char group[64]; /* printable form of addr, for logging / mcsend.c's mcast_open_send */
  unsigned port;
  _Atomic uint32_t ssrc;
  _Atomic int ssrc_known;
  _Atomic time_t last_seen;
  _Atomic unsigned generation; /* bumped on every reclaim: lets mcsend.c detect a reused slot */
  time_t bitrate_window_start;
  uint64_t bitrate_window_bytes;
  int oversized_logged; /* channel_store: re-armed once a payload is back within CHANNEL_MAX_PAYLOAD */
  _Atomic double nominal_bps;
  psi_t *psi; /* FCC RAP detection only */
  void *ring; /* RET ring */
  size_t ring_size;
  rap_cache_t cache;
} channel_t;

typedef struct channel_table channel_table_t;

/* ring_slots 0 = no RET, cache_cap 0 = no FCC */
channel_table_t *channel_table_new(size_t max_channels, size_t ring_slots, size_t cache_cap);
void channel_table_free(channel_table_t *t);

channel_t *channel_lookup(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port);

channel_t *channel_find_by_ssrc(channel_table_t *t, uint32_t ssrc);

/* single write path for both RET ring and FCC cache, one call per captured packet.
   also maintains t's ssrc->channel index (channel_find_by_ssrc), taking t->lock only
   when ssrc actually changed since the last call for this channel */
void channel_store(channel_table_t *t, channel_t *c, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len);

int channel_find(const channel_t *c, uint16_t seq, channel_slot_t *out); /* RET; 0 if ring inactive */

int channel_has_rap(const channel_t *c); /* FCC; 0 if cache inactive */
size_t channel_cache_count(const channel_t *c);
int channel_cache_get(const channel_t *c, size_t index, rap_cache_entry_t *out);

void channel_table_reap(channel_table_t *t, time_t max_age_s);

/* bounded per-call slice of channel_table_reap - see channel.c */
void channel_table_reap_step(channel_table_t *t, time_t max_age_s, size_t max_scan);

#endif
