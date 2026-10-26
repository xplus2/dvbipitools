/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/mux/mp4/priv.h"

void put_be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

void put_be64(unsigned char *p, uint64_t v) {
  put_be32(p, (uint32_t)(v >> 32));
  put_be32(p + 4, (uint32_t)v);
}

void p4_write_ftyp_mdat_head(mp4_t *m) {
  mp4buf_t ftyp;
  mp4buf_t top;
  unsigned char mdat_hdr[16];

  memset(&ftyp, 0, sizeof ftyp);
  if (m->video_ok) {
    mb_fourcc(&ftyp, "isom");
    mb_u32(&ftyp, 0);
    mb_fourcc(&ftyp, "isom");
    mb_fourcc(&ftyp, "iso2");
    mb_fourcc(&ftyp, "avc1");
    mb_fourcc(&ftyp, "mp41");
  } else {
    mb_fourcc(&ftyp, "M4A ");
    mb_u32(&ftyp, 0);
    mb_fourcc(&ftyp, "M4A ");
    mb_fourcc(&ftyp, "mp42");
    mb_fourcc(&ftyp, "isom");
  }
  memset(&top, 0, sizeof top);
  mb_box(&top, "ftyp", &ftyp);
  p4_wfd(m, top.p, top.len);
  mp4buf_free(&top);
  m->moov_pos = *m->bytes;
  put_be32(mdat_hdr, MP4_MOOV_RESERVE);
  memcpy(mdat_hdr + 4, "free", 4);
  p4_wfd(m, mdat_hdr, 8);
  for (size_t left = MP4_MOOV_RESERVE - 8; left && !m->err;) {
    static const unsigned char zero[4096];
    size_t n = left < sizeof zero ? left : sizeof zero;
    p4_wfd(m, zero, n);
    left -= n;
  }
  m->mdat_hdr_pos = *m->bytes;
  put_be32(mdat_hdr, 8);
  memcpy(mdat_hdr + 4, "free", 4);
  put_be32(mdat_hdr + 8, 0); /* size 0: to EOF, stays valid while recording */
  memcpy(mdat_hdr + 12, "mdat", 4);
  p4_wfd(m, mdat_hdr, sizeof mdat_hdr);
}
