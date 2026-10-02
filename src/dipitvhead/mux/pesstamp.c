/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "pesstamp.h"

#define TS_PAYLOAD_END 188
#define PES_FIXED_HEADER 9
#define PTS_MODULUS ((uint64_t)1 << 33)
#define ESCR_MODULUS (PTS_MODULUS * 300ULL)

typedef struct {
  unsigned char *pts;
  unsigned char *dts;
  unsigned char *escr;
  unsigned flags;
} fields_t;

static int stream_has_header(unsigned stream_id) {
  switch (stream_id) {
    case 0xBC:
    case 0xBE:
    case 0xBF:
    case 0xF0:
    case 0xF1:
    case 0xF2:
    case 0xF8:
    case 0xFF:
      return 0;
    default:
      return 1;
  }
}

static int locate(unsigned char *pkt188, fields_t *f) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  unsigned start = 4;
  unsigned char *pes;
  unsigned hdr_len;
  unsigned pts_dts;
  if (!(afc & 0x1) || !(pkt188[1] & 0x40)) return PESSTAMP_NONE;
  if (pkt188[3] >> 6) return PESSTAMP_SCRAMBLED;
  if (afc == 0x3) start += 1 + pkt188[4];
  if (start + 4 > TS_PAYLOAD_END) return PESSTAMP_NONE;
  pes = pkt188 + start;
  if (pes[0] || pes[1] || pes[2] != 1 || !stream_has_header(pes[3])) return PESSTAMP_NONE;
  if (start + PES_FIXED_HEADER > TS_PAYLOAD_END) return PESSTAMP_SPLIT;
  if ((pes[6] & 0xC0) != 0x80) return PESSTAMP_NONE;
  hdr_len = pes[8];
  f->flags = pes[7];
  pts_dts = (f->flags >> 6) & 0x3;
  if (pts_dts == 0x1) return PESSTAMP_NONE;
  if (!pts_dts && !(f->flags & 0x20)) return PESSTAMP_NONE;
  if (start + PES_FIXED_HEADER + hdr_len > TS_PAYLOAD_END) return PESSTAMP_SPLIT;
  f->pts = f->dts = f->escr = NULL;
  {
    unsigned char *p = pes + PES_FIXED_HEADER;
    unsigned used = 0;
    if (pts_dts >= 0x2) {
      f->pts = p;
      p += 5;
      used += 5;
    }
    if (pts_dts == 0x3) {
      f->dts = p;
      p += 5;
      used += 5;
    }
    if (f->flags & 0x20) {
      f->escr = p;
      used += 6;
    }
    if (used > hdr_len) return PESSTAMP_NONE;
  }
  return PESSTAMP_FOUND;
}

static uint64_t read_ts(const unsigned char *p) {
  return ((uint64_t)(p[0] & 0x0E) << 29) | ((uint64_t)p[1] << 22) | ((uint64_t)(p[2] & 0xFE) << 14) | ((uint64_t)p[3] << 7) | ((uint64_t)p[4] >> 1);
}

static void write_ts(unsigned char *p, uint64_t v) {
  p[0] = (unsigned char)((p[0] & 0xF1) | ((v >> 29) & 0x0E));
  p[1] = (unsigned char)(v >> 22);
  p[2] = (unsigned char)((p[2] & 0x01) | ((v >> 14) & 0xFE));
  p[3] = (unsigned char)(v >> 7);
  p[4] = (unsigned char)((p[4] & 0x01) | ((v << 1) & 0xFE));
}

static uint64_t read_escr(const unsigned char *p) {
  uint64_t base = ((uint64_t)(p[0] & 0x38) << 27) | ((uint64_t)(p[0] & 0x03) << 28) | ((uint64_t)p[1] << 20) | ((uint64_t)(p[2] & 0xF8) << 12) |
                  ((uint64_t)(p[2] & 0x03) << 13) | ((uint64_t)p[3] << 5) | ((uint64_t)p[4] >> 3);
  unsigned ext = ((unsigned)(p[4] & 0x03) << 7) | (p[5] >> 1);
  return base * 300 + ext;
}

