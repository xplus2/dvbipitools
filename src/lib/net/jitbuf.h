/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_JITBUF_H
#define DVBIPITOOLS_LIB_NET_JITBUF_H

#include <stddef.h>
#include <stdint.h>

#define JITBUF_MAX_DGRAM 1500
#define JITBUF_MAX_DELAY_MS 2000

typedef enum {
  JITBUF_FIFO, /* delay only, arrival order */
  JITBUF_AUTO  /* first datagram decides: RTP reorders, else FIFO */
} jitbuf_mode_t;

typedef struct {
  uint64_t reordered; /* arrived behind later seq */
  uint64_t lost;     /* seq gaps given up on */
  uint64_t late;     /* arrived behind release point */
  uint64_t dup;
  uint64_t resync;   /* ssrc change or seq jump */
  uint64_t dropped;  /* oversize, overflow, not RTP in reorder mode */
} jitbuf_stats_t;

typedef struct jitbuf jitbuf_t;

/* NULL on calloc failure or delay_ms outside 1..JITBUF_MAX_DELAY_MS */
jitbuf_t *jitbuf_new(unsigned delay_ms, jitbuf_mode_t mode);
void jitbuf_free(jitbuf_t *j);

/* copies datagram. now_ns: one monotonic clock for all calls */
void jitbuf_push(jitbuf_t *j, const unsigned char *dgram, size_t len, uint64_t now_ns);

/* oldest due datagram, RTP header kept. 0 = none due or cap too small (dropped) */
size_t jitbuf_pop(jitbuf_t *j, unsigned char *out, size_t cap, uint64_t now_ns);

/* -1 empty, 0 due now, else ns until next release */
int64_t jitbuf_next_ns(const jitbuf_t *j, uint64_t now_ns);

/* age of oldest queued datagram, capped at delay. 0 = empty */
uint64_t jitbuf_depth_ns(const jitbuf_t *j, uint64_t now_ns);

size_t jitbuf_queued(const jitbuf_t *j);
void jitbuf_stats(const jitbuf_t *j, jitbuf_stats_t *st);

#endif
