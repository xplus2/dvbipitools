/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRET_CHANNEL_H
#define DIPIRET_CHANNEL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define CHANNEL_MAX_PAYLOAD 1472 /* Ethernet-MTU-bound RTP/UDP payload ceiling */
#define CHANNEL_DEFAULT_MAX 384 /* max_channels 0 default; headroom above a typical ~300 TV-channel DVB-IPI lineup (radio usually has no RET), ~1.1GB at a 2000-slot ring (~3MB/channel) */

/* plain snapshot returned by channel_find - not the internal ring storage layout */
typedef struct {
  uint16_t seq;
  uint32_t timestamp;
  unsigned char payload[CHANNEL_MAX_PAYLOAD];
  size_t payload_len;
  int valid;
} channel_slot_t;

/* preallocated once, never moved/realloc'd/freed - pointer stable forever, nothing to lock.
 * single writer (capture thread) / many readers (worker threads). family/group/port published
 * via in_use (release on claim, acquire on read), immutable after. ring is opaque, seqlock-protected - only touch via channel_store/channel_find. */
typedef struct {
  _Atomic int in_use;
  int family;
  char group[64];
  unsigned port;
  _Atomic uint32_t ssrc;
  _Atomic int ssrc_known;
  _Atomic time_t last_seen;
  void *ring;
  size_t ring_size;
} channel_t;

typedef struct channel_table channel_table_t;

/* preallocates max_channels channel slots (0 = CHANNEL_DEFAULT_MAX) each with ring_slots ring entries; nothing allocated after this call */
channel_table_t *channel_table_new(size_t ring_slots, size_t max_channels);
void channel_table_free(channel_table_t *t);

/* finds or claims a free slot for family/group/port; NULL + logged warning if every slot is in_use */
channel_t *channel_lookup(channel_table_t *t, int family, const char *group, unsigned port);

channel_t *channel_find_by_ssrc(channel_table_t *t, uint32_t ssrc); /* NULL if unknown; pointer is always safe to hold, see above */

void channel_store(channel_t *c, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len); /* single-writer only; drops silently if payload_len > CHANNEL_MAX_PAYLOAD */

int channel_find(const channel_t *c, uint16_t seq, channel_slot_t *out); /* lock-free, any number of concurrent readers; copies into *out; 1 found, 0 not */

void channel_table_reap(channel_table_t *t, time_t max_age_s); /* frees the slot (in_use = 0) for channels idle longer than max_age_s; does not free memory, the slot is reused */

#endif
