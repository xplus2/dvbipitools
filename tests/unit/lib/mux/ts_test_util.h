/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_LIB_MUX_TS_TEST_UTIL_H
#define DVBIPITOOLS_TESTS_UNIT_LIB_MUX_TS_TEST_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"

#define DISCOVERY_PACKETS 3
#define EDGE_MAX_PACKETS 3
#define EDGE_MARKER_LEN 20

static inline void wrap_ts_packet(unsigned char pkt[188], unsigned pid, int pusi, const unsigned char *payload, size_t plen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  memcpy(pkt + 4, payload, plen);
  for (size_t i = 4 + plen; i < 188; i++)
    pkt[i] = 0xFF;
}

static inline void wrap_ts_packet_exact(unsigned char pkt[188], unsigned pid, int pusi, const unsigned char *payload, size_t plen) {
  size_t af_len = 183 - plen;

  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x30;
  pkt[4] = (unsigned char)af_len;
  if (af_len) {
    pkt[5] = 0x00;
    memset(pkt + 6, 0xFF, af_len - 1);
  }
  memcpy(pkt + 5 + af_len, payload, plen);
}

static inline void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  unsigned char payload[184];

  payload[0] = 0x00;
  memcpy(payload + 1, section, slen);
  wrap_ts_packet(pkt, pid, 1, payload, slen + 1);
}

static inline size_t build_adts_frame(unsigned char *out, size_t total_len) {
  out[0] = 0xFF;
  out[1] = 0xF1;
  out[2] = (unsigned char)(0x40 | (4 << 2));
  out[3] = (unsigned char)((total_len >> 11) & 0x03);
  out[4] = (unsigned char)((total_len >> 3) & 0xFF);
  out[5] = (unsigned char)((total_len & 0x07) << 5);
  out[6] = 0x00;
  for (size_t i = 7; i < total_len; i++)
    out[i] = 0xAB;
  return total_len;
}

static inline size_t build_pes_with_pts(unsigned char *out, uint64_t pts_90k, const unsigned char *payload, size_t plen) {
  size_t n = 0;

  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0x01;
  out[n++] = 0xC0;
  out[n++] = (unsigned char)((8 + plen) >> 8);
  out[n++] = (unsigned char)(8 + plen);
  out[n++] = 0x80;
  out[n++] = 0x80;
  out[n++] = 0x05;
  out[n++] = (unsigned char)(0x21 | ((pts_90k >> 29) & 0x0E));
  out[n++] = (unsigned char)(pts_90k >> 22);
  out[n++] = (unsigned char)(((pts_90k >> 14) & 0xFE) | 0x01);
  out[n++] = (unsigned char)(pts_90k >> 7);
  out[n++] = (unsigned char)(((pts_90k << 1) & 0xFE) | 0x01);
  memcpy(out + n, payload, plen);
  n += plen;
  return n;
}

