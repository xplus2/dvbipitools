/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <string.h>

static const unsigned short ac4_frame_len48[14] = {1920, 1920, 2048, 1536, 1536, 960, 960, 1024, 768, 768, 512, 384, 384, 2048};

static unsigned br_variable_bits(br_t *b, int n_bits) {
  unsigned value = 0;
  unsigned more;
  do {
    value += br_u(b, n_bits);
    more = br_u(b, 1);
    if (more) {
      value <<= n_bits;
      value += (1u << n_bits);
    }
  } while (more && !b->err);
  return value;
}

/* channel_mode as ch_mode, -1 reserved. v1: table 56 of TS 103 190-2, else table 88 of TS 103 190-1 */
static int ac4_channel_mode(br_t *b, int v1) {
  unsigned v;
  if (!br_u(b, 1)) return 0;
  if (!br_u(b, 1)) return 1;
  v = br_u(b, 2);
  if (v != 3) return (int)v + 2;
  v = br_u(b, 3);
  if (v <= 5) return (int)v + 5;
  if (!v1) {
    if (v == 7) br_variable_bits(b, 2);
    return -1;
  }
  if (v == 6) return br_u(b, 1) ? 12 : 11;
  v = br_u(b, 2);
  if (v <= 2) return (int)v + 13;
  br_variable_bits(b, 2);
  return -1;
}

/* channels per ch_mode, LFE included */
static const unsigned char ac4_mode_ch[16] = {1, 2, 3, 5, 6, 7, 8, 7, 8, 7, 8, 11, 12, 13, 14, 24};

/* TS 103 190-2 table A.27 speaker group bits */
#define AC4_G_LR 0x000001u
#define AC4_G_C 0x000002u
#define AC4_G_LSRS 0x000004u
#define AC4_G_LBRB 0x000008u
#define AC4_G_TFLTFR 0x000010u
#define AC4_G_TBLTBR 0x000020u
#define AC4_G_LFE 0x000040u
#define AC4_G_TSLTSR 0x000080u
#define AC4_G_TFC 0x000200u
#define AC4_G_TBC 0x000400u
#define AC4_G_TC 0x000800u
#define AC4_G_LFE2 0x001000u
#define AC4_G_BFLBFR 0x002000u
#define AC4_G_BFC 0x004000u
#define AC4_G_CB 0x008000u
#define AC4_G_LSCRRSCR 0x010000u
#define AC4_G_LWRW 0x020000u

/* flags: b_4_back_channels_present, b_centre_present, top_channels_present, only read for ch_mode 11..14 */
typedef struct {
  int mode;
  unsigned back;
  unsigned centre;
  unsigned top;
} ac4_chan_t;

/* speaker groups of one channel substream, TS 103 190-2 clause E.10.3 with table A.27 for ch_mode >= 11 */
static unsigned ac4_groups(const ac4_chan_t *c) {
  unsigned g;
  if (c->mode == 0) return AC4_G_C;
  if (c->mode == 1) return AC4_G_LR;
  if (c->mode <= 10) {
    g = AC4_G_LR | AC4_G_C;
    if (c->mode >= 3) g |= AC4_G_LSRS;
    if (c->mode == 5 || c->mode == 6) g |= AC4_G_LBRB;
    if (c->mode == 7 || c->mode == 8) g |= AC4_G_LWRW;
    if (c->mode == 9 || c->mode == 10) g |= AC4_G_TFLTFR;
    if (c->mode >= 4 && !(c->mode & 1)) g |= AC4_G_LFE;
    return g;
  }
  if (c->mode == 15)
    return AC4_G_LR | AC4_G_C | AC4_G_LSRS | AC4_G_LBRB | AC4_G_TFLTFR | AC4_G_TBLTBR | AC4_G_LFE | AC4_G_TSLTSR | AC4_G_TFC | AC4_G_TBC | AC4_G_TC |
           AC4_G_LFE2 | AC4_G_BFLBFR | AC4_G_BFC | AC4_G_CB | AC4_G_LWRW;
  g = AC4_G_LR | AC4_G_LSRS;
  if (c->centre) g |= AC4_G_C;
  if (c->back) g |= AC4_G_LBRB;
  if (c->top == 1 || c->top == 2) g |= AC4_G_TSLTSR;
  if (c->top == 3) g |= AC4_G_TFLTFR | AC4_G_TBLTBR;
  if (c->mode >= 13) g |= AC4_G_LSCRRSCR;
  if (!(c->mode & 1)) g |= AC4_G_LFE;
  return g;
}

