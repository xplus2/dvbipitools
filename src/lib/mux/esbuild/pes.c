/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "pes.h"

#include <string.h>

static void put_ts_field(unsigned char *p, uint64_t ts, unsigned prefix4) {
  p[0] = (unsigned char)((prefix4 << 4) | ((ts >> 29) & 0x0E) | 0x01);
  p[1] = (unsigned char)(ts >> 22);
  p[2] = (unsigned char)(((ts >> 14) & 0xFE) | 0x01);
  p[3] = (unsigned char)(ts >> 7);
  p[4] = (unsigned char)(((ts << 1) & 0xFE) | 0x01);
}

static int is_video_stream_id(unsigned char stream_id) { return (stream_id & 0xF0) == 0xE0; }

size_t esbuild_pes_build(unsigned char stream_id, uint64_t pts_90k, int has_dts, uint64_t dts_90k, const unsigned char *es, size_t es_len, unsigned char *out, size_t cap) {
  size_t opt_field_len = has_dts ? 10 : 5;
  size_t hdr_len = 9 + opt_field_len;
  size_t after_len = 3 + opt_field_len + es_len;
  unsigned pkt_len_val;

  if (cap < hdr_len + es_len) return 0;
  if (after_len > 0xFFFF) {
    /* video only escape, ISO/IEC 13818-1 2.4.3.7: PES_packet_length=0 unbounded,
       terminated by next PUSI. invalid for other stream types */
    if (!is_video_stream_id(stream_id)) return 0;
    pkt_len_val = 0;
  } else pkt_len_val = (unsigned)after_len;
  out[0] = 0x00;
  out[1] = 0x00;
  out[2] = 0x01;
  out[3] = stream_id;
  out[4] = (unsigned char)(pkt_len_val >> 8);
  out[5] = (unsigned char)pkt_len_val;
  out[6] = 0x85;
  out[7] = (unsigned char)(has_dts ? 0xC0 : 0x80);
  out[8] = (unsigned char)opt_field_len;
  if (has_dts) {
    put_ts_field(out + 9, pts_90k, 0x3);
    put_ts_field(out + 14, dts_90k, 0x1);
  } else {
    put_ts_field(out + 9, pts_90k, 0x2);
  }
  memcpy(out + hdr_len, es, es_len);
  return hdr_len + es_len;
}
