/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "esbuild.h"

#include <string.h>

static int is_video_codec(codec_t c) { return c == CODEC_H264 || c == CODEC_HEVC || c == CODEC_VVC; }

static unsigned nal_header_len(codec_t c) { return c == CODEC_H264 ? 1 : 2; }

/* Random access NALs. H264 5 IDR (ITU-T H.264 Table 7-1). HEVC 16-21 BLA/IDR/CRA
   (ITU-T H.265 Table 7-1). VVC 7-10 IDR/CRA/GDR (ITU-T H.266 Table 5) */
static int nal_is_rap(codec_t c, const unsigned char *nal, size_t len) {
  if (c == CODEC_H264) return len >= 1 && (nal[0] & 0x1F) == 5;
  if (len < 2) return 0;
  if (c == CODEC_HEVC) {
    unsigned t = (nal[0] >> 1) & 0x3F;
    return t >= 16 && t <= 21;
  }
  if (c == CODEC_VVC) {
    unsigned t = (nal[1] >> 3) & 0x1F;
    return t >= 7 && t <= 10;
  }
  return 0;
}

static int nal_is_aud(codec_t c, const unsigned char *nal, size_t len) {
  if (c == CODEC_H264) return len >= 1 && (nal[0] & 0x1F) == 9;
  if (len < 2) return 0;
  if (c == CODEC_HEVC) return ((nal[0] >> 1) & 0x3F) == 35;
  if (c == CODEC_VVC) return ((nal[1] >> 3) & 0x1F) == 20;
  return 0;
}

static size_t nal_len_read(const unsigned char *p, unsigned n) {
  size_t v = 0;
  while (n--) v = (v << 8) | *p++;
  return v;
}

static size_t emit_nal(unsigned char *out, size_t outcap, size_t pos, const unsigned char *data, size_t len) {
  static const unsigned char start_code[4] = {0, 0, 0, 1};
  if (pos + 4 + len > outcap) return (size_t)-1;
  memcpy(out + pos, start_code, 4);
  memcpy(out + pos + 4, data, len);
  return pos + 4 + len;
}

static size_t emit_ps(const esbuild_track_t *t, const esbuild_ps_t *ps, unsigned n, unsigned char *out, size_t outcap, size_t pos) {
  for (unsigned i = 0; i < n && pos != (size_t)-1; i++) pos = emit_nal(out, outcap, pos, t->ps_pool + ps[i].off, ps[i].len);
  return pos;
}

/* any picture type, rbsp trailing bits included */
static size_t emit_aud(const esbuild_track_t *t, int rap, unsigned char *out, size_t outcap, size_t pos) {
  unsigned char aud[3];
  size_t n = 0;

  if (t->codec == CODEC_H264) {
    aud[n++] = 0x09;
    aud[n++] = 0xF0;
  } else if (t->codec == CODEC_HEVC) {
    aud[n++] = 0x46;
    aud[n++] = 0x01;
    aud[n++] = 0x50;
  } else {
    aud[n++] = 0x00;
    aud[n++] = 0xA1;
    aud[n++] = rap ? 0xA8 : 0x28;
  }
  return emit_nal(out, outcap, pos, aud, n);
}

static void scan_au(codec_t c, const unsigned char *p, const unsigned char *end, unsigned lsz, int *rap, int *has_aud) {
  unsigned hdrlen = nal_header_len(c);
  int first = 1;

  *rap = 0;
  *has_aud = 0;
  while ((size_t)(end - p) >= lsz) {
    size_t len = nal_len_read(p, lsz);
    p += lsz;
    if ((size_t)(end - p) < len || len < hdrlen) return;
    if (first) *has_aud = nal_is_aud(c, p, len);
    first = 0;
    if (nal_is_rap(c, p, len)) {
      *rap = 1;
      return;
    }
    p += len;
  }
}

