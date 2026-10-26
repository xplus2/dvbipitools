/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_FX_TS_H
#define DVBIPITOOLS_TESTS_UNIT_FX_TS_H

#include <check.h>
#include "lib/demux/crc32.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void wrap_pes_packet(unsigned char pkt[188], unsigned pid, const unsigned char *payload, size_t plen) {
  unsigned char hdr[9] = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x00};
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  memcpy(pkt + 4, hdr, sizeof hdr);
  memcpy(pkt + 4 + sizeof hdr, payload, plen);
  for (size_t i = 4 + sizeof hdr + plen; i < 188; i++) pkt[i] = 0xFF;
}

static inline void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

static inline size_t build_pat(unsigned char *out, unsigned pmt_pid) {
  unsigned char body[16];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;
  body[n++] = 0x00;
  body[n++] = 0x01;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0x01;
  body[n++] = (unsigned char)(0xE0 | ((pmt_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt_pid;
  hdr = n + 4;
  out[0] = 0x00;
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

static inline size_t build_pmt(unsigned char *out, unsigned prog_num, unsigned pcr_pid) {
  unsigned char body[16];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pcr_pid;
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

static inline size_t build_pmt_aac(unsigned char *out, unsigned prog_num, unsigned pmt_pid) {
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
  body[n++] = 0x0F; /* AAC */
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

static inline void make_packet(unsigned char pkt[188], unsigned char marker) {
  memset(pkt, 0xAB, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = marker;
  pkt[3] = 0x10;
}

static inline void make_pkt(unsigned char pkt[188], unsigned ctrl, unsigned afc, unsigned af_len) {
  memset(pkt, 0xAA, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x01;
  pkt[2] = 0x00;
  pkt[3] = (unsigned char)((ctrl << 6) | (afc << 4));
  pkt[4] = (unsigned char)af_len;
}

#endif
