/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_FILTER_PACE_H
#define DIPIREC_FILTER_PACE_H

#include <stdint.h>

typedef struct pace_ctrl pace_ctrl_t;

pace_ctrl_t *pace_new(void);
void pace_free(pace_ctrl_t *p);

/* RTP-framed source: call once per src_read() chunk, its most recent RTP timestamp (90kHz, 32-bit, RFC2250) */
void pace_feed_rtp_ts(pace_ctrl_t *p, uint32_t rtp_ts_90k);

/* raw (non-RTP) source: call once per 188B packet. pcr_pid 0: no-op, PMT not resolved yet */
void pace_feed_pcr_pkt(pace_ctrl_t *p, const unsigned char *pkt188, unsigned pcr_pid);

/* wrap-accumulates raw (bits wide) onto *elapsed_ticks, using/updating *last_raw.
   pure, no sleeping. exposed for unit-testing wraparound math. */
void pace_accumulate(uint64_t *last_raw, uint64_t *elapsed_ticks, uint64_t raw, unsigned bits);

/* 1 once a feed call took its first real sample (baseline set). test hook:
   first sample never sleeps, safe to probe after one feed call. */
int pace_has_baseline(const pace_ctrl_t *p);

#endif
