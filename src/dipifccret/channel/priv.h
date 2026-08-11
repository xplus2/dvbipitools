/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CHANNEL_PRIV_H
#define DIPIFCCRET_CHANNEL_PRIV_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "channel.h"

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

/* hash.c */
size_t next_pow2(size_t n);
size_t chan_key_hash(int family, const void *addr, size_t addr_len, unsigned port);
void chan_hash_rebuild(channel_table_t *t);
channel_t *chan_hash_probe(channel_table_t *t, int family, const void *addr, size_t addr_len, unsigned port, size_t *avail);
size_t chan_ssrc_hash(uint32_t ssrc);
void ssrc_hash_rebuild(channel_table_t *t);
void ssrc_hash_remove(channel_table_t *t, size_t slot_idx, uint32_t ssrc);
void ssrc_hash_insert(channel_table_t *t, size_t slot_idx, uint32_t ssrc);

/* ring.c */
void ret_ring_store(channel_t *c, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len);
int scan_ts_packets(psi_t *psi, const unsigned char *payload, size_t payload_len);
void rap_cache_append(rap_cache_t *rc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, int is_rap);

#endif
