/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

/* DTS core substream, ETSI TS 102 114 clause 5: tab 5-4f (AMODE+SFREQ) */
static const unsigned char dts_amode_ch[16] = {1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 6, 7, 8, 8};
static const unsigned dts_sfreq_rate[16] = {0, 8000, 16000, 32000, 0, 0, 11025, 22050, 44100, 0, 0, 12000, 24000, 48000, 0, 0};

/* TS 102 114 clause 7.5.2 tab 7-2, only nuExtSSFsize */
static int dts_ext_substream_size(const unsigned char *d, size_t len, size_t *out_size) {
  br_t b;
  unsigned hdr_type;
  unsigned fsize_bits;
  if (len < 6) return 0;
  if (d[0] != 0x64 || d[1] != 0x58 || d[2] != 0x20 || d[3] != 0x25) return 0;
  b.d = d + 4;
  b.len = len - 4;
  b.bit = 0;
  b.err = 0;
  br_u(&b, 8);
  br_u(&b, 2);
  hdr_type = br_u(&b, 1);
  br_u(&b, hdr_type ? 12 : 8);
  fsize_bits = hdr_type ? 20 : 16;
  *out_size = (size_t)br_u(&b, fsize_bits) + 1;
  return !b.err;
}

/* nuTotalNumChs of asset 0 (incl. LFE), TS 102 114 tables 7-2 and 7-5. 0: not carried, static fields absent */
static int dts_ext_total_channels(const unsigned char *d, size_t len, unsigned *total) {
  br_t b;
  unsigned ss_index;
  unsigned fsize_bits;
  unsigned n_presnt;
  unsigned n_assets;
  unsigned active[8];
  if (len < 6) return 0;
  if (d[0] != 0x64 || d[1] != 0x58 || d[2] != 0x20 || d[3] != 0x25) return 0;
  b.d = d + 4;
  b.len = len - 4;
  b.bit = 0;
  b.err = 0;
  br_u(&b, 8);
  ss_index = br_u(&b, 2);
  fsize_bits = br_u(&b, 1) ? 20 : 16;
  br_u(&b, fsize_bits == 20 ? 12 : 8);
  br_u(&b, fsize_bits);
  if (!br_u(&b, 1)) return 0;
  br_u(&b, 2);
  br_u(&b, 3);
  if (br_u(&b, 1)) {
    br_u(&b, 32);
    br_u(&b, 4);
  }
  n_presnt = br_u(&b, 3) + 1;
  n_assets = br_u(&b, 3) + 1;
  for (unsigned i = 0; i < n_presnt; i++) active[i] = br_u(&b, ss_index + 1);
  for (unsigned i = 0; i < n_presnt; i++)
    for (unsigned ss = 0; ss <= ss_index; ss++)
      if ((active[i] >> ss) & 1) br_u(&b, 8);
  if (br_u(&b, 1)) {
    unsigned out_bits;
    unsigned n_cfg;
    br_u(&b, 2);
    out_bits = (br_u(&b, 2) + 1) << 2;
    n_cfg = br_u(&b, 2) + 1;
    br_skip(&b, (size_t)n_cfg * out_bits);
  }
  br_skip(&b, (size_t)n_assets * fsize_bits);
  br_u(&b, 9);
  br_u(&b, 3);
  if (br_u(&b, 1)) br_u(&b, 4);
  if (br_u(&b, 1)) br_u(&b, 24);
  if (br_u(&b, 1)) {
    unsigned n_text = br_u(&b, 10) + 1;
    br_skip(&b, (size_t)n_text * 8);
  }
  br_u(&b, 5);
  br_u(&b, 4);
  *total = br_u(&b, 8) + 1;
  return !b.err;
}

/* ext: extension substream bytes, core_total: core channels incl. LFE or 0 */
static void dts_note_total(esc_track_t *t, esc_frame_t *f, const unsigned char *ext, size_t ext_len, unsigned core_total) {
  unsigned total;
  if (dts_ext_total_channels(ext, ext_len, &total)) {
    t->dts_total_ch = total;
    t->dts_total_ext = 1;
  } else if (core_total && !t->dts_total_ext) {
    t->dts_total_ch = core_total;
  }
  if (t->dts_total_ch) {
    f->chcfg_scheme = ESC_CHCFG_DTS_2012;
    f->chcfg_value = t->dts_total_ch;
  }
}

int next_dts(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  br_t b;
  unsigned nblks = 0;
  unsigned fsize = 0;
  unsigned amode = 0;
  unsigned sfreq = 0;
  unsigned lff = 0;
  size_t core_size = 0;
  size_t ext_size = 0;
  int have_core = 0;
  if (len < 11) return 1; /* core header through LFF */
  if (d[0] == 0x7F && d[1] == 0xFE && d[2] == 0x80 && d[3] == 0x01) {
    b.d = d + 4;
    b.len = len - 4;
    b.bit = 0;
    b.err = 0;
    br_u(&b, 1);
    br_u(&b, 5);
    br_u(&b, 1);
    nblks = br_u(&b, 7);
    fsize = br_u(&b, 14);
    amode = br_u(&b, 6);
    sfreq = br_u(&b, 4);
    br_u(&b, 15); /* RATE .. ASPF */
    lff = br_u(&b, 2);
    if (b.err || amode > 15 || !dts_sfreq_rate[sfreq]) return -1;
    core_size = (size_t)fsize + 1;
    if (core_size < 9) return -1;
    have_core = 1;
  }
  if (!have_core) {
    size_t sz;
    if (!dts_ext_substream_size(d, len, &sz)) return -1;
    if (len < sz) return 1;
    f->consumed = sz;
    f->out = d;
    f->outlen = sz;
    f->rate = t->rate;
    f->ch = t->channels;
    f->dts_has_core = 0;
    dts_note_total(t, f, d, len, 0);
    return 0;
  }
  if (len < core_size) return 1;
  if (dts_ext_substream_size(d + core_size, len - core_size, &ext_size)) {
    if (len < core_size + ext_size) return 1;
  } else ext_size = 0;
  f->consumed = core_size + ext_size;
  f->out = d;
  f->outlen = f->consumed;
  f->rate = dts_sfreq_rate[sfreq];
  f->ch = dts_amode_ch[amode];
  f->samples = (nblks + 1) * 32;
  f->dts_has_core = 1;
  t->rate = f->rate;
  t->channels = f->ch;
  dts_note_total(t, f, d + core_size, len - core_size, f->ch + ((lff == 1 || lff == 2) ? 1u : 0u));
  return 0;
}
