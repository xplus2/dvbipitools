/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_FMP4_SAMPLE_H
#define DVBIPITOOLS_LIB_DEMUX_FMP4_SAMPLE_H

#include <stddef.h>
#include <stdint.h>

#include "box.h"

typedef struct {
  unsigned track_id;
  int default_base_is_moof;
  uint64_t base_data_offset;
  int have_base_data_offset;
  uint32_t default_sample_duration;
  int have_default_sample_duration;
  uint32_t default_sample_size;
  int have_default_sample_size;
  uint32_t default_sample_flags;
  int have_default_sample_flags;
} fmp4_tfhd_t;

int fmp4_parse_tfhd(const unsigned char *body, size_t len, fmp4_tfhd_t *out);

int fmp4_parse_tfdt(const unsigned char *body, size_t len, uint64_t *base_decode_time);

typedef struct {
  const unsigned char *data;
  size_t size;
  uint32_t duration;
  int32_t cts_offset;
  uint32_t flags;
} fmp4_dec_sample_t;

#define FMP4_MAX_SAMPLES 512

unsigned fmp4_parse_trun_samples(const unsigned char *trun_body, size_t trun_len, const fmp4_tfhd_t *tfhd, const fmp4_box_t *moof, const unsigned char *mdat_body, size_t mdat_len, fmp4_dec_sample_t *out, unsigned max);

int fmp4_sample_is_keyframe(uint32_t sample_flags);

#endif
