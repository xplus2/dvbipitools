/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "track.h"

#include <string.h>

static codec_t codec_for_fourcc(const char *fourcc, int *is_video) {
  static const struct { const char fourcc[5]; codec_t codec; int is_video; } map[] = {
{"avc1", CODEC_H264, 1}, {"hvc1", CODEC_HEVC, 1}, {"vvc1", CODEC_VVC, 1}, {"av01", CODEC_AV1, 1}, {"lvc1", CODEC_LCEVC, 1},
{"ac-3", CODEC_AC3, 0}, {"ec-3", CODEC_EAC3, 0}, {"Opus", CODEC_OPUS, 0}, {"dtsc", CODEC_DTS, 0}, {"dtse", CODEC_DTS_HD, 0},
{"dtsl", CODEC_DTS_HD_MA, 0},
{"dtsh", CODEC_DTS_HD_MA, 0}, /* has_core=1 for both DTS_HD and DTS_HD_MA, fourcc alone can't tell them apart */
{"mlpa", CODEC_TRUEHD, 0}, {"ac-4", CODEC_AC4, 0},
{"mp4a", CODEC_AAC, 0}, /* esds oti disambiguates AAC-family vs MP2A below */
  };
  size_t i;
  for (i = 0; i < sizeof map / sizeof map[0]; i++)
    if (!memcmp(fourcc, map[i].fourcc, 4)) {
      *is_video = map[i].is_video;
      return map[i].codec;
    }
  *is_video = -1;
  return CODEC_NONE;
}

static void add_ps(fmp4_nal_t *arr, unsigned *n, const unsigned char *data, size_t len) {
  if (*n >= FMP4_PS_MAX) return;
  arr[*n].data = data;
  arr[*n].len = len;
  (*n)++;
}

static void parse_avcc(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  unsigned n_sps, n_pps, i;
  if (end - p < 6) return;
  p += 5; /* configurationVersion, profile, compat, level, lengthSizeMinusOne-byte */
  n_sps = *p++ & 0x1F;
  for (i = 0; i < n_sps && end - p >= 2; i++) {
    size_t len = fmp4_rb_u16(p);
    p += 2;
    if ((size_t)(end - p) < len) return;
    add_ps(out->sps, &out->n_sps, p, len);
    p += len;
  }
  if (end - p < 1) return;
  n_pps = *p++;
  for (i = 0; i < n_pps && end - p >= 2; i++) {
    size_t len = fmp4_rb_u16(p);
    p += 2;
    if ((size_t)(end - p) < len) return;
    add_ps(out->pps, &out->n_pps, p, len);
    p += len;
  }
}

static void parse_hvcc(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  unsigned num_arrays, a;
  if (end - p < 23) return;
  p += 22; /* configurationVersion(1)+ptl(12)+min_spatial_seg(2)+parallelism(1)+chroma(1)+
              bitDepthLuma(1)+bitDepthChroma(1)+avgFrameRate(2)+lengthSize/etc(1) */
  num_arrays = *p++;
  for (a = 0; a < num_arrays && end - p >= 3; a++) {
    unsigned nal_type = p[0] & 0x3F;
    unsigned num_nalus;
    unsigned i;
    p += 1;
    num_nalus = fmp4_rb_u16(p);
    p += 2;
    for (i = 0; i < num_nalus && end - p >= 2; i++) {
      size_t len = fmp4_rb_u16(p);
      p += 2;
      if ((size_t)(end - p) < len) return;
      if (nal_type == 32) add_ps(out->vps, &out->n_vps, p, len);
      else if (nal_type == 33) add_ps(out->sps, &out->n_sps, p, len);
      else if (nal_type == 34) add_ps(out->pps, &out->n_pps, p, len);
      p += len;
    }
  }
}

static void parse_esds_desc(const unsigned char **pp, const unsigned char *end, unsigned *tag_out, const unsigned char **body_out, size_t *size_out) {
  const unsigned char *p = *pp;
  unsigned tag;
  size_t size = 0;
  unsigned char b;
  if (p >= end) {
    *tag_out = 0;
    return;
  }
  tag = *p++;
  do {
    if (p >= end) {
      *tag_out = 0;
      return;
    }
    b = *p++;
    size = (size << 7) | (b & 0x7F);
  } while (b & 0x80);
  if ((size_t)(end - p) < size) {
    *tag_out = 0;
    return;
  }
  *tag_out = tag;
  *body_out = p;
  *size_out = size;
  *pp = p + size;
}

static void parse_esds(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  unsigned tag;
  const unsigned char *body;
  size_t size;
  const unsigned char *es_end, *q;
  if (end - p < 4) return;
  p += 4;
  parse_esds_desc(&p, end, &tag, &body, &size);
  if (tag != 0x03 || size < 3) return;
  es_end = body + size;
  q = body + 3; /* ES_ID(2)+flags(1) */
  while (q < es_end) {
    unsigned dtag;
    const unsigned char *dbody;
    size_t dsize;
    const unsigned char *dc_end, *r;
    parse_esds_desc(&q, es_end, &dtag, &dbody, &dsize);
    if (!dtag) break;
    if (dtag != 0x04 || dsize < 1) continue;
    if (dbody[0] == 0x6B) out->codec = CODEC_MP2A;
    dc_end = dbody + dsize;
    r = dbody + 13; /* oti(1)+streamType/upStream/reserved(1)+bufferSizeDB(3)+maxBitrate(4)+avgBitrate(4) */
    while (r < dc_end) {
      unsigned itag;
      const unsigned char *ibody;
      size_t isize;
      parse_esds_desc(&r, dc_end, &itag, &ibody, &isize);
      if (!itag) break;
      if (itag != 0x05) continue;
      out->cpriv = ibody;
      out->cpriv_len = isize;
    }
  }
}