/* channel count of a speaker group mask */
static unsigned ac4_group_channels(unsigned g) {
  static const struct {
    unsigned bit;
    unsigned n;
  } w[] = {{AC4_G_LR, 2}, {AC4_G_C, 1}, {AC4_G_LSRS, 2}, {AC4_G_LBRB, 2}, {AC4_G_TFLTFR, 2}, {AC4_G_TBLTBR, 2}, {AC4_G_LFE, 1}, {AC4_G_TSLTSR, 2},
           {AC4_G_TFC, 1}, {AC4_G_TBC, 1}, {AC4_G_TC, 1}, {AC4_G_LFE2, 1}, {AC4_G_BFLBFR, 2}, {AC4_G_BFC, 1}, {AC4_G_CB, 1}, {AC4_G_LSCRRSCR, 2}, {AC4_G_LWRW, 2}};
  unsigned n = 0;
  for (size_t i = 0; i < sizeof w / sizeof w[0]; i++)
    if (g & w[i].bit) n += w[i].n;
  return n;
}

/* TS 103 190-1 table F.2, bitstream_version 0: tag:dolby.com,2014 location masks by ch_mode. 3.0 from table F.1 */
static const unsigned short ac4_dolby2011[11] = {0x4000, 0xA000, 0xE000, 0xF800, 0xF802, 0xFA00, 0xFA02, 0xF820, 0xF822, 0xF810, 0xF812};

static void ac4_skip_emdf_info(br_t *b) {
  unsigned emdf_version = br_u(b, 2);
  unsigned key_id;
  unsigned lp;
  unsigned ls;
  unsigned n_skip;
  if (emdf_version == 3) br_variable_bits(b, 2);
  key_id = br_u(b, 3);
  if (key_id == 7) br_variable_bits(b, 3);
  if (br_u(b, 1)) {
    unsigned substream_index = br_u(b, 2);
    if (substream_index == 3) br_variable_bits(b, 2);
  }
  lp = br_u(b, 2);
  ls = br_u(b, 2);
  n_skip = 0;
  if (lp) n_skip += 1u << (2 * (lp - 1));
  if (ls) n_skip += 1u << (2 * (ls - 1));
  while (n_skip-- && !b->err) br_u(b, 8);
}

static unsigned ac4_frame_rate_multiply(br_t *b, unsigned frame_rate_index) {
  unsigned factor = 1;
  switch (frame_rate_index) {
    case 2: case 3: case 4:
      if (br_u(b, 1)) factor = br_u(b, 1) ? 4 : 2;
      break;
    case 0: case 1: case 7: case 8: case 9:
      if (br_u(b, 1)) factor = 2;
      break;
    default:
      break;
  }
  return factor;
}

static void ac4_skip_frame_rate_fractions(br_t *b, unsigned frame_rate_index, unsigned factor) {
  if (frame_rate_index >= 5 && frame_rate_index <= 9) {
    if (factor == 1) br_u(b, 1);
    return;
  }
  if (frame_rate_index < 10 || frame_rate_index > 12) return;
  if (br_u(b, 1)) br_u(b, 1);
}

