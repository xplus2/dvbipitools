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

/* RFC 6285 7.3.1 RAMS-I response codes.
   unused = feature absent: congestion detection, client/stream policy, preamble burst, in-session value update. */
typedef enum {
  BURST_UPDATE = 100,             /* repeat RAMS-R on existing burst, or NACK-driven bitrate halving */
  BURST_ACCEPT = 200,
  BURST_DONE = 201,
  BURST_MALFORMED = 400,          /* min_buffer_fill_ms > max_buffer_fill_ms */
  BURST_MIN_BUFFER_INVALID = 401, /* min_buffer_fill_ms > max_buffer_fill_bound_ms, DoS bound */
  BURST_MAX_BUFFER_INVALID = 402, /* max_buffer_fill_ms == 0 */
  BURST_BITRATE_INSUFFICIENT = 403,
  BURST_TERM_MALFORMED = 404,     /* corrupt RAMS-T TLV region, see rtcp_malformed_cb */
  BURST_INTERNAL_ERROR = 500,     /* burst_new() allocation failure */
  BURST_CONGESTION = 502,         /* local sendto() EAGAIN/ENOBUFS during a tick, no NACK/RR-based detection */
  BURST_TABLE_FULL = 503,         /* max-bursts concurrent sessions reached */
  BURST_NOT_SUPPORTED = 504,      /* FCC disabled, --no-fcc */
  BURST_NOT_ELIGIBLE = 505,       /* client source address outside --fcc-client-range */
  BURST_NOT_ENABLED = 506,        /* channel outside --fcc-range */
  BURST_NO_VALID_START = 507,     /* cached depth < requested min buffer fill */
  BURST_NO_RAP = 508,             /* channel known, no random-access point cached yet */
  BURST_SSRC_NOT_FOUND = 509,     /* explicit media_ssrc requested, no matching channel */
  BURST_REJECT = 510,             /* channel unresolvable, see README known gaps (ignore_media_ssrc) */
  BURST_PREAMBLE_ONLY = 511,      /* unused, DVB Annex I RAMS-R profile omits triggering TLV */
  BURST_POLICY_DENIED = 512       /* unused, no policy engine */
} burst_response_t;

/* c NULL: 510 if ignore_media_ssrc else 509. min>bound (0=no bound): 401. max==0: 402.
   min>max: 400. no RAP: 508. cache<min: 507. bitrate<=nominal: 403. */
burst_response_t burst_decide(const channel_t *c, const rtcp_rams_r_t *req, unsigned max_buffer_fill_bound_ms);

/* channel borrowed: generation snapshotted at burst_new, rechecked each tick to detect
   slot reuse (see channel_t.generation). cursor/bytes_sent single-ticker-thread only;
   done is cross-thread atomic */
typedef struct {
  const channel_t *channel;
  unsigned generation;
  _Atomic uint16_t rtx_seq;
  unsigned char rtx_pt;
  size_t cursor;
  _Atomic double target_bps; /* pacer-thread-written at claim; nack_cb may reduce it mid-burst, see main.c */
  double bytes_sent;
  struct timespec start_time; /* CLOCK_MONOTONIC, sub-second pacing precision */
  _Atomic int done;
  _Atomic int has_stop_seq; /* RAMS-T Extended RTP Seqnum TLV, RFC 6285 Sec 7.5: clip instead of stopping now */
  _Atomic uint16_t stop_seq; /* low 16 bits of client's first-received multicast extended seqnum */
} burst_t;

typedef enum { BURST_TICK_CONTINUE, BURST_TICK_DONE } burst_tick_result_t;

/* client_max_bps 0 = no cap. call only after burst_decide == BURST_ACCEPT */
burst_t *burst_new(const channel_t *c, double multiplier, double client_max_bps, unsigned char rtx_pt);
void burst_free(burst_t *b);

/* no socket ownership, same as capture.c/ret.c. dscp: sent entry's own mirrored DSCP, F.9/I.2.12 */
typedef void (*burst_send_fn)(const unsigned char *pkt, size_t len, int dscp, void *user);

/* sends what pacing budget allows; DONE on cache catch-up or duration_cap_ms, same code (BURST_DONE) either way */
burst_tick_result_t burst_tick(burst_t *b, unsigned duration_cap_ms, burst_send_fn send_cb, void *user);

/* RAMS-T. has_stop_seq 0: stop now. has_stop_seq 1: keep sending up to (not
   including) stop_seqnum, RFC 6285 Sec 7.5 Extended RTP Seqnum TLV */
void burst_terminate(burst_t *b, int has_stop_seq, uint32_t stop_seqnum);

int burst_is_done(const burst_t *b);

#endif
