/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_BURST_H
#define DIPIFCCRET_BURST_H

#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "lib/demux/rtcp.h"
#include "lib/mux/rtx.h"

#include "../channel/channel.h"

/* RAMS-I codes used; 401/402/404/504-506/511/100 unimplemented */
typedef enum {
  BURST_ACCEPT = 200,
  BURST_MALFORMED = 400,
  BURST_BITRATE_INSUFFICIENT = 403,
  BURST_REJECT = 510
} burst_response_t;

/* c NULL/no RAP -> reject; min>max buffer-fill -> malformed; client max bitrate <= nominal -> insufficient */
burst_response_t burst_decide(const channel_t *c, const rtcp_rams_r_t *req);

/* channel borrowed: generation snapshotted at burst_new, rechecked each tick to detect
 * slot reuse (see channel_t.generation). cursor/bytes_sent single-ticker-thread only;
 * done is cross-thread atomic */
typedef struct {
  const channel_t *channel;
  unsigned generation;
  rtx_ctx_t *rtx;
  unsigned char rtx_pt;
  size_t cursor;
  double target_bps;
  double bytes_sent;
  struct timespec start_time; /* CLOCK_MONOTONIC, sub-second pacing precision */
  _Atomic int done;
} burst_t;

typedef enum { BURST_TICK_CONTINUE, BURST_TICK_DONE } burst_tick_result_t;

/* client_max_bps 0 = no cap. call only after burst_decide == BURST_ACCEPT */
burst_t *burst_new(const channel_t *c, double multiplier, double client_max_bps, unsigned char rtx_pt);
void burst_free(burst_t *b);

/* no socket ownership, same as capture.c/ret.c */
typedef void (*burst_send_fn)(const unsigned char *pkt, size_t len, void *user);

/* sends what pacing budget allows; DONE on cache catch-up or duration_cap_ms, same code (201) either way */
burst_tick_result_t burst_tick(burst_t *b, unsigned duration_cap_ms, burst_send_fn send_cb, void *user);

/* RAMS-T: stop now, ignores optional seqnum TLV (no clipping) */
void burst_terminate(burst_t *b);

int burst_is_done(const burst_t *b);

#endif
