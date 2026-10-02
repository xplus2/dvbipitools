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

#endif
