/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

/* TrueHD stream2 channel_arrangement Bit channel count */
static const unsigned char thd_chancount[13] = {2, 1, 1, 2, 2, 2, 2, 1, 1, 2, 2, 1, 1};

static int truehd_ch(unsigned chanmap) {
  int ch = 0;
  for (int i = 0; i < 13; i++) {
    if ((chanmap >> i) & 1) ch += thd_chancount[i];
  }
  return ch;
}

/* arrangement bit to AC-4 speaker group bit (TS 103 190-2 table A.27). Lsd/Rsd has no group */
static const unsigned thd_group[13] = {0x00001, 0x00002, 0x00040, 0x00004, 0x00010, 0x00008, 0x20000, 0x08000, 0x00800, 0, 0x20000, 0x00200, 0x01000};

/* speaker group mask to ISO/IEC 23091-3 ChannelConfiguration index, as mapped in TS 103 190-2 table G.1 */
static const struct {
  unsigned mask;
  unsigned cicp;
} thd_cicp_map[] = {
  {0x000002, 1}, {0x000001, 2}, {0x000003, 3}, {0x008003, 4}, {0x000007, 5}, {0x000047, 6}, {0x020047, 7}, {0x008001, 9},
  {0x000005, 10}, {0x008047, 11}, {0x00004F, 12}, {0x02FF7F, 13}, {0x000057, 14}, {0x00145F, 15}, {0x000077, 16},
  {0x000A77, 17}, {0x000A7F, 18}, {0x00007F, 19}, {0x01007F, 20},
};

static unsigned truehd_cicp(unsigned chanmap) {
  unsigned mask = 0;
  for (int i = 0; i < 13; i++) {
    if (!((chanmap >> i) & 1)) continue;
    if (!thd_group[i]) return 0;
    mask |= thd_group[i];
  }
  for (size_t i = 0; i < sizeof thd_cicp_map / sizeof thd_cicp_map[0]; i++)
    if (thd_cicp_map[i].mask == mask) return thd_cicp_map[i].cicp;
  return 0;
}

int next_truehd(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  unsigned au_size;
  unsigned ratebits;
  int has_major_sync;
  if (len < 8) return 1;
  au_size = (((unsigned)d[0] << 8 | d[1]) & 0x0FFF) * 2;
  if (au_size < 8) return -1;
  if (len < au_size) return 1;
  has_major_sync = d[4] == 0xF8 && d[5] == 0x72 && d[6] == 0x6F && (d[7] == 0xBA || d[7] == 0xBB);
  if (has_major_sync && d[7] == 0xBA && au_size >= 22) {
    br_t b;
    unsigned num_substreams;
    unsigned substream_info;
    unsigned chanmap2;
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
      t->truehd_cicp = truehd_cicp(chanmap2);
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
  if (t->truehd_cicp) {
    f->chcfg_scheme = ESC_CHCFG_CICP;
    f->chcfg_value = t->truehd_cicp;
  }
  f->samples = t->truehd_samples;
  return 0;
}