static int ac4_parse_presentation0(br_t *b, unsigned bitstream_version, unsigned frame_rate_index, esc_frame_t *f) {
  unsigned b_single_substream = br_u(b, 1);
  unsigned presentation_config = 0;
  unsigned pver = 0;
  int mode;

  if (!b_single_substream) {
    presentation_config = br_u(b, 3);
    if (presentation_config == 7) presentation_config += br_variable_bits(b, 2);
  }
  while (br_u(b, 1) == 1 && !b->err) pver++;
  if (b->err) return -1;
  f->ac4_presentation_version = pver;
  if (!b_single_substream && presentation_config == 6) return -1;
  f->ac4_mdcompat = br_u(b, 3);
  if (br_u(b, 1)) br_variable_bits(b, 2);
  ac4_frame_rate_multiply(b, frame_rate_index);
  ac4_skip_emdf_info(b);
  if (b->err) return -1;
  if (!b_single_substream) {
    br_u(b, 1);
    if (presentation_config > 5) return -1;
  }
  mode = ac4_channel_mode(b, 0);
  if (b->err || mode < 0) return -1;
  f->ch = ac4_mode_ch[mode];
  if (pver == 0) {
    ac4_chan_t c = {mode, 0, 0, 0};
    if (bitstream_version == 0) {
      f->chcfg_scheme = ESC_CHCFG_DOLBY_2011;
      f->chcfg_value = ac4_dolby2011[mode];
    } else {
      f->chcfg_scheme = ESC_CHCFG_DOLBY_2015;
      f->chcfg_value = ac4_groups(&c);
    }
  }
  return 0;
}

/* group_index of ac4_sgi_specifier() */
static unsigned ac4_sgi_index(br_t *b) {
  unsigned idx = br_u(b, 3);
  if (idx == 7) idx += br_variable_bits(b, 2);
  return idx;
}

#define AC4_PRES_GROUPS_MAX 16

/* ac4_presentation_v1_info() of 1 presentation, bitstream_version 2. 0 ok, -1 unsupported or malformed.
   groups: first presentation's substream group indices */
static int ac4_parse_presentation_v1(br_t *b, unsigned frame_rate_index, int first, unsigned *factor, unsigned *groups, unsigned *ngroups, unsigned *max_group) {
  unsigned single = br_u(b, 1);
  unsigned config = 0;
  unsigned n_groups = 0;
  unsigned n_pres_groups = 0;
  unsigned add_emdf = 0;
  unsigned fr_factor;
  unsigned idx;

  if (!single) {
    config = br_u(b, 3);
    if (config == 7) config += br_variable_bits(b, 2);
  }
  while (br_u(b, 1) == 1) {
    if (b->err) break;
  }
  if (!single && config == 6) {
    if (first) return -1;
    add_emdf = 1;
  } else {
    br_u(b, 3);
    if (br_u(b, 1)) br_variable_bits(b, 2);
    fr_factor = ac4_frame_rate_multiply(b, frame_rate_index);
    if (first) *factor = fr_factor;
    ac4_skip_frame_rate_fractions(b, frame_rate_index, fr_factor);
    ac4_skip_emdf_info(b);
    if (br_u(b, 1)) br_u(b, 1);
    if (single) {
      n_groups = 1;
      n_pres_groups = 1;
    } else {
      br_u(b, 1);
      switch (config) {
        case 0: case 2:
          n_groups = 2;
          n_pres_groups = 2;
          break;
        case 1:
          n_groups = 2;
          n_pres_groups = 1;
          break;
        case 3:
          n_groups = 3;
          n_pres_groups = 3;
          break;
        case 4:
          n_groups = 3;
          n_pres_groups = 2;
          break;
        case 5:
          n_groups = br_u(b, 2) + 2;
          if (n_groups == 5) n_groups += br_variable_bits(b, 2);
          n_pres_groups = n_groups;
          break;
        default: {
          unsigned n_skip = br_u(b, 5);
          if (br_u(b, 1)) n_skip += br_variable_bits(b, 2) << 5;
          if (first) return -1;
          while (n_skip-- && !b->err) br_u(b, 8);
          break;
        }
      }
    }
    for (unsigned i = 0; i < n_groups && !b->err; i++) {
      idx = ac4_sgi_index(b);
      if (idx > *max_group) *max_group = idx;
      if (first && i < n_pres_groups) {
        if (*ngroups >= AC4_PRES_GROUPS_MAX) return -1;
        groups[(*ngroups)++] = idx;
      }
    }
    br_u(b, 1);
    add_emdf = br_u(b, 1);
    br_u(b, 1);
    br_u(b, 1);
    if (br_u(b, 2) == 3) br_variable_bits(b, 2);
  }
  if (add_emdf) {
    unsigned n = br_u(b, 2);
    if (!n) n = br_variable_bits(b, 2) + 4;
    while (n-- && !b->err) ac4_skip_emdf_info(b);
  }
  return b->err ? -1 : 0;
}

