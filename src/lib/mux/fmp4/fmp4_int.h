/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_FMP4_FMP4_INT_H
#define DVBIPITOOLS_LIB_MUX_FMP4_FMP4_INT_H

#include "box.h"
#include "fmp4.h"

typedef struct {
  fmp4_track_cfg_t cfg;
  unsigned char cpriv[FMP4_CPRIV_MAX];
  uint64_t next_dts;
  uint64_t frag_base_dts;
} fmp4_trk_t;

typedef struct {
  uint32_t duration;
  uint32_t size;
  int32_t cts_offset;
  int keyframe;
} frag_sample_t;

typedef struct {
  frag_sample_t *samples;
  int nsamples, cap;
  unsigned char *data;
  size_t data_len, data_cap;
} frag_track_t;

struct fmp4_mux {
  fmp4_trk_t trk[FMP4_MAX_TRACKS];
  int ntrk;
  mp4buf_t out;
  uint32_t seq;
  frag_track_t frag[FMP4_MAX_TRACKS];
};

/* fmp4_moov.c */
void build_mvhd(mp4buf_t *out, int ntrk);
void build_trak(mp4buf_t *out, const fmp4_trk_t *t);
void build_mvex(mp4buf_t *out, const fmp4_mux_t *m);

#endif
