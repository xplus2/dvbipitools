/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "esbuild.h"

#include <string.h>

static int is_video_codec(codec_t c) { return c == CODEC_H264 || c == CODEC_HEVC || c == CODEC_VVC; }

static unsigned nal_header_len(codec_t c) { return c == CODEC_H264 ? 1 : 2; }

/* IDR-type NAL numbers: H264 5 (ITU-T H.264 Table 7-1). HEVC 19/20 IDR_W_RADL/IDR_N_LP
   (ITU-T H.265 Table 7-1). VVC 7/8 IDR_W_RADL/IDR_N_LP (ITU-T H.266 Table 5) */
static int nal_is_idr(codec_t c, const unsigned char *nal, size_t len) {
  if (c == CODEC_H264) return len >= 1 && (nal[0] & 0x1F) == 5;
  if (len < 2) return 0;
  if (c == CODEC_HEVC) {
    unsigned t = (nal[0] >> 1) & 0x3F;
    return t == 19 || t == 20;
  }
  if (c == CODEC_VVC) {
    unsigned t = (nal[1] >> 3) & 0x1F;
    return t == 7 || t == 8;
  }
  return 0;
}

static size_t emit_nal(unsigned char *out, size_t outcap, size_t pos, const unsigned char *data, size_t len) {
  static const unsigned char start_code[4] = {0, 0, 0, 1};
  if (pos + 4 + len > outcap) return (size_t)-1;
  memcpy(out + pos, start_code, 4);
  memcpy(out + pos + 4, data, len);
  return pos + 4 + len;
}

static size_t convert_video(const esbuild_track_t *t, const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap) {
  const unsigned char *p = sample->data;
  const unsigned char *end = p + sample->size;
  size_t pos = 0;
  int injected = 0;
  unsigned hdrlen = nal_header_len(t->codec);

  while ((size_t)(end - p) >= 4) {
    uint32_t len = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    p += 4;
    if ((size_t)(end - p) < len || len < hdrlen) break;
    if (!injected && nal_is_idr(t->codec, p, len)) {
      unsigned i;
      for (i = 0; i < t->n_vps; i++) {
        pos = emit_nal(out, outcap, pos, t->vps[i].data, t->vps[i].len);
        if (pos == (size_t)-1) return 0;
      }
      for (i = 0; i < t->n_sps; i++) {
        pos = emit_nal(out, outcap, pos, t->sps[i].data, t->sps[i].len);
        if (pos == (size_t)-1) return 0;
      }
      for (i = 0; i < t->n_pps; i++) {
        pos = emit_nal(out, outcap, pos, t->pps[i].data, t->pps[i].len);
        if (pos == (size_t)-1) return 0;
      }
      injected = 1;
    }
    pos = emit_nal(out, outcap, pos, p, len);
    if (pos == (size_t)-1) return 0;
    p += len;
  }
  return pos;
}

static size_t convert_aac_adts(const esbuild_track_t *t, const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap) {
  size_t framelen = 7 + sample->size;
  unsigned profile = t->aac_object_type ? t->aac_object_type - 1 : 1;
  if (framelen > outcap || framelen > 0x1FFF || t->aac_sr_index > 12) return 0;
  out[0] = 0xFF;
  out[1] = 0xF1;
  out[2] = (unsigned char)(((profile & 3) << 6) | ((t->aac_sr_index & 0xF) << 2) | ((t->aac_channels >> 2) & 1));
  out[3] = (unsigned char)(((t->aac_channels & 3) << 6) | ((framelen >> 11) & 3));
  out[4] = (unsigned char)((framelen >> 3) & 0xFF);
  out[5] = (unsigned char)(((framelen & 7) << 5) | 0x1F);
  out[6] = 0xFC;
  memcpy(out + 7, sample->data, sample->size);
  return framelen;
}

static size_t convert_passthrough(const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap) {
  if (sample->size > outcap) return 0;
  memcpy(out, sample->data, sample->size);
  return sample->size;
}

void esbuild_track_init(esbuild_track_t *out, const fmp4_stsd_entry_t *stsd) {
  unsigned i;
  memset(out, 0, sizeof *out);
  out->codec = stsd->codec;
  for (i = 0; i < stsd->n_vps; i++) out->vps[i] = stsd->vps[i];
  out->n_vps = stsd->n_vps;
  for (i = 0; i < stsd->n_sps; i++) out->sps[i] = stsd->sps[i];
  out->n_sps = stsd->n_sps;
  for (i = 0; i < stsd->n_pps; i++) out->pps[i] = stsd->pps[i];
  out->n_pps = stsd->n_pps;
  /* 2 B mini AudioSpecificConfig (next_aac(): aot<<3|sfi>>1, (sfi&1)<<7|chcfg<<3) */
  if (stsd->codec == CODEC_AAC && stsd->cpriv_len >= 2) {
    const unsigned char *c = stsd->cpriv;
    out->aac_object_type = c[0] >> 3;
    out->aac_sr_index = (unsigned)((c[0] & 0x7) << 1) | (c[1] >> 7);
    out->aac_channels = (c[1] >> 3) & 0xF;
  }
}

size_t esbuild_convert_sample(const esbuild_track_t *t, const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap) {
  if (is_video_codec(t->codec)) return convert_video(t, sample, out, outcap);
  if (t->codec == CODEC_AAC) return convert_aac_adts(t, sample, out, outcap);
  return convert_passthrough(sample, out, outcap);
}
