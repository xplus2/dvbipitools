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
  unsigned rate;
  unsigned channels;
  unsigned char vps[ESCODEC_PS_MAX];
  unsigned char sps[ESCODEC_PS_MAX];
  unsigned char pps[ESCODEC_PS_MAX];
  size_t vpslen;
  size_t spslen;
  size_t ppslen;
  unsigned char ptl[12]; /* HEVC profile_tier_level */
  unsigned chroma;
  unsigned av1_reduced_still_picture_header;
  unsigned char au[ESCODEC_AU_MAX];
  int latm_cfg_ok;
  int latm_flt;
  unsigned truehd_samples;
  unsigned dts_total_ch;  /* DTS: all channels incl. LFE, extension asset wins over core */
  int dts_total_ext;
  unsigned truehd_cicp;
  log_throttle_t vbuf_drop_throttle;
} esc_track_t;

typedef enum {
  ESC_CHCFG_NONE,
  ESC_CHCFG_DOLBY_2011, /* tag:dolby.com,2014:dash:audio_channel_configuration:2011, 16 bit location mask */
  ESC_CHCFG_DOLBY_2015, /* tag:dolby.com,2015:dash:audio_channel_configuration:2015, 24 bit speaker group mask */
  ESC_CHCFG_DTS_2012,   /* tag:dts.com,2014:dash:audio_channel_configuration:2012, total channels incl. LFE */
  ESC_CHCFG_CICP        /* urn:mpeg:mpegB:cicp:ChannelConfiguration, ISO/IEC 23091-3 index */
} esc_chcfg_t;

typedef struct {
  size_t consumed;
  const unsigned char *out;
  size_t outlen;
  unsigned rate;
  unsigned ch;
  unsigned samples;
  int layer;
  unsigned bsid;  /* AC3/EAC3 dac3/dec3 fields, unset otherwise */
  unsigned bsmod;
  unsigned acmod;
  unsigned lfeon;
  unsigned bitrate_code;              /* AC3 only: frmsizecod >> 1, dac3 bit_rate_code */
  int atmos;
  unsigned truehd_format_info;
  unsigned truehd_peak_data_rate;
  int dts_has_core;
  int ac4_iframe;
  unsigned ac4_bitstream_version;
  unsigned ac4_presentation_version;
  unsigned ac4_mdcompat;
  esc_chcfg_t chcfg_scheme;
  unsigned chcfg_value;
} esc_frame_t;

typedef struct {
  unsigned seq_profile;
  unsigned seq_level_idx0;
  unsigned seq_tier0;
  unsigned high_bitdepth;
  unsigned twelve_bit;
  unsigned monochrome;
  unsigned subsampling_x;
  unsigned subsampling_y;
  unsigned chroma_sample_pos;
} av1_seq_hdr_t;

/* video.c */
void skip_scaling_list(br_t *b, int sz);
void skip_scaling_matrices(br_t *b, int n);
void br_align(br_t *b);
void skip_vvc_gci(br_t *b);
void skip_vvc_ptl(br_t *b, unsigned max_sublayers_minus1);
int h264_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h);
int hevc_info(const unsigned char *nal, size_t len, unsigned char *ptl, unsigned *chroma, unsigned *w, unsigned *h);

/* HEVC SPS luma/chroma bit depth (8..16). 0 ok, -1 malformed */
int hevc_bit_depth(const unsigned char *nal, size_t len, unsigned *luma, unsigned *chroma);
int vvc_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h);
int av1_seq_hdr_info(const unsigned char *obu, size_t len, av1_seq_hdr_t *info, unsigned *w, unsigned *h);
size_t build_avcc(const esc_track_t *t, unsigned char *o, size_t cap);
size_t build_hvcc(const esc_track_t *t, unsigned char *o, size_t cap);
size_t build_vvcc(const esc_track_t *t, unsigned char *o, size_t cap);
size_t build_av1c(const av1_seq_hdr_t *info, const unsigned char *sps, size_t spslen, unsigned char *o, size_t cap);

/* audio.c */
int next_frame(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);

#endif