static size_t convert_video(const esbuild_track_t *t, const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap) {
  const unsigned char *p = sample->data;
  const unsigned char *end = p + sample->size;
  size_t pos = 0;
  int injected = 0;
  int rap;
  int has_aud;
  unsigned hdrlen = nal_header_len(t->codec);
  unsigned lsz = t->nal_len_size ? t->nal_len_size : 4;

  scan_au(t->codec, p, end, lsz, &rap, &has_aud);
  while ((size_t)(end - p) >= lsz) {
    size_t len = nal_len_read(p, lsz);
    p += lsz;
    if ((size_t)(end - p) < len || len < hdrlen) break;
    if (!pos && !has_aud) {
      pos = emit_aud(t, rap, out, outcap, pos);
      if (pos == (size_t)-1) return 0;
    }
    if (!injected && nal_is_rap(t->codec, p, len)) {
      pos = emit_ps(t, t->vps, t->n_vps, out, outcap, pos);
      if (pos != (size_t)-1) pos = emit_ps(t, t->sps, t->n_sps, out, outcap, pos);
      if (pos != (size_t)-1) pos = emit_ps(t, t->pps, t->n_pps, out, outcap, pos);
      if (pos == (size_t)-1) return 0;
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
  /* ADTS profile is 2 bit aot-1, SBR/PS (5, 29) ride on LC core */
  unsigned profile = (t->aac_object_type >= 1 && t->aac_object_type <= 4) ? t->aac_object_type - 1 : 1;
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

static int copy_ps(esbuild_track_t *t, esbuild_ps_t *dst, unsigned *n_dst, const fmp4_nal_t *src, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    if (src[i].len > sizeof t->ps_pool - t->ps_used) return 0;
    memcpy(t->ps_pool + t->ps_used, src[i].data, src[i].len);
    dst[i].off = (unsigned short)t->ps_used;
    dst[i].len = (unsigned short)src[i].len;
    t->ps_used += (unsigned)src[i].len;
  }
  *n_dst = n;
  return 1;
}

int esbuild_track_init(esbuild_track_t *out, const fmp4_stsd_entry_t *stsd) {
  memset(out, 0, sizeof *out);
  out->codec = stsd->codec;
  out->nal_len_size = stsd->nal_len_size ? stsd->nal_len_size : 4;
  if (!copy_ps(out, out->vps, &out->n_vps, stsd->vps, stsd->n_vps) || !copy_ps(out, out->sps, &out->n_sps, stsd->sps, stsd->n_sps) || !copy_ps(out, out->pps, &out->n_pps, stsd->pps, stsd->n_pps)) return 0;
  /* 2 B mini AudioSpecificConfig (next_aac(): aot<<3|sfi>>1, (sfi&1)<<7|chcfg<<3) */
  if (stsd->codec == CODEC_AAC && stsd->cpriv_len >= 2) {
    const unsigned char *c = stsd->cpriv;
    out->aac_object_type = c[0] >> 3;
    out->aac_sr_index = (unsigned)((c[0] & 0x7) << 1) | (c[1] >> 7);
    out->aac_channels = (c[1] >> 3) & 0xF;
  }
  return 1;
}

/* start codes outgrow short length fields, every NAL has at least hdrlen payload B */
size_t esbuild_es_bound(const esbuild_track_t *t, size_t sample_size) {
  size_t grow = 0;

  if (is_video_codec(t->codec) && t->nal_len_size < 4) grow = (sample_size / (t->nal_len_size + nal_header_len(t->codec)) + 1) * (4 - t->nal_len_size);
  return sample_size + grow + t->ps_used + 4 * (size_t)(t->n_vps + t->n_sps + t->n_pps) + 7 + 7;
}

size_t esbuild_convert_sample(const esbuild_track_t *t, const fmp4_dec_sample_t *sample, unsigned char *out, size_t outcap) {
  if (is_video_codec(t->codec)) return convert_video(t, sample, out, outcap);
  if (t->codec == CODEC_AAC) return convert_aac_adts(t, sample, out, outcap);
  return convert_passthrough(sample, out, outcap);
}
