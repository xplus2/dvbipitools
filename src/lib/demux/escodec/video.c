/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include "aubuild.h"
#include "escodec.h"

/* skips one scaling list (H.264 SPS scaling_list()): sz entries, delta-coded, wraps mod 256 */
static void skip_scaling_list(br_t *b, int sz) {
  int last = 8, next = 8;
  for (int j = 0; j < sz; j++) {
    if (next)
      next = (last + br_se(b) + 256) % 256;
    last = next ? next : last;
  }
}

static void skip_scaling_matrices(br_t *b, int n) {
  for (int k = 0; k < n; k++)
    if (br_u(b, 1))
      skip_scaling_list(b, (k < 6) ? 16 : 64);
}

/* H.264 SPS -> dimensions */
int h264_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h) {
  unsigned char rb[ESCODEC_PS_MAX];
  br_t b;
  unsigned profile, chroma = 1, wmbs, hmus, fmo, poc;
  unsigned cl = 0, cr = 0, ct = 0, cb = 0, subw, subh;
  int high_profile;
  b.len = rbsp_unescape(nal, len, rb, sizeof rb);
  b.d = rb;
  b.bit = 0;
  b.err = 0;
  br_u(&b, 8); /* NAL header */
  profile = br_u(&b, 8);
  br_u(&b, 16); /* constraints + level */
  br_ue(&b);    /* sps id */
  switch (profile) { /* Annex A profiles carrying chroma_format_idc et al */
    case 44: case 83: case 86: case 100: case 110: case 118:
    case 122: case 128: case 134: case 135: case 138: case 139:
    case 244:
      high_profile = 1;
      break;
    default:
      high_profile = 0;
  }
  if (high_profile) {
    chroma = br_ue(&b);
    if (chroma == 3)
      br_u(&b, 1);
    br_ue(&b);
    br_ue(&b);
    br_u(&b, 1);
    if (br_u(&b, 1)) /* scaling matrices */
      skip_scaling_matrices(&b, (chroma != 3) ? 8 : 12);
  }
  br_ue(&b); /* log2_max_frame_num_minus4 */
  poc = br_ue(&b);
  if (poc == 0) {
    br_ue(&b);
  } else if (poc == 1) {
    unsigned n;
    br_u(&b, 1);
    br_se(&b);
    br_se(&b);
    n = br_ue(&b);
    for (unsigned k = 0; k < n && !b.err; k++)
      br_se(&b);
  }
  br_ue(&b); /* max_num_ref_frames */
  br_u(&b, 1);
  wmbs = br_ue(&b);
  hmus = br_ue(&b);
  fmo = br_u(&b, 1);
  if (!fmo)
    br_u(&b, 1);
  br_u(&b, 1);
  if (br_u(&b, 1)) {
    cl = br_ue(&b);
    cr = br_ue(&b);
    ct = br_ue(&b);
    cb = br_ue(&b);
  }
  if (b.err)
    return -1;
  subw = (chroma == 1 || chroma == 2) ? 2 : 1;
  subh = (chroma == 1) ? 2 : 1;
  if (chroma == 0)
    subw = subh = 1;
  *w = (wmbs + 1) * 16 - (cl + cr) * subw;
  *h = (2 - fmo) * (hmus + 1) * 16 - (ct + cb) * subh * (2 - fmo);
  return (*w && *h) ? 0 : -1;
}

