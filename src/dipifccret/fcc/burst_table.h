/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_BURST_TABLE_H
#define DIPIFCCRET_BURST_TABLE_H

#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>

#include "burst.h"

typedef struct {
  int in_use;
  struct sockaddr_storage addr;
  socklen_t addrlen;
  int fd;
  burst_t *b;
  uint8_t msn; /* RAMS-I MSN for this burst, 0 at claim, incremented per code-100 update */
  unsigned nack_count; /* NACKs from client during current burst, 0 at claim */
  int congestion_adapted; /* bitrate already halved once this burst, 0 at claim */
} burst_slot_t;

typedef struct {
  burst_slot_t *slots;
  size_t cap;
  pthread_mutex_t lock;
} burst_table_t;

burst_table_t *burst_table_new(size_t cap);
void burst_table_free(burst_table_t *t);

/* NULL if table full: caller should reject, not retry */
burst_slot_t *burst_table_claim(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b);

/* existing burst session for this client address, or NULL */
burst_slot_t *burst_table_find(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen);

/* terminates matching session if found; 1 found, 0 not. has_stop_seq/stop_seqnum: see burst_terminate() */
int burst_table_terminate(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int has_stop_seq, uint32_t stop_seqnum);

#endif
