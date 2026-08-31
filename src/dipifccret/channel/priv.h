/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CHANNEL_PRIV_H
#define DIPIFCCRET_CHANNEL_PRIV_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/seqlock.h"

#include "channel.h"

/* single writer (capture thread): lookup/store/reap. atomic reads elsewhere, lock-free.
   two rings by design: RET seq-keyed, FCC RAP-anchored linear-from-RAP. */

#define RET_PAYLOAD_WORDS (CHANNEL_MAX_PAYLOAD / 8)
#define FCC_PAYLOAD_WORDS (CHANNEL_MAX_PAYLOAD / 8)

typedef struct {
  _Atomic unsigned gen; /* seqlock: odd = write in progress, even = stable */
  _Atomic uint16_t seq;
  _Atomic uint32_t timestamp;
  _Atomic unsigned char dscp;
  _Atomic uint64_t payload[RET_PAYLOAD_WORDS];
  _Atomic size_t payload_len;
  _Atomic int valid;
} ret_ring_entry_t;

typedef struct {
  _Atomic unsigned gen; /* odd = write in progress, even = stable */
  _Atomic uint16_t seq;
  _Atomic uint32_t timestamp;
  _Atomic unsigned char dscp;
  _Atomic uint64_t payload[FCC_PAYLOAD_WORDS];
  _Atomic size_t payload_len;
} fcc_ring_entry_t;

struct channel_table {
  channel_t *chan; /* fixed array, size max_channels, preallocated once */
  size_t max_channels;
  size_t ring_slots;
  size_t cache_cap;

  /* open-addr index: (family,group,port)->slot+1. 0=empty, TOMBSTONE=reaped, CLAIMING=insert in flight.
     rebuild >75% load. multi-writer: channel_lookup (any thread), reap_slot, both CAS single buckets.
     hash_rebuild_active/hash_active_writers pair excludes concurrent bucket ops during a rewrite. */
  _Atomic size_t *hash;
  size_t hash_size;
  size_t hash_mask;
  _Atomic size_t hash_used;
  _Atomic int hash_rebuild_active;
  _Atomic int hash_active_writers;

  /* open-addr index: ssrc->slot+1, same conventions. channel_find_by_ssrc's NACK-path lookup.
     write-only from capture thread, read cross-thread by listen workers: seqlock-guarded via ssrc_gen.
     bucket array itself _Atomic too: seqlock only tells readers when to discard, not access safety. */
  _Atomic size_t *ssrc_hash;
  size_t ssrc_hash_size;
  size_t ssrc_hash_mask;
  size_t ssrc_hash_used;
  _Atomic unsigned ssrc_gen;

  _Atomic size_t *resolve_hash; /* hash(family,addr,port)%max_channels -> slot+1, no probe, sized max_channels,
                                    not padded. single-word publish, read cross-thread by resolve-by-port listeners. */

  size_t reap_cursor; /* channel_table_reap_step()'s scan position, wraps at max_channels */
};

#define CHANNEL_HASH_TOMBSTONE SIZE_MAX
#define CHANNEL_HASH_CLAIMING (SIZE_MAX - 1)

/* hash.c */
size_t chan_key_hash(int family, const void *addr, size_t addr_len, unsigned port);
void chan_hash_rebuild(channel_table_t *t);
void hash_writer_enter(channel_table_t *t);
void hash_writer_exit(channel_table_t *t);
/* match: returned directly. no match: NULL, *claimed_h = CAS'd bucket (state CLAIMING),
   *was_tombstone = TOMBSTONE vs EMPTY before claim. exactly one of publish_claim/rollback_claim
   follows. call wrapped in hash_writer_enter/exit. */
channel_t *chan_hash_find_or_claim(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port, size_t *claimed_h, int *was_tombstone);
void chan_hash_publish_claim(channel_table_t *t, size_t h, int was_tombstone, size_t slot_idx);
void chan_hash_rollback_claim(channel_table_t *t, size_t h, int was_tombstone);
size_t chan_ssrc_hash(uint32_t ssrc);
void ssrc_hash_rebuild(channel_table_t *t);
void ssrc_hash_remove(channel_table_t *t, size_t slot_idx, uint32_t ssrc);
void ssrc_hash_insert(channel_table_t *t, size_t slot_idx, uint32_t ssrc);

/* ring.c */
void ret_ring_store(channel_t *c, uint16_t seq, uint32_t timestamp, unsigned char dscp, const unsigned char *payload, size_t payload_len);
int scan_ts_packets(psi_t *psi, const unsigned char *payload, size_t payload_len);
void rap_cache_append(rap_cache_t *rc, uint16_t seq, uint32_t timestamp, unsigned char dscp, const unsigned char *payload, size_t payload_len, int is_rap);

#endif