static void parse_dac3(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  if (end - p < 3) return;
  out->ac3_bsid = (unsigned char)((p[0] >> 1) & 0x1F);
  out->ac3_bsmod = (unsigned char)(((p[0] & 0x1) << 2) | ((p[1] >> 6) & 0x3));
  out->ac3_acmod = (unsigned char)((p[1] >> 3) & 0x7);
  out->ac3_lfeon = (unsigned char)((p[1] >> 2) & 0x1);
  out->ac3_bitrate_code = ((unsigned)(p[1] & 0x3) << 3) | ((p[2] >> 5) & 0x7);
}

static void parse_dec3(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  if (end - p < 4) return;
  out->ac3_bitrate_code = (fmp4_rb_u16(p) >> 3) & 0x1FFF;
  out->ac3_bsid = (unsigned char)((p[2] >> 1) & 0x1F);
  out->ac3_bsmod = (unsigned char)((p[3] >> 5) & 0x7);
  out->ac3_acmod = (unsigned char)((p[3] >> 2) & 0x7);
  out->ac3_lfeon = (unsigned char)((p[3] >> 1) & 0x1);
}

static void parse_dops(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  if (end - p < 2) return;
  out->channels = p[1];
}

static void parse_ddts(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  if (end - p < 4) return;
  out->dts_rate = fmp4_rb_u32(p);
}

static void parse_dmlp(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  if (end - p < 6) return;
  out->truehd_format_info = fmp4_rb_u32(p);
  out->truehd_peak_data_rate = fmp4_rb_u16(p + 4) >> 1;
}

static void parse_dac4(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  out->cpriv = p;
  out->cpriv_len = (size_t)(end - p);
}

static void parse_vvcc(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out) {
  unsigned num_arrays, a;
  if (end - p < 2) return;
  num_arrays = p[1];
  p += 2; /* byte0: ptl_present_flag+reserved. byte1: numOfArrays */
  for (a = 0; a < num_arrays && end - p >= 5; a++) {
    unsigned nal_type = p[0] & 0x1F;
    unsigned num_nalus;
    unsigned i;
    p += 1; /* type_byte */
    num_nalus = fmp4_rb_u16(p);
    p += 2;
    for (i = 0; i < num_nalus && end - p >= 2; i++) {
      size_t len = fmp4_rb_u16(p);
      p += 2;
      if ((size_t)(end - p) < len) return;
      if (nal_type == 14) add_ps(out->vps, &out->n_vps, p, len); /* VVC_NAL_VPS */
      else if (nal_type == 15) add_ps(out->sps, &out->n_sps, p, len); /* VVC_NAL_SPS */
      else if (nal_type == 16) add_ps(out->pps, &out->n_pps, p, len); /* VVC_NAL_PPS */
      p += len;
    }
  }
}

static void parse_video_config(const fmp4_box_t *cfg, fmp4_stsd_entry_t *out) {
  const unsigned char *p = (const unsigned char *)cfg->body;
  const unsigned char *end = p + cfg->body_len;
  if (!memcmp(cfg->fourcc, "avcC", 4)) parse_avcc(p, end, out);
  else if (!memcmp(cfg->fourcc, "hvcC", 4)) parse_hvcc(p, end, out);
  else if (!memcmp(cfg->fourcc, "vvcC", 4)) parse_vvcc(p, end, out);
}

int fmp4_parse_stsd_entry(const fmp4_box_t *entry_box, fmp4_stsd_entry_t *out) {
  const unsigned char *body = entry_box->body;
  size_t len = entry_box->body_len;
  int is_video;
  fmp4_box_t cfg;

  memset(out, 0, sizeof *out);
  out->codec = codec_for_fourcc(entry_box->fourcc, &is_video);
  if (is_video < 0) return 0;

  if (is_video) {
    if (len < 78) return 0;
    out->width = fmp4_rb_u16(body + 24);
    out->height = fmp4_rb_u16(body + 26);
    if (fmp4_box_find(body + 78, len - 78, "avcC", &cfg) || fmp4_box_find(body + 78, len - 78, "hvcC", &cfg) || fmp4_box_find(body + 78, len - 78, "vvcC", &cfg))
      parse_video_config(&cfg, out);
    return 1;
  }

  if (len < 28) return 0;
  out->channels = fmp4_rb_u16(body + 16);
  out->rate = fmp4_rb_u32(body + 24) >> 16;

  if (fmp4_box_find(body + 28, len - 28, "dac3", &cfg)) parse_dac3(cfg.body, cfg.body + cfg.body_len, out);
  else if (fmp4_box_find(body + 28, len - 28, "dec3", &cfg)) parse_dec3(cfg.body, cfg.body + cfg.body_len, out);
  else if (fmp4_box_find(body + 28, len - 28, "dOps", &cfg)) parse_dops(cfg.body, cfg.body + cfg.body_len, out);
  else if (fmp4_box_find(body + 28, len - 28, "ddts", &cfg)) parse_ddts(cfg.body, cfg.body + cfg.body_len, out);
  else if (fmp4_box_find(body + 28, len - 28, "dmlp", &cfg)) parse_dmlp(cfg.body, cfg.body + cfg.body_len, out);
  else if (fmp4_box_find(body + 28, len - 28, "dac4", &cfg)) parse_dac4(cfg.body, cfg.body + cfg.body_len, out);
  else if (fmp4_box_find(body + 28, len - 28, "esds", &cfg)) parse_esds(cfg.body, cfg.body + cfg.body_len, out);
  return 1;
}