static void write_escr(unsigned char *p, uint64_t v) {
  uint64_t base = v / 300;
  unsigned ext = (unsigned)(v % 300);
  p[0] = (unsigned char)((p[0] & 0xC4) | ((base >> 27) & 0x38) | ((base >> 28) & 0x03));
  p[1] = (unsigned char)(base >> 20);
  p[2] = (unsigned char)((p[2] & 0x04) | ((base >> 12) & 0xF8) | ((base >> 13) & 0x03));
  p[3] = (unsigned char)(base >> 5);
  p[4] = (unsigned char)((p[4] & 0x04) | ((base << 3) & 0xF8) | ((ext >> 7) & 0x03));
  p[5] = (unsigned char)((ext << 1) | (p[5] & 0x01));
}

int pesstamp_read(const unsigned char pkt188[188], pes_stamp_t *st) {
  fields_t f;
  int r;
  memset(st, 0, sizeof *st);
  r = locate((unsigned char *)pkt188, &f);
  if (r != PESSTAMP_FOUND) return r;
  if (f.pts) {
    st->has_pts = 1;
    st->pts = read_ts(f.pts);
  }
  if (f.dts) {
    st->has_dts = 1;
    st->dts = read_ts(f.dts);
  }
  if (f.escr) {
    st->has_escr = 1;
    st->escr27 = read_escr(f.escr);
  }
  return PESSTAMP_FOUND;
}

int pesstamp_shift(unsigned char pkt188[188], int64_t delta90k) {
  fields_t f;
  uint64_t d;
  int r = locate(pkt188, &f);
  if (r != PESSTAMP_FOUND) return r;
  d = (uint64_t)(((delta90k % (int64_t)PTS_MODULUS) + (int64_t)PTS_MODULUS) % (int64_t)PTS_MODULUS);
  if (f.pts) write_ts(f.pts, (read_ts(f.pts) + d) % PTS_MODULUS);
  if (f.dts) write_ts(f.dts, (read_ts(f.dts) + d) % PTS_MODULUS);
  if (f.escr) write_escr(f.escr, (read_escr(f.escr) + d * 300ULL) % ESCR_MODULUS);
  return PESSTAMP_FOUND;
}

int afstamp_shift(unsigned char pkt188[188], int64_t delta90k) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  unsigned afl;
  unsigned end;
  unsigned flags;
  unsigned off = 6;
  unsigned ext_end;
  unsigned eflags;
  unsigned p;
  uint64_t d;
  if (!(afc & 0x2)) return 0;
  afl = pkt188[4];
  if (afl < 1 || afl > 183) return 0;
  end = 5 + afl;
  flags = pkt188[5];
  if (!(flags & 0x01)) return 0;
  if (flags & 0x10) off += 6;
  if (flags & 0x08) off += 6;
  if (flags & 0x04) off += 1;
  if (flags & 0x02) {
    if (off >= end) return 0;
    off += 1 + pkt188[off];
  }
  if (off + 2 > end) return 0;
  ext_end = off + 1 + pkt188[off];
  if (ext_end > end) return 0;
  eflags = pkt188[off + 1];
  if (!(eflags & 0x20)) return 0;
  p = off + 2;
  if (eflags & 0x80) p += 2;
  if (eflags & 0x40) p += 3;
  if (p + 5 > ext_end) return 0;
  d = (uint64_t)(((delta90k % (int64_t)PTS_MODULUS) + (int64_t)PTS_MODULUS) % (int64_t)PTS_MODULUS);
  write_ts(pkt188 + p, (read_ts(pkt188 + p) + d) % PTS_MODULUS);
  return 1;
}

int afstamp_clear_discontinuity(unsigned char pkt188[188]) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  if (!(afc & 0x2) || pkt188[4] < 1 || !(pkt188[5] & 0x80)) return 0;
  pkt188[5] &= 0x7F;
  return 1;
}