static inline void build_aac_discovery(unsigned char pkts[DISCOVERY_PACKETS][188]) {
  unsigned char sec[256];
  unsigned char body[32];
  size_t slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;

  wrap_section_packet(pkts[0], 0x0000, sec, slen);

  body[n++] = (unsigned char)(101 >> 8);
  body[n++] = (unsigned char)101;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
  body[n++] = 0x01;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x0F;
  body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
  body[n++] = 0x01;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  hdr = n + 4;
  sec[0] = 0x02;
  sec[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  sec[2] = (unsigned char)hdr;
  memcpy(sec + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(sec, crc_at);
  sec[crc_at + 0] = (unsigned char)(crc >> 24);
  sec[crc_at + 1] = (unsigned char)(crc >> 16);
  sec[crc_at + 2] = (unsigned char)(crc >> 8);
  sec[crc_at + 3] = (unsigned char)crc;
  wrap_section_packet(pkts[1], 0x0100, sec, crc_at + 4);

  slen = psi_build_sdt(0, 0x1234, 2, 101, 0x01, "Provider", "Service", sec, sizeof sec);
  wrap_section_packet(pkts[2], 0x0011, sec, slen);
}

typedef enum {
  EDGE_SINGLE_FRAME,
  EDGE_EMPTY_PES_THEN_FRAME,
  EDGE_GARBAGE_ES,
  EDGE_TRUNCATED_FRAME,
  EDGE_COUNT
} edge_case_t;

static const int edge_expects_frame[EDGE_COUNT] = {1, 1, 0, 0};

static inline size_t build_edge_packets(edge_case_t e, unsigned char pkts[EDGE_MAX_PACKETS][188]) {
  unsigned char es[64] = {0};
  unsigned char pes[128];
  size_t n = 0;
  size_t plen;

  switch (e) {
    case EDGE_SINGLE_FRAME:
      plen = build_pes_with_pts(pes, 90000, es, build_adts_frame(es, 50));
      wrap_ts_packet_exact(pkts[n++], 0x0101, 1, pes, plen);
      break;
    case EDGE_EMPTY_PES_THEN_FRAME:
      plen = build_pes_with_pts(pes, 87000, es, 0);
      wrap_ts_packet_exact(pkts[n++], 0x0101, 1, pes, plen);
      plen = build_pes_with_pts(pes, 90000, es, build_adts_frame(es, 50));
      wrap_ts_packet_exact(pkts[n++], 0x0101, 1, pes, plen);
      break;
    case EDGE_GARBAGE_ES:
      memset(es, 0x5A, 50);
      plen = build_pes_with_pts(pes, 90000, es, 50);
      wrap_ts_packet_exact(pkts[n++], 0x0101, 1, pes, plen);
      break;
    default:
      build_adts_frame(es, 50);
      plen = build_pes_with_pts(pes, 90000, es, 30);
      wrap_ts_packet_exact(pkts[n++], 0x0101, 1, pes, plen);
      break;
  }
  return n;
}

static inline int buffer_has_frame_marker(const unsigned char *buf, size_t len) {
  unsigned char marker[EDGE_MARKER_LEN];

  memset(marker, 0xAB, sizeof marker);
  for (size_t i = 0; i + sizeof marker <= len; i++)
    if (memcmp(buf + i, marker, sizeof marker) == 0) return 1;
  return 0;
}

static inline unsigned char *slurp_file(const char *path, size_t *len_out) {
  FILE *f = fopen(path, "rb");
  unsigned char *buf;
  long size;

  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  rewind(f);
  buf = malloc(size > 0 ? (size_t)size : 1);
  if (!buf || (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size)) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *len_out = size > 0 ? (size_t)size : 0;
  return buf;
}

static inline size_t build_pes_with_pts_dts(unsigned char *out, uint64_t pts_90k, uint64_t dts_90k, const unsigned char *payload, size_t plen) {
  size_t n = 0;

  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0x01;
  out[n++] = 0xE0;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0x80;
  out[n++] = 0xC0;
  out[n++] = 0x0A;
  out[n++] = (unsigned char)(0x31 | ((pts_90k >> 29) & 0x0E));
  out[n++] = (unsigned char)(pts_90k >> 22);
  out[n++] = (unsigned char)(((pts_90k >> 14) & 0xFE) | 0x01);
  out[n++] = (unsigned char)(pts_90k >> 7);
  out[n++] = (unsigned char)(((pts_90k << 1) & 0xFE) | 0x01);
  out[n++] = (unsigned char)(0x11 | ((dts_90k >> 29) & 0x0E));
  out[n++] = (unsigned char)(dts_90k >> 22);
  out[n++] = (unsigned char)(((dts_90k >> 14) & 0xFE) | 0x01);
  out[n++] = (unsigned char)(dts_90k >> 7);
  out[n++] = (unsigned char)(((dts_90k << 1) & 0xFE) | 0x01);
  memcpy(out + n, payload, plen);
  n += plen;
  out[4] = (unsigned char)((n - 6) >> 8);
  out[5] = (unsigned char)(n - 6);
  return n;
}

static inline size_t build_pmt_video(unsigned char *out, unsigned prog_num, unsigned pmt_pid, unsigned char stream_type) {
  unsigned char body[16];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;
  unsigned es_pid = pmt_pid + 1;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((es_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)es_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = stream_type;
  body[n++] = (unsigned char)(0xE0 | ((es_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)es_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  hdr = n + 4;
  out[0] = 0x02;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

static inline size_t build_pmt_av1_video(unsigned char *out, unsigned prog_num, unsigned pmt_pid) {
  unsigned char body[20];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;
  unsigned es_pid = pmt_pid + 1;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((es_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)es_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x06;
  body[n++] = (unsigned char)(0xE0 | ((es_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)es_pid;
  body[n++] = 0xF0;
  body[n++] = 0x06;
  body[n++] = 0x05;
  body[n++] = 0x04;
  memcpy(body + n, "AV01", 4);
  n += 4;
  hdr = n + 4;
  out[0] = 0x02;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

static inline size_t build_vvc_au(unsigned char *out) {
  static const unsigned char vps[] = {0x00, 0x71, 0xAA, 0xBB};
  static const unsigned char sps[] = {0x00, 0x79, 0x11, 0x0B, 0xFF, 0xFF, 0xDF, 0x00, 0x12};
  static const unsigned char pps[] = {0x00, 0x81, 0xCC, 0xDD};
  static const unsigned char idr[] = {0x00, 0x39, 0xEE};
  static const unsigned char sc[] = {0x00, 0x00, 0x01};
  size_t n = 0;
#define APPEND(a) memcpy(out + n, a, sizeof a); n += sizeof a
  APPEND(sc); APPEND(vps);
  APPEND(sc); APPEND(sps);
  APPEND(sc); APPEND(pps);
  APPEND(sc); APPEND(idr);
#undef APPEND
  return n;
}

static inline size_t build_av1_au(unsigned char *out) {
  static const unsigned char seqhdr[] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x01};
  static const unsigned char frame[] = {0x30, 0x00, 0xAB, 0xCD};
  static const unsigned char sc[] = {0x00, 0x00, 0x01};
  size_t n = 0;
#define APPEND(a) memcpy(out + n, a, sizeof a); n += sizeof a
  APPEND(sc); APPEND(seqhdr);
  APPEND(sc); APPEND(frame);
#undef APPEND
  return n;
}

static inline size_t build_annexb_au(unsigned char *out, const unsigned char *const *nals, const size_t *lens, unsigned count) {
  static const unsigned char sc[] = {0x00, 0x00, 0x01};
  size_t n = 0;

  for (unsigned i = 0; i < count; i++) {
    memcpy(out + n, sc, sizeof sc);
    n += sizeof sc;
    memcpy(out + n, nals[i], lens[i]);
    n += lens[i];
  }
  return n;
}

static const unsigned char h264_sps_1080p[] = {0x67, 0x64, 0x00, 0x28, 0xAC, 0xD9, 0x40, 0x78, 0x02, 0x27, 0xE5, 0xC0, 0x44, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xF0, 0x3C, 0x60, 0xC6, 0x58};
static const unsigned char h264_pps[] = {0x68, 0xEB, 0xE3, 0xCB, 0x22, 0xC0};
static const unsigned char hevc_vps[] = {0x40, 0x01, 0x0C, 0x11, 0x22};
static const unsigned char hevc_sps_1080p[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x10, 0x20, 0x30, 0x90, 0x11, 0x22, 0x33, 0x44, 0x55, 0x78, 0xA0, 0x03, 0xC0, 0x80, 0x11, 0x07, 0xCB};
static const unsigned char hevc_pps[] = {0x44, 0x01, 0xC1, 0x72};

/* slice NAL: header bytes then EDGE_MARKER_LEN marker bytes, found again in the muxed output */
static inline size_t build_marked_nal(unsigned char *out, const unsigned char *hdr, size_t hdr_len) {
  memcpy(out, hdr, hdr_len);
  memset(out + hdr_len, 0xAB, EDGE_MARKER_LEN);
  return hdr_len + EDGE_MARKER_LEN;
}

static inline size_t build_h264_au(unsigned char *out, int idr) {
  static const unsigned char idr_hdr[] = {0x65, 0x88, 0x84};
  static const unsigned char p_hdr[] = {0x41, 0x9A};
  unsigned char slice[32];
  size_t slen = idr ? build_marked_nal(slice, idr_hdr, sizeof idr_hdr) : build_marked_nal(slice, p_hdr, sizeof p_hdr);
  const unsigned char *idr_nals[3] = {h264_sps_1080p, h264_pps, slice};
  size_t idr_lens[3] = {sizeof h264_sps_1080p, sizeof h264_pps, slen};

  if (idr) return build_annexb_au(out, idr_nals, idr_lens, 3);
  return build_annexb_au(out, idr_nals + 2, idr_lens + 2, 1);
}

static inline size_t build_hevc_au(unsigned char *out, int idr) {
  static const unsigned char idr_hdr[] = {0x26, 0x01, 0xAF};
  static const unsigned char trail_hdr[] = {0x02, 0x01, 0xD0};
  unsigned char slice[32];
  size_t slen = idr ? build_marked_nal(slice, idr_hdr, sizeof idr_hdr) : build_marked_nal(slice, trail_hdr, sizeof trail_hdr);
  const unsigned char *nals[4] = {hevc_vps, hevc_sps_1080p, hevc_pps, slice};
  size_t lens[4] = {sizeof hevc_vps, sizeof hevc_sps_1080p, sizeof hevc_pps, slen};

  if (idr) return build_annexb_au(out, nals, lens, 4);
  return build_annexb_au(out, nals + 3, lens + 3, 1);
}

/* sequence header 1920x1080 + I picture, or P picture */
static inline size_t build_mpeg2_au(unsigned char *out, int intra) {
  static const unsigned char seq[] = {0x00, 0x00, 0x01, 0xB3, 0x78, 0x04, 0x38, 0x13, 0xFF, 0xFF, 0xE0, 0x18};
  size_t n = 0;

  if (intra) {
    memcpy(out, seq, sizeof seq);
    n = sizeof seq;
  }
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0x01;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = intra ? 0x08 : 0x10;
  memset(out + n, 0xAB, EDGE_MARKER_LEN);
  return n + EDGE_MARKER_LEN;
}

static inline void build_video_discovery(unsigned char pkts[DISCOVERY_PACKETS][188], int av1, unsigned char stream_type) {
  unsigned char sec[256];
  size_t slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkts[0], 0x0000, sec, slen);
  slen = av1 ? build_pmt_av1_video(sec, 101, 0x0100) : build_pmt_video(sec, 101, 0x0100, stream_type);
  wrap_section_packet(pkts[1], 0x0100, sec, slen);
  slen = psi_build_sdt(0, 0x1234, 2, 101, 0x01, "Provider", "Service", sec, sizeof sec);
  wrap_section_packet(pkts[2], 0x0011, sec, slen);
}


static inline unsigned count_frame_markers(const unsigned char *buf, size_t len) {
  unsigned char marker[EDGE_MARKER_LEN];
  unsigned n = 0;
  memset(marker, 0xAB, sizeof marker);
  for (size_t i = 0; i + sizeof marker <= len; i++) {
    if (memcmp(buf + i, marker, sizeof marker) == 0) {
      n++;
      i += sizeof marker - 1;
    }
  }
  return n;
}

#endif