/* ac4_substream_info_chan() of bitstream_version 2. 0 ok, -1 reserved channel_mode */
static int ac4_parse_info_chan(br_t *b, unsigned fs_index, unsigned factor, unsigned substreams_present, ac4_chan_t *c) {
  c->mode = ac4_channel_mode(b, 1);
  c->back = 0;
  c->centre = 0;
  c->top = 0;
  if (b->err || c->mode < 0) return -1;
  if (c->mode >= 11 && c->mode <= 14) {
    c->back = br_u(b, 1);
    c->centre = br_u(b, 1);
    c->top = br_u(b, 2);
  }
  if (fs_index == 1 && br_u(b, 1)) br_u(b, 1);
  if (br_u(b, 1) && (br_u(b, 3) & 1)) br_u(b, 2);
  if (c->mode >= 7 && c->mode <= 10) br_u(b, 1);
  for (unsigned i = 0; i < factor; i++) br_u(b, 1);
  if (substreams_present && br_u(b, 2) == 3) br_variable_bits(b, 2);
  return b->err ? -1 : 0;
}

/* ac4_substream_group_info() of bitstream_version 2: speaker groups accumulate in *mask.
   1 channel coded, 0 object based (nothing parsed past it), -1 malformed */
static int ac4_parse_group_chan(br_t *b, unsigned fs_index, unsigned factor, unsigned *mask) {
  unsigned substreams_present = br_u(b, 1);
  unsigned hsf_ext = br_u(b, 1);
  unsigned n_lf = 1;
  if (!br_u(b, 1)) {
    n_lf = br_u(b, 2) + 2;
    if (n_lf == 5) n_lf += br_variable_bits(b, 2);
  }
  if (!br_u(b, 1)) return b->err ? -1 : 0;
  for (unsigned i = 0; i < n_lf; i++) {
    ac4_chan_t c;
    if (ac4_parse_info_chan(b, fs_index, factor, substreams_present, &c) < 0) return -1;
    *mask |= ac4_groups(&c);
    if (hsf_ext && substreams_present && br_u(b, 2) == 3) br_variable_bits(b, 2);
  }
  if (br_u(b, 1)) {
    br_u(b, 3);
    if (br_u(b, 1)) {
      if (br_u(b, 1)) {
        br_u(b, 1);
        br_u(b, 16);
      } else {
        unsigned n = br_u(b, 6);
        while (n-- && !b->err) br_u(b, 8);
      }
    }
  }
  return b->err ? -1 : 1;
}

static int ac4_parse_v1_channels(br_t *b, unsigned fs_index, unsigned frame_rate_index, unsigned n_presentations, esc_frame_t *f) {
  unsigned groups[AC4_PRES_GROUPS_MAX];
  unsigned ngroups = 0;
  unsigned max_group = 0;
  unsigned factor = 1;
  unsigned resolved = 0;
  unsigned mask = 0;

  if (!n_presentations) return -1;
  if (br_u(b, 1)) {
    br_u(b, 16);
    if (br_u(b, 1)) for (int i = 0; i < 16; i++) br_u(b, 8);
  }
  for (unsigned p = 0; p < n_presentations; p++)
    if (ac4_parse_presentation_v1(b, frame_rate_index, p == 0, &factor, groups, &ngroups, &max_group) < 0) return -1;
  for (unsigned g = 0; g <= max_group && resolved < ngroups; g++) {
    unsigned gmask = 0;
    int r = ac4_parse_group_chan(b, fs_index, factor, &gmask);
    int referenced = 0;
    if (r < 0) return -1;
    for (unsigned i = 0; i < ngroups; i++)
      if (groups[i] == g) referenced = 1;
    if (r == 0) {
      if (!referenced) return -1;
      f->chcfg_scheme = ESC_CHCFG_DOLBY_2015;
      f->chcfg_value = 0x800000u;
      return 0;
    }
    if (!referenced) continue;
    mask |= gmask;
    for (unsigned i = 0; i < ngroups; i++)
      if (groups[i] == g) resolved++;
  }
  if (resolved < ngroups) return -1;
  f->chcfg_scheme = ESC_CHCFG_DOLBY_2015;
  f->chcfg_value = mask;
  f->ch = ac4_group_channels(mask);
  return 0;
}

