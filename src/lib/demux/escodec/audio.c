/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "escodec.h"

/* AC-3 frame size in words: [frmsizecod][fscod 48/44.1/32k] */
static const unsigned short ac3_fsz[38][3] = {
    {64, 69, 96},       {64, 70, 96},       {80, 87, 120},
    {80, 88, 120},      {96, 104, 144},     {96, 105, 144},
    {112, 121, 168},    {112, 122, 168},    {128, 139, 192},
    {128, 140, 192},    {160, 174, 240},    {160, 175, 240},
    {192, 208, 288},    {192, 209, 288},    {224, 243, 336},
    {224, 244, 336},    {256, 278, 384},    {256, 279, 384},
    {320, 348, 480},    {320, 349, 480},    {384, 417, 576},
    {384, 418, 576},    {448, 487, 672},    {448, 488, 672},
    {512, 557, 768},    {512, 558, 768},    {640, 696, 960},
    {640, 697, 960},    {768, 835, 1152},   {768, 836, 1152},
    {896, 975, 1344},   {896, 976, 1344},   {1024, 1114, 1536},
    {1024, 1115, 1536}, {1152, 1253, 1728}, {1152, 1254, 1728},
    {1280, 1393, 1920}, {1280, 1394, 1920}};
static const unsigned ac3_rate[3] = {48000, 44100, 32000};
static const unsigned ac3_ch[8] = {2, 1, 2, 3, 3, 4, 4, 5};

