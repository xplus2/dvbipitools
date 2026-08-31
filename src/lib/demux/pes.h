/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_DEMUX_PES_H
#define DIPIREC_DEMUX_PES_H

#include <stddef.h>
#include <stdint.h>

/* one complete PES: ES payload + 90 kHz PTS/DTS (has_pts/has_dts 0 if absent).
   dts present only if PES header's PTS_DTS_flags = 11 */
typedef void (*pes_cb)(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data,
                        size_t len);
typedef struct pes pes_t;
pes_t *pes_new(pes_cb cb, void *ctx);
void pes_free(pes_t *p);
int  pes_track(pes_t *p, unsigned pid);               /* pid */
void pes_feed(pes_t *p, const unsigned char *pkt);    /* 188 B */
void pes_flush(pes_t *p);                             /* pending PES */

typedef struct {
  uint64_t pts_ext, last_raw; /* 33-bit PTS unwrap state */
  int pts_seen;
} pts_unwrap_t;

/* unwrap a 33-bit PTS into a monotonic tick count, ms = return/90 */
int64_t pts_unwrap(pts_unwrap_t *st, uint64_t raw);

#endif