typedef struct {
  unsigned char buf[16];
  size_t bytelen;
  unsigned long long acc;
  int accbits;
} ac4_bitw_t;

static void ac4_bw_put(ac4_bitw_t *w, unsigned val, int n) {
  w->acc = (w->acc << n) | (val & (n < 32 ? (1u << n) - 1 : 0xFFFFFFFFu));
  w->accbits += n;
  while (w->accbits >= 8) {
    w->accbits -= 8;
    w->buf[w->bytelen++] = (unsigned char)(w->acc >> w->accbits);
  }
}

static void ac4_build_dsi(esc_track_t *t, unsigned bitstream_version, unsigned fs_index, unsigned frame_rate_index) {
  ac4_bitw_t w;
  w.bytelen = 0;
  w.acc = 0;
  w.accbits = 0;
  ac4_bw_put(&w, 1, 3);
  ac4_bw_put(&w, bitstream_version, 7);
  ac4_bw_put(&w, fs_index, 1);
  ac4_bw_put(&w, frame_rate_index, 4);
  ac4_bw_put(&w, 0, 9);
  ac4_bw_put(&w, 0, 2);
  ac4_bw_put(&w, 0, 32);
  ac4_bw_put(&w, 0xFFFFFFFFu, 32);
  if (w.accbits) ac4_bw_put(&w, 0, 8 - w.accbits);
  memcpy(t->cpriv, w.buf, w.bytelen);
  t->cpriv_len = w.bytelen;
}

int next_ac4(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  size_t hdrlen;
  size_t frame_size;
  size_t total;
  br_t b;
  unsigned bitstream_version;
  unsigned fs_index;
  unsigned frame_rate_index;
  int crc;
  unsigned b_single_presentation;
  unsigned n_presentations;

  if (len < 4) return 1;
  if (d[0] != 0xAC || (d[1] != 0x40 && d[1] != 0x41)) return -1;
  crc = d[1] == 0x41;
  frame_size = ((size_t)d[2] << 8) | d[3];
  hdrlen = 4;
  if (frame_size == 0xFFFF) {
    if (len < 7) return 1;
    frame_size = ((size_t)d[4] << 16) | ((size_t)d[5] << 8) | d[6];
    hdrlen = 7;
  }
  if (!frame_size) return -1;
  total = hdrlen + frame_size + (crc ? 2u : 0u);
  if (len < total) return 1;

  b.d = d + hdrlen;
  b.len = frame_size;
  b.bit = 0;
  b.err = 0;

  bitstream_version = br_u(&b, 2);
  if (bitstream_version == 3) bitstream_version += br_variable_bits(&b, 2);
  br_u(&b, 10);
  if (br_u(&b, 1)) {
    unsigned wait_frames = br_u(&b, 3);
    if (wait_frames > 0) br_u(&b, 2);
  }
  fs_index = br_u(&b, 1);
  frame_rate_index = br_u(&b, 4);
  if (b.err || frame_rate_index >= 14) return -1;
  if (fs_index == 0 && frame_rate_index != 13) return -1;

  f->consumed = total;
  f->out = d + hdrlen;
  f->outlen = frame_size;
  f->rate = fs_index ? 48000u : 44100u;
  f->samples = ac4_frame_len48[frame_rate_index];
  f->ac4_bitstream_version = bitstream_version;
  f->ac4_iframe = (int)br_u(&b, 1);
  if (!t->cpriv_len) ac4_build_dsi(t, bitstream_version, fs_index, frame_rate_index);
  b_single_presentation = br_u(&b, 1);
  if (b_single_presentation) n_presentations = 1;
  else if (br_u(&b, 1)) n_presentations = br_variable_bits(&b, 2) + 2;
  else n_presentations = 0;
  if (br_u(&b, 1)) {
    unsigned payload_base = br_u(&b, 5) + 1;
    if (payload_base == 0x20) br_variable_bits(&b, 3);
  }
  if (!b.err && n_presentations) {
    if (bitstream_version <= 1) ac4_parse_presentation0(&b, bitstream_version, frame_rate_index, f);
    else ac4_parse_v1_channels(&b, fs_index, frame_rate_index, n_presentations, f);
  }
  return 0;
}
