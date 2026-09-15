/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

/* MPEG audio bitrate kbps: [mpeg1?0:1][layer-1][index] */
static const unsigned short mpa_br[2][3][16] = {
  {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
  {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
  {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}
    },
 {
  {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
  {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
  {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}
    }
};
static const unsigned mpa_sample_rates[4][3] = {{11025, 12000, 8000},{0, 0, 0},{22050, 24000, 16000},{44100, 48000, 32000}};

int next_mpa(const esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned ver;
  unsigned lay;
  unsigned bri;
  unsigned sri;
  unsigned pad;
  unsigned br;
  unsigned sr;
  int mpeg1;
  int ly;
  (void)t;
  if (len < 4) return 1;
  if (d[0] != 0xFF || (d[1] & 0xE0) != 0xE0) return -1;
  ver = (d[1] >> 3) & 3;
  lay = (d[1] >> 1) & 3;
  bri = (d[2] >> 4) & 0x0F;
  sri = (d[2] >> 2) & 3;
  pad = (d[2] >> 1) & 1;
  if (ver == 1 || lay == 0 || bri == 0 || bri == 15 || sri == 3) return -1;
  mpeg1 = (ver == 3);
  ly = 4 - (int)lay;
  br = mpa_br[mpeg1 ? 0 : 1][ly - 1][bri] * 1000u;
  sr = mpa_sample_rates[ver][sri];
  if (!br || !sr) return -1;
  if (ly == 1) f->consumed = (12 * br / sr + pad) * 4;
  else if (ly == 2) f->consumed = 144 * br / sr + pad;
  else f->consumed = (mpeg1 ? 144u : 72u) * br / sr + pad;

  if (f->consumed < 4) return -1;
  if (len < f->consumed) return 1;
  if (ly == 1) f->samples = 384;
  else if (ly == 2) f->samples = 1152;
  else f->samples = mpeg1 ? 1152 : 576;
  f->rate = sr;
  f->ch = (((d[3] >> 6) & 3) == 3) ? 1 : 2;
  f->layer = ly;
  f->out = d;
  f->outlen = f->consumed;
  return 0;
}