/* HEVC SPS -> profile_tier_level, chroma, dimensions */
int hevc_info(const unsigned char *nal, size_t len, unsigned char *ptl, unsigned *chroma, unsigned *w, unsigned *h) {
  unsigned char rb[ESCODEC_PS_MAX];
  br_t b;
  unsigned maxsub;
  b.len = rbsp_unescape(nal, len, rb, sizeof rb);
  b.d = rb;
  b.bit = 0;
  b.err = 0;
  br_u(&b, 16); /* 2-byte NAL header */
  br_u(&b, 4);  /* sps_video_parameter_set_id */
  maxsub = br_u(&b, 3);
  br_u(&b, 1);
  for (unsigned i = 0; i < 12; i++)
    ptl[i] = (unsigned char)br_u(&b, 8);
  if (maxsub > 0) {
    if (maxsub > 7)
      return -1;
    unsigned sp[8], sl[8];
    for (unsigned i = 0; i < maxsub; i++) {
      sp[i] = br_u(&b, 1);
      sl[i] = br_u(&b, 1);
    }
    for (unsigned i = maxsub; i < 8; i++)
      br_u(&b, 2);
    for (unsigned i = 0; i < maxsub; i++) {
      if (sp[i]) {
        br_u(&b, 32);
        br_u(&b, 32);
        br_u(&b, 24);
      }
      if (sl[i])
        br_u(&b, 8);
    }
  }
  br_ue(&b); /* sps_seq_parameter_set_id */
  *chroma = br_ue(&b);
  if (*chroma == 3)
    br_u(&b, 1);
  *w = br_ue(&b);
  *h = br_ue(&b);
  if (br_u(&b, 1)) { /* conformance window */
    unsigned l = br_ue(&b), r = br_ue(&b), t = br_ue(&b), bo = br_ue(&b);
    unsigned subw = (*chroma == 1 || *chroma == 2) ? 2 : 1;
    unsigned subh = (*chroma == 1) ? 2 : 1;
    *w -= (l + r) * subw;
    *h -= (t + bo) * subh;
  }
  if (b.err || !*w || !*h)
    return -1;
  return 0;
}

static void br_align(br_t *b) {
  b->bit = (b->bit + 7) & ~(size_t)7;
}

static void skip_vvc_gci(br_t *b) {
  unsigned gci_present = br_u(b, 1);
  if (gci_present) {
    unsigned num_additional_bits;
    for (int i = 0; i < 69; i++) br_u(b, 1);
    num_additional_bits = br_u(b, 8);
    for (unsigned i = 0; i < num_additional_bits; i++) br_u(b, 1);
  }
  br_align(b);
}

static void skip_vvc_ptl(br_t *b, unsigned max_sublayers_minus1) {
  unsigned char sublayer_level_present[8];
  unsigned i;
  unsigned num_sub_profiles;
  br_u(b, 7 + 1 + 8 + 1 + 1); /* profile_idc, tier, level, frame_only, multilayer */
  skip_vvc_gci(b);
  if (max_sublayers_minus1 > 7) max_sublayers_minus1 = 7;
  for (i = 0; i < max_sublayers_minus1; i++) sublayer_level_present[i] = (unsigned char)br_u(b, 1);
  br_align(b);
  for (i = 0; i < max_sublayers_minus1; i++) if (sublayer_level_present[i]) br_u(b, 8);
  num_sub_profiles = br_u(b, 8);
  for (i = 0; i < num_sub_profiles; i++) br_u(b, 32);
}

int vvc_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h) {
  unsigned char rb[ESCODEC_PS_MAX];
  br_t b;
  unsigned max_sublayers_minus1;
  unsigned ptl_dpb_hrd_present;
  b.len = rbsp_unescape(nal, len, rb, sizeof rb);
  b.d = rb;
  b.bit = 0;
  b.err = 0;
  br_u(&b, 16 + 4 + 4);
  max_sublayers_minus1 = br_u(&b, 3);
  br_u(&b, 2 + 2);
  ptl_dpb_hrd_present = br_u(&b, 1);
  if (!ptl_dpb_hrd_present) return -1; /* multi-layer VPS-only ptl signaling, out of scope */
  skip_vvc_ptl(&b, max_sublayers_minus1);
  br_u(&b, 1);
  if (br_u(&b, 1)) br_u(&b, 1);
  *w = br_ue(&b);
  *h = br_ue(&b);
  if (b.err || !*w || !*h) return -1;
  return 0;
}

size_t build_avcc(const esc_track_t *t, unsigned char *o, size_t cap) {
  size_t n = 0;

  if (t->spslen < 4 || 11 + t->spslen + t->ppslen > cap)
    return 0;
  o[n++] = 1;
  o[n++] = t->sps[1]; /* profile */
  o[n++] = t->sps[2]; /* compat */
  o[n++] = t->sps[3]; /* level */
  o[n++] = 0xFF;      /* 4-byte NALU length */
  o[n++] = 0xE1;      /* 1 SPS */
  o[n++] = (unsigned char)(t->spslen >> 8);
  o[n++] = (unsigned char)t->spslen;
  memcpy(o + n, t->sps, t->spslen);
  n += t->spslen;
  o[n++] = 1; /* 1 PPS */
  o[n++] = (unsigned char)(t->ppslen >> 8);
  o[n++] = (unsigned char)t->ppslen;
  memcpy(o + n, t->pps, t->ppslen);
  n += t->ppslen;
  return n;
}

