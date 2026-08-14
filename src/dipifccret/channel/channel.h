/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CHANNEL_H
#define DIPIFCCRET_CHANNEL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

#include "lib/demux/psi/psi.h"
#include "lib/demux/rtcp.h"

#define CHANNEL_DEFAULT_MAX 384 /* max_channels 0 default */
#define CHANNEL_BITRATE_WINDOW_S 2
#define CHANNEL_MAX_PAYLOAD 1472 /* Ethernet-MTU-bound RTP/UDP payload ceiling */
#define CHANNEL_HNED_TRACK_MAX 32 /* recently seen (HNED ssrc, transport address) pairs, F.5.3 SRBT 8 source */
#define CHANNEL_HNED_COLLISION_MAX 8 /* pending SSRC-collision entries reported via RSI SRBT 8 */

/* one HNED's most recent transport address (and CNAME if sent) per ssrc.
   F.5.3 primary collision check: [SSRC,CNAME]. Fallback for no-SDES HNEDs: [SSRC,address]. */
typedef struct {
  uint32_t ssrc;
  struct sockaddr_storage addr;
  socklen_t addrlen;
  char cname[RTCP_CNAME_MAX];
  size_t cname_len;
  int has_cname;
  time_t last_seen;
  int valid;
} hned_track_entry_t;

/* one ssrc currently believed to be in use by more than one HNED at once */
typedef struct {
  uint32_t ssrc;
  time_t last_detected;
  int valid;
} hned_collision_entry_t;

/* RET ring snapshot, returned by channel_find */
typedef struct {
  uint16_t seq;
  uint32_t timestamp;
  unsigned char dscp; /* original packet DSCP, F.9, mirrored onto RTX packet */
  unsigned char payload[CHANNEL_MAX_PAYLOAD];
  size_t payload_len;
  int valid;
} channel_slot_t;

/* FCC cache entry, returned by channel_cache_get */
typedef struct {
  uint16_t seq;
  uint32_t timestamp;
  unsigned char dscp; /* original packet DSCP, I.2.12, mirrored onto burst packet */
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
   ring_size 0 = RET disabled here, cache.cap 0 = FCC disabled here; either or both may be active */
typedef struct {
  _Atomic int in_use;
  int family;
  unsigned char addr[16]; /* raw dst bytes, 4 (v4) or 16 (v6), see addr_len, hash/lookup key */
  size_t addr_len;
  char group[64]; /* printable form of addr, for logging / mcsend.c's mcast_open_send */
  unsigned port;
  size_t resolve_slot; /* hash(family,addr,port) % max_channels, see channel_lookup_by_resolve_slot() */
  _Atomic uint32_t ssrc;
  _Atomic int ssrc_known;
  _Atomic time_t last_seen;
  _Atomic unsigned generation; /* bumped on every reclaim: lets mcsend.c detect a reused slot */
  _Atomic uint16_t rtx_seq_mc; /* MC RET session's own RTX seq space, F.3.2.1; reset by reclaim memset below */
  time_t bitrate_window_start;
  uint64_t bitrate_window_bytes;
  int oversized_logged; /* channel_store: re-armed once a payload is back within CHANNEL_MAX_PAYLOAD */
  _Atomic double nominal_bps;
  psi_t *psi; /* FCC RAP detection only */
  void *ring; /* RET ring */
  size_t ring_size;
  rap_cache_t cache;
  hned_track_entry_t hned[CHANNEL_HNED_TRACK_MAX]; /* guarded by owning table lock, not lock-free */
  hned_collision_entry_t hned_collisions[CHANNEL_HNED_COLLISION_MAX]; /* same */
} channel_t;

typedef struct channel_table channel_table_t;

/* ring_slots 0 = no RET, cache_cap 0 = no FCC */
channel_table_t *channel_table_new(size_t max_channels, size_t ring_slots, size_t cache_cap);
void channel_table_free(channel_table_t *t);

channel_t *channel_lookup(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port);

channel_t *channel_find_by_ssrc(channel_table_t *t, uint32_t ssrc);

/* NULL if slot not in use, no probe on collision */
channel_t *channel_lookup_by_resolve_slot(channel_table_t *t, size_t slot);

size_t channel_table_capacity(const channel_table_t *t);

/* i must be < channel_table_capacity(t). NULL if slot isn't in use. */
channel_t *channel_table_at(channel_table_t *t, size_t i);

/* single write path for both RET ring and FCC cache, one call per captured packet.
   also maintains t's ssrc->channel index (channel_find_by_ssrc), taking t->lock only
   when ssrc actually changed since the last call for this channel */
void channel_store(channel_table_t *t, channel_t *c, uint32_t ssrc, uint16_t seq, uint32_t timestamp, unsigned char dscp, const unsigned char *payload, size_t payload_len);

int channel_find(const channel_t *c, uint16_t seq, channel_slot_t *out); /* RET; 0 if ring inactive */

int channel_has_rap(const channel_t *c); /* FCC; 0 if cache inactive */
size_t channel_cache_count(const channel_t *c);
int channel_cache_get(const channel_t *c, size_t index, rap_cache_entry_t *out);

/* F.5.3 SRBT 8 source. cname NULL/0-length if none this time.
   collision: [ssrc,cname] if both known, else [ssrc,address]. locks t, callable from any listen worker thread. */
void channel_hned_seen(channel_table_t *t, channel_t *c, uint32_t ssrc, const struct sockaddr *from, socklen_t fromlen, const char *cname, size_t cname_len);

/* pending collisions on c within max_age_s, up to cap ssrcs into out. returns count, locks t. */
size_t channel_hned_collisions(channel_table_t *t, channel_t *c, uint32_t *out, size_t cap, time_t max_age_s);

void channel_table_reap(channel_table_t *t, time_t max_age_s);

/* bounded slice of channel_table_reap, see channel.c */
void channel_table_reap_step(channel_table_t *t, time_t max_age_s, size_t max_scan);

#endif
