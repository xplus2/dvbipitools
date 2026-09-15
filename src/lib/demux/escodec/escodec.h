/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_ESCODEC_H
#define DVBIPITOOLS_LIB_DEMUX_ESCODEC_H

#include <stddef.h>

#include "lib/demux/bitreader.h"
#include "lib/demux/psi/psi.h"
#include "lib/helper/log.h"

#define ESCODEC_PS_MAX 512 /* SPS/PPS/VPS */
#define ESCODEC_AU_MAX 8192
#define ESCODEC_CPRIV_MAX 1024

/* container-agnostic codec state: avcC/hvcC/ASC, param sets, LATM cache */
typedef struct {
  codec_t codec;
  unsigned char cpriv[ESCODEC_CPRIV_MAX]; /* codecpriv: ASC/avcC/hvcC */
  size_t cpriv_len;
  unsigned rate, channels;
  unsigned char vps[ESCODEC_PS_MAX], sps[ESCODEC_PS_MAX], pps[ESCODEC_PS_MAX];
  size_t vpslen, spslen, ppslen;
  unsigned char ptl[12]; /* HEVC profile_tier_level */
  unsigned chroma;
  unsigned av1_reduced_still_picture_header;
  unsigned char au[ESCODEC_AU_MAX];
  int latm_cfg_ok, latm_flt;
  unsigned truehd_samples;
  log_throttle_t vbuf_drop_throttle;
} esc_track_t;

typedef struct {
  size_t consumed;
  const unsigned char *out;
  size_t outlen;
  unsigned rate, ch, samples;
  int layer;
  unsigned bsid, bsmod, acmod, lfeon; /* AC3/EAC3 dac3/dec3 fields, unset otherwise */
  unsigned bitrate_code;              /* AC3 only: 5-bit frmsizecod, dac3's bit_rate_code */
  int atmos;
  unsigned truehd_format_info, truehd_peak_data_rate;
  int dts_has_core;
  int ac4_iframe;
  unsigned ac4_bitstream_version;
  unsigned ac4_presentation_version, ac4_mdcompat;
} esc_frame_t;

typedef struct {
  unsigned seq_profile, seq_level_idx0, seq_tier0;
  unsigned high_bitdepth, twelve_bit, monochrome;
  unsigned subsampling_x, subsampling_y, chroma_sample_pos;
} av1_seq_hdr_t;

/* video.c */
int h264_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h);
int hevc_info(const unsigned char *nal, size_t len, unsigned char *ptl, unsigned *chroma, unsigned *w, unsigned *h);
int vvc_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h);
int av1_seq_hdr_info(const unsigned char *obu, size_t len, av1_seq_hdr_t *info, unsigned *w, unsigned *h);
size_t build_avcc(const esc_track_t *t, unsigned char *o, size_t cap);
size_t build_hvcc(const esc_track_t *t, unsigned char *o, size_t cap);
size_t build_vvcc(const esc_track_t *t, unsigned char *o, size_t cap);
size_t build_av1c(const av1_seq_hdr_t *info, const unsigned char *sps, size_t spslen, unsigned char *o, size_t cap);

/* audio.c */
int next_frame(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);

#endif
