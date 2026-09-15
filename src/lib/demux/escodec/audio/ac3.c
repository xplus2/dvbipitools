/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

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

int next_ac3(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned fscod, frmsizecod, acmod;
  br_t b;
  (void)t;
  if (len < 7) return 1;
  if (d[0] != 0x0B || d[1] != 0x77) return -1;
  fscod = d[4] >> 6;
  frmsizecod = d[4] & 0x3F;
  if (fscod > 2 || frmsizecod > 37) return -1;
  f->consumed = (size_t)ac3_fsz[frmsizecod][fscod] * 2;
  if (len < f->consumed) return 1;
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
  if ((acmod & 1) && acmod != 1) br_u(&b, 2); /* cmixlev */
  if (acmod & 4) br_u(&b, 2); /* surmixlev */
  if (acmod == 2) br_u(&b, 2); /* dsurmod */
  f->lfeon = br_u(&b, 1);
  return 0;
}

int next_eac3(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  static const unsigned blk[4] = {1, 2, 3, 6};
  static const unsigned rate2[3] = {24000, 22050, 16000};
  unsigned frmsiz, fscod, numblkscod;

  (void)t;
  if (len < 6) return 1;
  if (d[0] != 0x0B || d[1] != 0x77) return -1;
  frmsiz = ((unsigned)(d[2] & 0x07) << 8) | d[3];
  f->consumed = ((size_t)frmsiz + 1) * 2;
  if (f->consumed < 6) return -1;
  if (len < f->consumed) return 1;
  fscod = d[4] >> 6;
  numblkscod = (d[4] >> 4) & 3;
  if (fscod == 3) {
    if (numblkscod > 2) return -1;
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
