/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "pes.h"

static void put_pts(unsigned char *p, uint64_t pts) {
  p[0] = (unsigned char)(0x20 | ((pts >> 29) & 0x0E) | 0x01);
  p[1] = (unsigned char)(pts >> 22);
  p[2] = (unsigned char)(((pts >> 14) & 0xFE) | 0x01);
  p[3] = (unsigned char)(pts >> 7);
  p[4] = (unsigned char)(((pts << 1) & 0xFE) | 0x01);
}

size_t pes_build(uint64_t pts_90k, const unsigned char *frame, size_t frame_len, unsigned char *out, size_t cap) {
  size_t hdr_len = 14;
  unsigned pkt_len;
  if (frame_len > 65527 || cap < hdr_len + frame_len)
    return 0;

  out[0] = 0x00;
  out[1] = 0x00;
  out[2] = 0x01;
  out[3] = 0xC0; /* audio stream 0 */
  pkt_len = (unsigned)(8 + frame_len);
  out[4] = (unsigned char)(pkt_len >> 8);
  out[5] = (unsigned char)pkt_len;
  out[6] = 0x85; /* '10', no scrambling/priority, data_alignment=1, original_or_copy=1 */
  out[7] = 0x80; /* PTS_DTS_flags=10 (PTS only), no ESCR/ES_rate/trick/copy/CRC/extension */
  out[8] = 5;    /* PES_header_data_length: PTS field only */
  put_pts(out + 9, pts_90k);
  memcpy(out + hdr_len, frame, frame_len);
  return hdr_len + frame_len;
}
