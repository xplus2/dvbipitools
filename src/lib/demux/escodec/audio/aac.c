/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <string.h>

static const unsigned aac_sample_rates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000,  7350};

int next_aac(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned prof, sfi, chcfg, fl, hl;
  if (len < 7) return 1;
  if (d[0] != 0xFF || (d[1] & 0xF6) != 0xF0) return -1;
  prof = (d[2] >> 6) & 3;
  sfi = (d[2] >> 2) & 0x0F;
  chcfg = (unsigned)((d[2] & 1) << 2) | ((d[3] >> 6) & 3);
  fl = ((unsigned)(d[3] & 3) << 11) | ((unsigned)d[4] << 3) | (d[5] >> 5);
  hl = (d[1] & 1) ? 7 : 9;
  if (sfi > 12 || fl <= hl) return -1;
  if (len < fl) return 1;
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

static unsigned read_aot(br_t *b) {
  unsigned aot = br_u(b, 5);
  return aot == 31 ? 32 + br_u(b, 6) : aot;
}

static void skip_ext_sampling_freq(br_t *b) {
  if (br_u(b, 4) == 0x0F) br_u(b, 24);
}

static int latm_cfg(br_t *b, esc_track_t *t) {
  unsigned amv, sfi, ch, aot;
  size_t asc_start, asc_end;
  amv = br_u(b, 1);
  if (amv) {
    if (br_u(b, 1)) return -1;

    {
      unsigned n = br_u(b, 2), i;
      for (i = 0; i <= n; i++) br_u(b, 8);
    }
  }
  if (!br_u(b, 1)) return -1;
  if (br_u(b, 6) || br_u(b, 4) || br_u(b, 3)) return -1;
  asc_start = b->bit;
  aot = read_aot(b);
  sfi = br_u(b, 4);
  if (sfi == 15) return -1;
  ch = br_u(b, 4);
  if (aot == 5 || aot == 29) {
    skip_ext_sampling_freq(b);
    aot = read_aot(b);
  }
  br_u(b, 1);
  if (br_u(b, 1)) br_u(b, 14);
  if (br_u(b, 1)) return -1;
  asc_end = b->bit;
  if (b->err || sfi > 12) return -1;
  t->cpriv_len = br_slice(b, asc_start, asc_end, t->cpriv, sizeof t->cpriv);
  if (!t->cpriv_len) return -1;
  t->rate = aac_sample_rates[sfi];
  t->channels = ch ? ch : 2;
  t->latm_flt = (int)br_u(b, 3);
  if (t->latm_flt == 0) br_u(b, 8);
  else                  return -1;
  if (br_u(b, 1)) {
    unsigned esc;
    do {
      esc = br_u(b, 1);
      br_u(b, 8);
    } while (esc && !b->err);
  }
  if (br_u(b, 1)) br_u(b, 8);
  return b->err ? -1 : 0;
}

int next_latm(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  br_t b;
  size_t total, plen = 0, i;
  unsigned v;
  if (len < 3) return 1;
  if (d[0] != 0x56 || (d[1] & 0xE0) != 0xE0) return -1;
  total = 3 + (size_t)((((unsigned)d[1] & 0x1F) << 8) | d[2]);
  if (len < total) return 1;
  b.d = d + 3;
  b.len = total - 3;
  b.bit = 0;
  b.err = 0;
  if (br_u(&b, 1) == 0) {
    if (latm_cfg(&b, t)) return -1;
    t->latm_cfg_ok = 1;
  } else if (!t->latm_cfg_ok) return -1;
  do {
    v = br_u(&b, 8);
    plen += v;
  } while (v == 255 && !b.err);
  if (b.err || !plen || plen > sizeof t->au) return -1;
  if (b.bit % 8 == 0 && (b.bit >> 3) + plen <= b.len) {
    memcpy(t->au, b.d + (b.bit >> 3), plen);
    b.bit += plen * 8;
  } else {
    for (i = 0; i < plen; i++) t->au[i] = (unsigned char)br_u(&b, 8);
    if (b.err) return -1;
  }
  f->consumed = total;
  f->out = t->au;
  f->outlen = plen;
  f->rate = t->rate;
  f->ch = t->channels;
  f->samples = 1024;
  return 0;
}
