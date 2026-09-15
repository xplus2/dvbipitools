/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

/* TrueHD stream2 channel_arrangement Bit channel count */
static const unsigned char thd_chancount[13] = {2, 1, 1, 2, 2, 2, 2, 1, 1, 2, 2, 1, 1};

static int truehd_ch(unsigned chanmap) {
  int ch = 0, i;
  for (i = 0; i < 13; i++) if ((chanmap >> i) & 1) ch += thd_chancount[i];
  return ch;
}

int next_truehd(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned au_size, ratebits;
  int has_major_sync;
  if (len < 8) return 1;
  au_size = (((unsigned)d[0] << 8 | d[1]) & 0x0FFF) * 2;
  if (au_size < 8) return -1;
  if (len < au_size) return 1;
  has_major_sync = d[4] == 0xF8 && d[5] == 0x72 && d[6] == 0x6F && (d[7] == 0xBA || d[7] == 0xBB);
  if (has_major_sync && d[7] == 0xBA && au_size >= 22) {
    br_t b;
    unsigned num_substreams, substream_info, chanmap2;
    b.d = d + 8;
    b.len = au_size - 8;
    b.bit = 0;
    b.err = 0;
    ratebits = br_u(&b, 4);
    br_u(&b, 4);
    br_u(&b, 2);
    br_u(&b, 2);
    br_u(&b, 5);
    br_u(&b, 2);
    chanmap2 = br_u(&b, 13);
    br_u(&b, 48);
    br_u(&b, 1);
    br_u(&b, 15);
    num_substreams = br_u(&b, 4);
    br_u(&b, 2);
    br_u(&b, 2);
    substream_info = br_u(&b, 8);
    if (!b.err && ratebits != 0x0F) {
      t->rate = (unsigned)((ratebits & 8) ? 44100 : 48000) << (ratebits & 7);
      t->channels = (unsigned)truehd_ch(chanmap2);
      t->truehd_samples = 40u << (ratebits & 7);
    }
    f->atmos = !b.err && num_substreams == 4 && (substream_info >> 7) == 1;
    if (au_size >= 20) {
      f->truehd_format_info = (unsigned)(d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11]);
      f->truehd_peak_data_rate = (unsigned)(d[18] << 8 | d[19]);
    }
  } else f->atmos = 0;

  if (!t->truehd_samples) return -1;
  f->consumed = au_size;
  f->out = d;
  f->outlen = au_size;
  f->rate = t->rate;
  f->ch = t->channels;
  f->samples = t->truehd_samples;
  return 0;
}
