/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_FMP4_FMP4_H
#define DVBIPITOOLS_LIB_MUX_FMP4_FMP4_H

#include <stddef.h>
#include <stdint.h>

#include "lib/demux/psi/psi.h"

#define FMP4_MAX_TRACKS 4
#define FMP4_CPRIV_MAX 512 /* avcC/hvcC config record, copied in */

typedef struct {
  codec_t codec; /* CODEC_H264/HEVC (video) or CODEC_AAC/AAC_LATM/AC3/EAC3/MP2A (audio) */
  unsigned track_id;
  unsigned timescale;
  unsigned width, height;   /* video only */
  unsigned rate, channels;  /* audio only */
  const unsigned char *cpriv; /* video: avcC/hvcC. AAC/AAC_LATM: AudioSpecificConfig. else: unused */
  size_t cpriv_len;
  unsigned char ac3_bsid, ac3_bsmod, ac3_acmod, ac3_lfeon; /* AC3/EAC3 dac3/dec3 fields */
  unsigned ac3_bitrate_code; /* AC3: 5-bit frmsizecod. EAC3: 13-bit data_rate estimate, kbps */
} fmp4_track_cfg_t;

typedef struct {
  int track_idx; /* index into tracks[] passed to fmp4_mux_new */
  const unsigned char *data; /* AVCC: 4-byte-BE-length-prefixed NALs */
  size_t size;
  uint32_t duration;   /* track timescale units */
  int32_t cts_offset;  /* PTS - DTS, track timescale units */
  int keyframe;
} fmp4_sample_t;

typedef struct fmp4_mux fmp4_mux_t;

fmp4_mux_t *fmp4_mux_new(const fmp4_track_cfg_t *tracks, int ntracks);
void fmp4_mux_free(fmp4_mux_t *m);

/* ftyp+moov, call once. *out valid until next fmp4_* call on m */
size_t fmp4_init_segment(fmp4_mux_t *m, unsigned char **out);

void fmp4_segment_begin(fmp4_mux_t *m, uint32_t sequence_number);
void fmp4_segment_add_sample(fmp4_mux_t *m, const fmp4_sample_t *s);
void fmp4_track_seed_dts(fmp4_mux_t *m, int track_idx, uint64_t dts);

/* styp+moof+mdat. *out valid until next fmp4_* call on m */
size_t fmp4_segment_end(fmp4_mux_t *m, unsigned char **out);

#endif
