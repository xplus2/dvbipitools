/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_ESBUILD_H
#define DVBIPITOOLS_LIB_MUX_ESBUILD_H

#include <stddef.h>

#include "lib/demux/fmp4/sample.h"
#include "lib/demux/fmp4/track.h"
#include "lib/demux/psi/psi.h"

typedef struct {
  codec_t codec;
  fmp4_nal_t vps[FMP4_PS_MAX];
  unsigned n_vps;
  fmp4_nal_t sps[FMP4_PS_MAX];
  unsigned n_sps;
  fmp4_nal_t pps[FMP4_PS_MAX];
  unsigned n_pps;
  unsigned aac_object_type;
  unsigned aac_sr_index;
  unsigned aac_channels;
} esbuild_track_t;

void esbuild_track_init(esbuild_track_t *out, const fmp4_stsd_entry_t *stsd);

size_t esbuild_convert_sample(const esbuild_track_t *t, const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap);

#endif