/* MPEG audio bitrate kbps: [mpeg1?0:1][layer-1][index] */
static const unsigned short mpa_br[2][3][16] = {
    {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
     {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
     {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}},
    {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
     {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
     {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}}};
static const unsigned mpa_sample_rates[4][3] = {{11025, 12000, 8000},{0, 0, 0},{22050, 24000, 16000},{44100, 48000, 32000}};
static const unsigned aac_sample_rates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000,  7350};

static int next_ac3(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned fscod, frmsizecod, acmod;
  br_t b;

  (void)t;
  if (len < 7)
    return 1;
  if (d[0] != 0x0B || d[1] != 0x77)
    return -1;
  fscod = d[4] >> 6;
  frmsizecod = d[4] & 0x3F;
  if (fscod > 2 || frmsizecod > 37)
    return -1;
  f->consumed = (size_t)ac3_fsz[frmsizecod][fscod] * 2;
  if (len < f->consumed)
    return 1;
  f->out = d;
  f->outlen = f->consumed;
  f->rate = ac3_rate[fscod];
  f->samples = 1536;
  f->bitrate_code = frmsizecod;

  b.d = d + 5;
  b.len = 2;
  b.bit = 0;
  b.err = 0;
  f->bsid = br_u(&b, 5);
  f->bsmod = br_u(&b, 3);
  acmod = br_u(&b, 3);
  f->acmod = acmod;
  f->ch = ac3_ch[acmod];
  if ((acmod & 1) && acmod != 1)
    br_u(&b, 2); /* cmixlev */
  if (acmod & 4)
    br_u(&b, 2); /* surmixlev */
  if (acmod == 2)
    br_u(&b, 2); /* dsurmod */
  f->lfeon = br_u(&b, 1);
  return 0;
}

static int next_eac3(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  static const unsigned blk[4] = {1, 2, 3, 6};
  static const unsigned rate2[3] = {24000, 22050, 16000};
  unsigned frmsiz, fscod, numblkscod;

  (void)t;
  if (len < 6)
    return 1;
  if (d[0] != 0x0B || d[1] != 0x77)
    return -1;
  frmsiz = ((unsigned)(d[2] & 0x07) << 8) | d[3];
  f->consumed = ((size_t)frmsiz + 1) * 2;
  if (f->consumed < 6)
    return -1;
  if (len < f->consumed)
    return 1;
  fscod = d[4] >> 6;
  numblkscod = (d[4] >> 4) & 3;
  if (fscod == 3) {
    if (numblkscod > 2)
      return -1;
    f->rate = rate2[numblkscod];
    f->samples = 6 * 256;
  } else {
    f->rate = ac3_rate[fscod];
    f->samples = blk[numblkscod] * 256;
  }
  f->acmod = (d[4] >> 1) & 7;
  f->ch = ac3_ch[f->acmod];
  f->lfeon = d[4] & 1;
  f->bsid = d[5] >> 3;
  f->bsmod = 0; /* no fixed E-AC-3 bit position, dec3 leaves 0 */
  f->out = d;
  f->outlen = f->consumed;
  return 0;
}

static int next_mpa(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned ver, lay, bri, sri, pad, br, sr;
  int mpeg1, ly;

  (void)t;
  if (len < 4)
    return 1;
  if (d[0] != 0xFF || (d[1] & 0xE0) != 0xE0)
    return -1;
  ver = (d[1] >> 3) & 3;
  lay = (d[1] >> 1) & 3;
  bri = (d[2] >> 4) & 0x0F;
  sri = (d[2] >> 2) & 3;
  pad = (d[2] >> 1) & 1;
  if (ver == 1 || lay == 0 || bri == 0 || bri == 15 || sri == 3)
    return -1;
  mpeg1 = (ver == 3);
  ly = 4 - (int)lay;
  br = mpa_br[mpeg1 ? 0 : 1][ly - 1][bri] * 1000u;
  sr = mpa_sample_rates[ver][sri];
  if (!br || !sr)
    return -1;
  if (ly == 1)
    f->consumed = (12 * br / sr + pad) * 4;
  else if (ly == 2)
    f->consumed = 144 * br / sr + pad;
  else
    f->consumed = (mpeg1 ? 144u : 72u) * br / sr + pad;
  if (f->consumed < 4)
    return -1;
  if (len < f->consumed)
    return 1;
  f->samples = (ly == 1) ? 384 : (ly == 2) ? 1152 : (mpeg1 ? 1152 : 576);
  f->rate = sr;
  f->ch = (((d[3] >> 6) & 3) == 3) ? 1 : 2;
  f->layer = ly;
  f->out = d;
  f->outlen = f->consumed;
  return 0;
}

static int next_aac(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned prof, sfi, chcfg, fl, hl;

  if (len < 7)
    return 1;
  if (d[0] != 0xFF || (d[1] & 0xF6) != 0xF0)
    return -1;
  prof = (d[2] >> 6) & 3;
  sfi = (d[2] >> 2) & 0x0F;
  chcfg = (unsigned)((d[2] & 1) << 2) | ((d[3] >> 6) & 3);
  fl = ((unsigned)(d[3] & 3) << 11) | ((unsigned)d[4] << 3) | (d[5] >> 5);
  hl = (d[1] & 1) ? 7 : 9;
  if (sfi > 12 || fl <= hl)
    return -1;
  if (len < fl)
    return 1;
  if (!t->cpriv_len) {
    unsigned aot = prof + 1;
    t->cpriv[0] = (unsigned char)((aot << 3) | (sfi >> 1));
    t->cpriv[1] = (unsigned char)(((sfi & 1) << 7) | (chcfg << 3));
    t->cpriv_len = 2;
  }
  f->consumed = fl;
  f->out = d + hl;
  f->outlen = fl - hl;
  f->rate = aac_sample_rates[sfi];
  f->ch = chcfg ? chcfg : 2;
  f->samples = 1024;
  return 0;
}

static void parse_sbr_ext(br_t *b) {
  unsigned ext = br_u(b, 5);
  if (ext == 5 && br_u(b, 1) && br_u(b, 4) == 15)
    br_u(b, 24);
}

static int latm_cfg(br_t *b, esc_track_t *t) {
  unsigned amv, sfi, ch, aot;
  size_t asc_start, asc_end;

  amv = br_u(b, 1);
  if (amv) {
    if (br_u(b, 1))
      return -1;
    {
      unsigned n = br_u(b, 2), i;
      for (i = 0; i <= n; i++)
        br_u(b, 8);
    }
  }
  if (!br_u(b, 1))
    return -1;
  if (br_u(b, 6) || br_u(b, 4) || br_u(b, 3))
    return -1;

  asc_start = b->bit;
  aot = br_u(b, 5);
  if (aot == 31)
    aot = 32 + br_u(b, 6);
  sfi = br_u(b, 4);
  if (sfi == 15)
    return -1;
  ch = br_u(b, 4);
  br_u(b, 1);
  if (br_u(b, 1))
    br_u(b, 14);
  if (br_u(b, 1))
    return -1;
  if ((aot == 5 || aot == 29) && br_u(b, 11) == 0x2B7)
    parse_sbr_ext(b);
  asc_end = b->bit;
  if (b->err || sfi > 12)
    return -1;
  t->cpriv_len = br_slice(b, asc_start, asc_end, t->cpriv, sizeof t->cpriv);
  if (!t->cpriv_len)
    return -1;
  t->rate = aac_sample_rates[sfi];
  t->channels = ch ? ch : 2;

  t->latm_flt = (int)br_u(b, 3);
  if (t->latm_flt == 0)
    br_u(b, 8);
  else
    return -1;
  if (br_u(b, 1)) {
    unsigned esc;
    do {
      esc = br_u(b, 1);
      br_u(b, 8);
    } while (esc && !b->err);
  }
  if (br_u(b, 1))
    br_u(b, 8);
  return b->err ? -1 : 0;
}

static int next_latm(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  br_t b;
  size_t total, plen = 0, i;
  unsigned v;

  if (len < 3)
    return 1;
  if (d[0] != 0x56 || (d[1] & 0xE0) != 0xE0)
    return -1;
  total = 3 + (size_t)((((unsigned)d[1] & 0x1F) << 8) | d[2]);
  if (len < total)
    return 1;
  b.d = d + 3;
  b.len = total - 3;
  b.bit = 0;
  b.err = 0;
  if (br_u(&b, 1) == 0) {
    if (latm_cfg(&b, t))
      return -1;
    t->latm_cfg_ok = 1;
  } else if (!t->latm_cfg_ok) {
    return -1;
  }
  do {
    v = br_u(&b, 8);
    plen += v;
  } while (v == 255 && !b.err);
  if (b.err || !plen || plen > sizeof t->au)
    return -1;
  if (b.bit % 8 == 0 && (b.bit >> 3) + plen <= b.len) {
    memcpy(t->au, b.d + (b.bit >> 3), plen);
    b.bit += plen * 8;
  } else {
    for (i = 0; i < plen; i++)
      t->au[i] = (unsigned char)br_u(&b, 8);
    if (b.err)
      return -1;
  }
  f->consumed = total;
  f->out = t->au;
  f->outlen = plen;
  f->rate = t->rate;
  f->ch = t->channels;
  f->samples = 1024;
  return 0;
}

static const unsigned short opus_frame_samples[32] = {
    480, 960, 1920, 2880, 480, 960, 1920, 2880, 480, 960, 1920, 2880, 480, 960, 480, 960,
    120, 240, 480, 960, 120, 240, 480, 960, 120, 240, 480, 960, 120, 240, 480, 960};

static int next_opus(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned toc, config, code, frames;

  (void)t;
  if (len < 1)
    return 1;
  if (len >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0)
    return -1; /* control header prefix au. not supported */
  toc = d[0];
  config = (toc >> 3) & 0x1F;
  code = toc & 0x03;
  if (code == 3) {
    if (len < 2) return 1;
    frames = d[1] & 0x3F;
    if (!frames) return -1;
  } else {
    frames = (code == 0) ? 1 : 2;
  }
  if (frames * opus_frame_samples[config] > 5760) return -1;
  f->consumed = len;
  f->out = d;
  f->outlen = len;
  f->rate = 48000;
  f->ch = ((toc >> 2) & 1) ? 2 : 1;
  f->samples = frames * opus_frame_samples[config];
  return 0;
}

int next_frame(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  memset(f, 0, sizeof *f);
  f->layer = 2;
  switch (t->codec) {
    case CODEC_AC3:         return next_ac3(t, d, len, f);
    case CODEC_EAC3:        return next_eac3(t, d, len, f);
    case CODEC_MP2A:        return next_mpa(t, d, len, f);
    case CODEC_AAC:         return next_aac(t, d, len, f);
    case CODEC_AAC_LATM:    return next_latm(t, d, len, f);
    case CODEC_OPUS:        return next_opus(t, d, len, f);
    default:                return -1;
  }
}
