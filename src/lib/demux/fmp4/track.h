/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_FMP4_TRACK_H
#define DVBIPITOOLS_LIB_DEMUX_FMP4_TRACK_H

#include <stddef.h>

#include "box.h"
#include "lib/demux/psi/psi.h"

#define FMP4_PS_MAX 4

typedef struct {
  const unsigned char *data;
  size_t len;
} fmp4_nal_t;

typedef struct {
  codec_t codec;
  unsigned width;
  unsigned height;
  unsigned rate;
  unsigned channels;
  fmp4_nal_t vps[FMP4_PS_MAX];
  unsigned n_vps;
  fmp4_nal_t sps[FMP4_PS_MAX];
  unsigned n_sps;
  fmp4_nal_t pps[FMP4_PS_MAX];
  unsigned n_pps;
  const unsigned char *cpriv;
  size_t cpriv_len;
  unsigned char ac3_bsid;
  unsigned char ac3_bsmod;
  unsigned char ac3_acmod;
  unsigned char ac3_lfeon;
  unsigned ac3_bitrate_code;
  unsigned truehd_format_info;
  unsigned truehd_peak_data_rate;
  unsigned dts_rate;
} fmp4_stsd_entry_t;

int fmp4_parse_stsd_entry(const fmp4_box_t *entry_box, fmp4_stsd_entry_t *out);

#endif
