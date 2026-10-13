/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <string.h>

static const unsigned short opus_frame_samples[32] = {
  480, 960, 1920, 2880, 480, 960, 1920, 2880, 480, 960, 1920, 2880, 480, 960, 480, 960,
  120, 240, 480, 960, 120, 240, 480, 960, 120, 240, 480, 960, 120, 240, 480, 960
};

int next_opus(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned toc;
  unsigned config;
  unsigned code;
  unsigned frames;
  size_t hdr = 0;

  if (len < 1) return 1;
  if (len >= 2 && d[0] == 0x7F && (d[1] & 0xE0) == 0xE0) {
    size_t p = 2;
    size_t au = 0;
    for (;;) {
      if (p >= len) return 1;
      au += d[p];
      if (d[p++] != 0xFF) break;
    }
    if (d[1] & 0x10) p += 2;
    if (d[1] & 0x08) p += 2;
    if (d[1] & 0x04) {
      if (p >= len) return 1;
      p += 1u + d[p];
    }
    if (!au) return -1;
    if (p > len || len - p < au) return 1;
    hdr = p;
    len = p + au;
  }
  d += hdr;
  len -= hdr;
  toc = d[0];
  config = (toc >> 3) & 0x1F;
  code = toc & 0x03;
  if (code == 3) {
    if (len < 2) return 1;
    frames = d[1] & 0x3F;
    if (!frames) return -1;
  } else frames = (code == 0) ? 1 : 2;

  if (frames * opus_frame_samples[config] > 5760) return -1;
  f->consumed = hdr + len;
  f->out = d;
  f->outlen = len;
  f->rate = 48000;
  f->ch = ((toc >> 2) & 1) ? 2 : 1;
  f->samples = frames * opus_frame_samples[config];
  if (!t->cpriv_len) {
    static const unsigned char magic[8] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
    memcpy(t->cpriv, magic, 8);
    t->cpriv[8] = 1;
    t->cpriv[9] = (unsigned char)f->ch;
    t->cpriv[10] = 0;
    t->cpriv[11] = 0;
    t->cpriv[12] = 0x80;
    t->cpriv[13] = 0xBB;
    t->cpriv[14] = 0;
    t->cpriv[15] = 0;
    t->cpriv[16] = 0;
    t->cpriv[17] = 0;
    t->cpriv[18] = 0;
    t->cpriv_len = 19;
  }
  return 0;
}
