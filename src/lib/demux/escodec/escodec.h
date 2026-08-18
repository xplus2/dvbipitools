/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_ESCODEC_H
#define DVBIPITOOLS_LIB_DEMUX_ESCODEC_H

#include <stddef.h>

#include "lib/demux/bitreader.h"
#include "lib/demux/psi/psi.h"

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
  unsigned char au[ESCODEC_AU_MAX];
  int latm_cfg_ok, latm_flt;
} esc_track_t;

typedef struct {
  size_t consumed;
  const unsigned char *out;
  size_t outlen;
  unsigned rate, ch, samples;
  int layer;
} esc_frame_t;

/* video.c */
int h264_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h);
int hevc_info(const unsigned char *nal, size_t len, unsigned char *ptl, unsigned *chroma, unsigned *w, unsigned *h);
size_t build_avcc(const esc_track_t *t, unsigned char *o, size_t cap);
size_t build_hvcc(const esc_track_t *t, unsigned char *o, size_t cap);

/* audio.c */
int next_frame(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);

#endif
