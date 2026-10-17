/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_ESBUILD_H
#define DVBIPITOOLS_LIB_MUX_ESBUILD_H

#include <stddef.h>

#include "lib/demux/fmp4/sample.h"
#include "lib/demux/fmp4/track.h"
#include "lib/demux/psi/psi.h"

#define ESBUILD_PS_POOL 4096

/* offset and length into esbuild_track_t.ps_pool, copy-safe */
typedef struct {
  unsigned short off;
  unsigned short len;
} esbuild_ps_t;

typedef struct {
  codec_t codec;
  unsigned nal_len_size;
  esbuild_ps_t vps[FMP4_PS_MAX];
  unsigned n_vps;
  esbuild_ps_t sps[FMP4_PS_MAX];
  unsigned n_sps;
  esbuild_ps_t pps[FMP4_PS_MAX];
  unsigned n_pps;
  unsigned char ps_pool[ESBUILD_PS_POOL];
  unsigned ps_used;
  esbuild_ps_t av1_cfg;
  unsigned aac_object_type;
  unsigned aac_sr_index;
  unsigned aac_channels;
} esbuild_track_t;

/* copies parameter sets, stsd may be freed afterwards. 0 if they do not fit ps_pool */
int esbuild_track_init(esbuild_track_t *out, const fmp4_stsd_entry_t *stsd);

/* upper bound of esbuild_convert_sample() output for a sample of sample_size bytes */
size_t esbuild_es_bound(const esbuild_track_t *t, size_t sample_size);

size_t esbuild_convert_sample(const esbuild_track_t *t, const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap);

#endif
