/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "sample.h"

#include <string.h>

#define FMP4_OPT_U32(flag_bit, bail, dest, valexpr, have) { \
  if (flags & (flag_bit)) { \
    if (end - p < 4) { bail; } \
    (dest) = (valexpr); \
    have; \
    p += 4; \
  } \
}

int fmp4_parse_tfhd(const unsigned char *body, size_t len, fmp4_tfhd_t *out) {
  const unsigned char *p = body;
  const unsigned char *end = body + len;
  uint32_t flags;
  memset(out, 0, sizeof *out);
  if (len < 8) return 0;
  flags = fmp4_rb_u24(p + 1);
  p += 4;
  out->track_id = fmp4_rb_u32(p);
  p += 4;
  if (flags & 0x000001) {
    if (end - p < 8) return 0;
    out->base_data_offset = fmp4_rb_u64(p);
    out->have_base_data_offset = 1;
    p += 8;
  }
  if (flags & 0x000002) {
    if (end - p < 4) return 0;
    p += 4;
  }
  FMP4_OPT_U32(0x000008, return 0, out->default_sample_duration, fmp4_rb_u32(p), out->have_default_sample_duration = 1);
  FMP4_OPT_U32(0x000010, return 0, out->default_sample_size, fmp4_rb_u32(p), out->have_default_sample_size = 1);
  FMP4_OPT_U32(0x000020, return 0, out->default_sample_flags, fmp4_rb_u32(p), out->have_default_sample_flags = 1);
  out->default_base_is_moof = (flags & 0x020000) != 0;
  return 1;
}

int fmp4_parse_tfdt(const unsigned char *body, size_t len, uint64_t *base_decode_time) {
  if (len < 4) return 0;
  if (body[0] == 1) {
    if (len < 12) return 0;
    *base_decode_time = fmp4_rb_u64(body + 4);
    return 1;
  }
  if (len < 8) return 0;
  *base_decode_time = fmp4_rb_u32(body + 4);
  return 1;
}

int fmp4_sample_is_keyframe(uint32_t sample_flags) { return !(sample_flags & 0x00010000); }

unsigned fmp4_parse_trun_samples(const unsigned char *trun_body, size_t trun_len, const fmp4_tfhd_t *tfhd, const fmp4_box_t *moof, const unsigned char *mdat_body, size_t mdat_len, fmp4_dec_sample_t *out, unsigned max) {
  const unsigned char *p = trun_body;
  const unsigned char *end = trun_body + trun_len;
  uint32_t flags;
  uint32_t sample_count;
  int32_t data_offset = 0;
  int have_data_offset = 0;
  uint32_t first_sample_flags = 0;
  int have_first_sample_flags = 0;
  const unsigned char *cursor;
  unsigned n = 0;
  unsigned i;

  if (trun_len < 8) return 0;
  flags = fmp4_rb_u24(p + 1);
  p += 4;
  sample_count = fmp4_rb_u32(p);
  p += 4;
  FMP4_OPT_U32(0x000001, return 0, data_offset, (int32_t)fmp4_rb_u32(p), have_data_offset = 1);
  FMP4_OPT_U32(0x000004, return 0, first_sample_flags, fmp4_rb_u32(p), have_first_sample_flags = 1);
  if (tfhd->have_base_data_offset && !tfhd->default_base_is_moof) return 0;
  cursor = moof->start + (have_data_offset ? data_offset : 0);
  if (cursor < mdat_body || (size_t)(cursor - mdat_body) > mdat_len) return 0;

  for (i = 0; i < sample_count && n < max; i++) {
    fmp4_dec_sample_t *s = &out[n];
    uint32_t duration = tfhd->have_default_sample_duration ? tfhd->default_sample_duration : 0;
    uint32_t size = tfhd->have_default_sample_size ? tfhd->default_sample_size : 0;
    uint32_t sflags = (have_first_sample_flags && i == 0) ? first_sample_flags : (tfhd->have_default_sample_flags ? tfhd->default_sample_flags : 0);
    int32_t cts = 0;
    FMP4_OPT_U32(0x000100, break, duration, fmp4_rb_u32(p), (void)0);
    FMP4_OPT_U32(0x000200, break, size, fmp4_rb_u32(p), (void)0);
    FMP4_OPT_U32(0x000400, break, sflags, fmp4_rb_u32(p), (void)0);
    FMP4_OPT_U32(0x000800, break, cts, (int32_t)fmp4_rb_u32(p), (void)0);
    if ((size_t)(cursor - mdat_body) + size > mdat_len) break;
    s->data = cursor;
    s->size = size;
    s->duration = duration;
    s->cts_offset = cts;
    s->flags = sflags;
    cursor += size;
    n++;
  }
  return n;
}