size_t build_vvcc(const esc_track_t *t, unsigned char *o, size_t cap) {
  static const unsigned char types[3] = {VVC_NAL_VPS, VVC_NAL_SPS, VVC_NAL_PPS};
  const unsigned char *ps[3];
  size_t pl[3];
  size_t n = 0;

  ps[0] = t->vps;
  pl[0] = t->vpslen;
  ps[1] = t->sps;
  pl[1] = t->spslen;
  ps[2] = t->pps;
  pl[2] = t->ppslen;
  if (2 + pl[0] + pl[1] + pl[2] + 3 * 5 > cap) return 0;
  o[n++] = 0xFE; /* ptl_present_flag=0: no NativePTL block */
  o[n++] = 3;
  for (size_t i = 0; i < 3; i++) {
    o[n++] = (unsigned char)(0x80 | types[i]);
    o[n++] = 0;
    o[n++] = 1;
    o[n++] = (unsigned char)(pl[i] >> 8);
    o[n++] = (unsigned char)pl[i];
    memcpy(o + n, ps[i], pl[i]);
    n += pl[i];
  }
  return n;
}

size_t build_hvcc(const esc_track_t *t, unsigned char *o, size_t cap) {
  static const unsigned char types[3] = {32, 33, 34};
  const unsigned char *ps[3];
  size_t pl[3], n = 0;

  ps[0] = t->vps;
  pl[0] = t->vpslen;
  ps[1] = t->sps;
  pl[1] = t->spslen;
  ps[2] = t->pps;
  pl[2] = t->ppslen;
  if (23 + pl[0] + pl[1] + pl[2] + 15 > cap) return 0;
  o[n++] = 1;
  memcpy(o + n, t->ptl, 12);
  n += 12;
  o[n++] = 0xF0; /* min_spatial_segmentation_idc = 0 */
  o[n++] = 0x00;
  o[n++] = 0xFC; /* parallelismType 0 */
  o[n++] = (unsigned char)(0xFC | (t->chroma & 3));
  o[n++] = 0xF8; /* bitDepthLumaMinus8 0 */
  o[n++] = 0xF8; /* bitDepthChromaMinus8 0 */
  o[n++] = 0x00; /* avgFrameRate */
  o[n++] = 0x00;
  o[n++] = 0x03; /* 4-byte NALU length */
  o[n++] = 3;    /* numOfArrays */
  for (size_t i = 0; i < 3; i++) {
    o[n++] = types[i];
    o[n++] = 0;
    o[n++] = 1;
    o[n++] = (unsigned char)(pl[i] >> 8);
    o[n++] = (unsigned char)pl[i];
    memcpy(o + n, ps[i], pl[i]);
    n += pl[i];
  }
  return n;
}

