/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_BURST_TABLE_H
#define DIPIFCCRET_BURST_TABLE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/socket.h>

#include "lib/net/sockaddr_index.h"
#include "lib/seqlock.h"

#include "burst.h"

#define BURST_ADDR_WORDS (sizeof(struct sockaddr_storage) / sizeof(uint64_t))
#define BURST_TABLE_STRIPE_COUNT_MAX 8 /* client addr hashes to one, own lock each */

/* claim/find/note_nack/terminate/release: stripe lock (picked by client addr). pacer
   (main.c): lock-free scan via gen seqlock below.
   msn/nack_count/congestion_adapted: stripe-lock only, never read by pacer, stay plain. */
typedef struct {
  _Atomic unsigned gen;
  _Atomic int in_use;
  _Atomic uint64_t addr_words[BURST_ADDR_WORDS];
  _Atomic socklen_t addrlen;
  _Atomic int fd;
  _Atomic(burst_t *) b;
  uint8_t msn; /* RAMS-I MSN for this burst, 0 at claim, incremented per code-100 update */
  unsigned nack_count; /* NACKs from client during current burst, 0 at claim */
  int congestion_adapted; /* bitrate already halved once this burst, 0 at claim */
} burst_slot_t;

/* full stripe rejects, no eviction: no protocol path to evict active repair session */
typedef struct {
  size_t base;
  size_t count;
  size_t *free_list; /* stack of own global slot indices, O(1) claim/release */
  size_t free_count;
  sockaddr_index_t *index; /* addr->global slot idx, O(1) avg, own clients only */
  pthread_mutex_t lock;
} burst_stripe_t;

typedef struct {
  burst_slot_t *slots; /* flat, cap entries: main.c's pacer scans this directly */
  size_t cap;
  burst_stripe_t stripes[BURST_TABLE_STRIPE_COUNT_MAX];
  size_t stripe_count;
  _Atomic uint64_t bytes_retransmitted_total; /* pacer-folded per tick, survives slot reuse */
  _Atomic uint64_t nacks_total;
  _Atomic uint64_t congestion_adaptations_total;
} burst_table_t;

typedef struct {
  unsigned bursts_active;
  uint64_t bytes_retransmitted_total;
  uint64_t nacks_total;
  uint64_t congestion_adaptations_total;
} burst_table_metrics_t;

typedef enum {
  BURST_TABLE_NACK_NONE,
  BURST_TABLE_NACK_ADAPTED,
  BURST_TABLE_NACK_TERMINATED
} burst_table_nack_action_t;

typedef struct {
  burst_table_nack_action_t action;
  uint8_t msn;
  double new_bps;
} burst_table_nack_result_t;

burst_table_t *burst_table_new(size_t cap);
void burst_table_free(burst_table_t *t);

/* NULL if that client's stripe is full: caller should reject, not retry */
burst_slot_t *burst_table_claim(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b);

/* existing burst session for this client address, or NULL */
burst_slot_t *burst_table_find(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen);

/* starts a new burst or swaps an existing session to b.
   returns 1 if an existing session was updated, 0 if a new slot was claimed, -1 if full. */
int burst_table_start(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b, uint8_t *msn_out);

/* note a NACK on an existing session. threshold 0 disables all actions. 1 found, 0 not found. */
int burst_table_note_nack(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, unsigned threshold, burst_table_nack_result_t *out);

/* terminates matching session if found. 1 found, 0 not. has_stop_seq/stop_seqnum: see burst_terminate() */
int burst_table_terminate(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int has_stop_seq, uint32_t stop_seqnum);

/* owns_slot: idx still in_use and holds expect_b.
   should_clear: also seqlock-clears slot, returns it to its stripe's free list.
   *did_clear: whether that clear ran. */
int burst_table_release(burst_table_t *t, size_t idx, const burst_t *expect_b, int should_clear, int *did_clear);

/* pacer folds each tick's bytes_sent delta here, doesn't survive slot reuse otherwise */
void burst_table_note_bytes_sent(burst_table_t *t, uint64_t bytes);

void burst_table_get_metrics(const burst_table_t *t, burst_table_metrics_t *out);

#endif
