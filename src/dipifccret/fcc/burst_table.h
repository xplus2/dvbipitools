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

/* claim/find/note_nack/terminate: table lock. pacer (main.c): lock-free via gen seqlock below.
   msn/nack_count/congestion_adapted: table-lock only, never read by pacer, stay plain. */
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

typedef struct {
  burst_slot_t *slots;
  size_t cap;
  pthread_mutex_t lock;
  sockaddr_index_t *index; /* addr->slot, O(1) avg. slots[]/in_use stay authoritative: lookups
    re-validate against in_use+addr, drop stales */
} burst_table_t;

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

/* NULL if table full: caller should reject, not retry */
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

#endif