int av1_seq_hdr_info(const unsigned char *obu, size_t len, av1_seq_hdr_t *info, unsigned *w, unsigned *h) {
  br_t b;
  unsigned ext, has_size, reduced, seq_profile;
  unsigned timing_present, dec_model_present = 0, init_delay_present, op_cnt, buf_delay_bits = 0;
  unsigned fwbits, fhbits, high_bitdepth, twelve_bit = 0, bitdepth12;
  unsigned choose_sct, force_sct, enable_order_hint;
  unsigned color_desc, cp, tc, mc, ssx, ssy, csp;
  unsigned level0 = 0, tier0 = 0;

  b.d = obu;
  b.len = len;
  b.bit = 0;
  b.err = 0;

  br_u(&b, 1);
  br_u(&b, 4);
  ext = br_u(&b, 1);
  has_size = br_u(&b, 1);
  br_u(&b, 1);
  if (ext) br_u(&b, 8);
  if (has_size) for (int i = 0; i < 8 && (br_u(&b, 8) & 0x80); i++) {}
  seq_profile = br_u(&b, 3);
  br_u(&b, 1);
  reduced = br_u(&b, 1);
  if (reduced) {
    level0 = br_u(&b, 5);
  } else {
    timing_present = br_u(&b, 1);
    if (timing_present) {
      br_u(&b, 32);
      br_u(&b, 32);
      if (br_u(&b, 1)) br_ue(&b);
      dec_model_present = br_u(&b, 1);
      if (dec_model_present) {
        buf_delay_bits = br_u(&b, 5) + 1;
        br_u(&b, 32);
        br_u(&b, 5);
        br_u(&b, 5);
      }
    }
    init_delay_present = br_u(&b, 1);
    op_cnt = br_u(&b, 5) + 1;
    for (unsigned i = 0; i < op_cnt && !b.err; i++) {
      unsigned level, tier;
      br_u(&b, 12);
      level = br_u(&b, 5);
      tier = (level > 7) ? br_u(&b, 1) : 0;
      if (i == 0) {
        level0 = level;
        tier0 = tier;
      }
      if (dec_model_present && br_u(&b, 1)) {
        br_u(&b, buf_delay_bits);
        br_u(&b, buf_delay_bits);
        br_u(&b, 1);
      }
      if (init_delay_present && br_u(&b, 1)) br_u(&b, 4);
    }
  }
  fwbits = br_u(&b, 4) + 1;
  fhbits = br_u(&b, 4) + 1;
  *w = br_u(&b, fwbits) + 1;
  *h = br_u(&b, fhbits) + 1;

  if (!reduced && br_u(&b, 1)) {
    br_u(&b, 4);
    br_u(&b, 3);
  }
  br_u(&b, 1);
  br_u(&b, 1);
  br_u(&b, 1);
  if (!reduced) {
    br_u(&b, 1);
    br_u(&b, 1);
    br_u(&b, 1);
    br_u(&b, 1);
    enable_order_hint = br_u(&b, 1);
    if (enable_order_hint) {
      br_u(&b, 1);
      br_u(&b, 1);
    }
    choose_sct = br_u(&b, 1);
    force_sct = choose_sct ? 2 : br_u(&b, 1);
    if (force_sct > 0 && !br_u(&b, 1)) br_u(&b, 1);
    if (enable_order_hint) br_u(&b, 3);
  }
  br_u(&b, 1);
  br_u(&b, 1);
  br_u(&b, 1);

  high_bitdepth = br_u(&b, 1);
  if (seq_profile == 2 && high_bitdepth) twelve_bit = br_u(&b, 1);
  bitdepth12 = twelve_bit;
  info->monochrome = (seq_profile == 1) ? 0 : br_u(&b, 1);
  color_desc = br_u(&b, 1);
  if (color_desc) {
    cp = br_u(&b, 8);
    tc = br_u(&b, 8);
    mc = br_u(&b, 8);
  } else {
    cp = tc = mc = 0xFF;
  }
  if (info->monochrome) {
    br_u(&b, 1);
    ssx = ssy = 1;
    csp = 0;
  } else if (cp == 1 && tc == 13 && mc == 0) { /* BT.709/sRGB/Identity: no subsampling */
    ssx = ssy = 0;
    csp = 0;
  } else {
    br_u(&b, 1);
    if (seq_profile == 0) {
      ssx = ssy = 1;
    } else if (seq_profile == 1) {
      ssx = ssy = 0;
    } else if (bitdepth12) {
      ssx = br_u(&b, 1);
      ssy = ssx ? br_u(&b, 1) : 0;
    } else {
      ssx = 1;
      ssy = 0;
    }
    csp = (ssx && ssy) ? br_u(&b, 2) : 0;
  }
  info->seq_profile = seq_profile;
  info->seq_level_idx0 = level0;
  info->seq_tier0 = tier0;
  info->high_bitdepth = high_bitdepth;
  info->twelve_bit = twelve_bit;
  info->subsampling_x = ssx;
  info->subsampling_y = ssy;
  info->chroma_sample_pos = csp;
  if (b.err || !*w || !*h) return -1;
  return 0;
}

size_t build_av1c(const av1_seq_hdr_t *info, const unsigned char *sps, size_t spslen, unsigned char *o, size_t cap) {
  if (4 + spslen > cap) return 0;
  o[0] = 0x80 | (unsigned char)1;
  o[1] = (unsigned char)((info->seq_profile << 5) | info->seq_level_idx0);
  o[2] = (unsigned char)((info->seq_tier0 << 7) | (info->high_bitdepth << 6) | (info->twelve_bit << 5)
                        | (info->monochrome << 4) | (info->subsampling_x << 3) | (info->subsampling_y << 2)
                        | info->chroma_sample_pos);
  o[3] = 0;
  memcpy(o + 4, sps, spslen);
  return 4 + spslen;
}
