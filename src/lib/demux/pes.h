/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_PES_H
#define DVBIPITOOLS_LIB_DEMUX_PES_H

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
  uint64_t pts_ext;  /* 33-bit PTS unwrap state */
  uint64_t last_raw;
  int pts_seen;
} pts_unwrap_t;

/* unwrap a 33-bit PTS into a monotonic tick count, ms = return/90 */
int64_t pts_unwrap(pts_unwrap_t *st, uint64_t raw);

#define PTS_DISC_MS 10000
#define PTS_DISC_NONE INT64_MIN

/* recorders: audio ahead of first video keyframe kept up to this, keeps A/V offset */
#define PTS_LEAD_KEEP_MS 500

/* mux-wide timeline rebase across splices. shift is subtracted from unwrapped ms */
typedef struct {
  int64_t shift;
  int64_t prev;
  int have_prev;
} pts_disc_t;

/* shift to subtract from raw (and sibling dts/pts of same PES). next: expected ms. jump > PTS_DISC_MS re-bases to next */
int64_t pts_disc_shift(pts_disc_t *d, int64_t raw, int64_t next);

/* same, no state change. ref: any current media position. for side channels (teletext) */
int64_t pts_disc_peek(const pts_disc_t *d, int64_t raw, int64_t ref);

#endif
