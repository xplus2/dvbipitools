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

static int ac4_channel_count(br_t *b) {
  static const unsigned char ch4[3] = {3, 5, 6};
  static const unsigned char ch7[6] = {7, 8, 7, 8, 7, 8};
  unsigned v2;
  unsigned v3;
  if (!br_u(b, 1)) return 1;
  if (!br_u(b, 1)) return 2;
  v2 = br_u(b, 2);
  if (v2 != 3) return ch4[v2];
  v3 = br_u(b, 3);
  if (v3 <= 5) return ch7[v3];
  if (v3 == 7) br_variable_bits(b, 2);
  return -1;
}

static void ac4_skip_emdf_info(br_t *b) {
  unsigned emdf_version = br_u(b, 2);
  unsigned key_id;
  if (emdf_version == 3) br_variable_bits(b, 2);
  key_id = br_u(b, 3);
  if (key_id == 7) br_variable_bits(b, 3);
  if (br_u(b, 1)) {
    unsigned substream_index = br_u(b, 2);
    if (substream_index == 3) br_variable_bits(b, 2);
  }
  {
    unsigned lp = br_u(b, 2);
    unsigned ls = br_u(b, 2);
    unsigned n_skip = 0;
    if (lp) n_skip += 1u << (2 * (lp - 1));
    if (ls) n_skip += 1u << (2 * (ls - 1));
    while (n_skip-- && !b->err) br_u(b, 8);
  }
}

static void ac4_skip_frame_rate_multiply_info(br_t *b, unsigned frame_rate_index) {
  switch (frame_rate_index) {
    case 2: case 3: case 4:
      if (br_u(b, 1)) br_u(b, 1);
      break;
    case 0: case 1: case 7: case 8: case 9:
      br_u(b, 1);
      break;
    default:
      break;
  }
}

static int ac4_parse_presentation0(br_t *b, unsigned frame_rate_index, esc_frame_t *f) {
  unsigned b_single_substream = br_u(b, 1);
  unsigned presentation_config = 0;
  unsigned pver = 0;
  int ch;

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
  ac4_skip_frame_rate_multiply_info(b, frame_rate_index);
  ac4_skip_emdf_info(b);
  if (b->err) return -1;
  if (!b_single_substream) {
    br_u(b, 1);
    if (presentation_config > 5) return -1;
  }
  ch = ac4_channel_count(b);
  if (b->err || ch < 0) return -1;
  f->ch = (unsigned)ch;
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

  {
    unsigned b_single_presentation = br_u(&b, 1);
    unsigned n_presentations;
    if (b_single_presentation) n_presentations = 1;
    else if (br_u(&b, 1)) n_presentations = br_variable_bits(&b, 2) + 2;
    else n_presentations = 0;
    if (br_u(&b, 1)) {
      unsigned payload_base = br_u(&b, 5) + 1;
      if (payload_base == 0x20) br_variable_bits(&b, 3);
    }
    if (!b.err && n_presentations) ac4_parse_presentation0(&b, frame_rate_index, f);
  }
  return 0;
}
