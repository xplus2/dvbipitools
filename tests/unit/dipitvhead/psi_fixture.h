/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_TESTS_PSI_FIXTURE_H
#define DIPITVHEAD_TESTS_PSI_FIXTURE_H

#include <stdint.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi/psi.h"
#include "lib/mux/psi_build.h"

static inline void fixture_wrap_section(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++) pkt[i] = 0xFF;
}

static inline void fixture_programme_packets_es(unsigned program, unsigned pcr_pid, const unsigned (*es)[2], unsigned n_es, unsigned char pkts[2][188]) {
  unsigned char section[128];
  unsigned char body[64];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;
  size_t slen = psi_build_pat(0x1234, 0, program, 0x0100, section, sizeof section);

  fixture_wrap_section(pkts[0], 0x0000, section, slen);
  body[n++] = (unsigned char)(program >> 8);
  body[n++] = (unsigned char)program;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pcr_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  for (unsigned i = 0; i < n_es; i++) {
    body[n++] = (unsigned char)es[i][0];
    body[n++] = (unsigned char)(0xE0 | ((es[i][1] >> 8) & 0x1F));
    body[n++] = (unsigned char)es[i][1];
    body[n++] = 0xF0;
    body[n++] = 0x00;
  }
  hdr = n + 4;
  section[0] = 0x02;
  section[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  section[2] = (unsigned char)hdr;
  memcpy(section + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(section, crc_at);
  section[crc_at + 0] = (unsigned char)(crc >> 24);
  section[crc_at + 1] = (unsigned char)(crc >> 16);
  section[crc_at + 2] = (unsigned char)(crc >> 8);
  section[crc_at + 3] = (unsigned char)crc;
  fixture_wrap_section(pkts[1], 0x0100, section, crc_at + 4);
}

static inline void fixture_programme_packets(unsigned program, unsigned pcr_pid, unsigned stream_type, unsigned es_pid, unsigned char pkts[2][188]) {
  const unsigned es[1][2] = {{stream_type, es_pid}};
  fixture_programme_packets_es(program, pcr_pid, es, 1, pkts);
}

static inline void fixture_pat_pmt_packets(unsigned pcr_pid, unsigned char pkts[2][188]) {
  fixture_programme_packets(101, pcr_pid, 0x1B, 0x0101, pkts);
}

static inline psi_t *build_psi_with_program(unsigned program, unsigned pcr_pid, unsigned stream_type, unsigned es_pid) {
  unsigned char pkts[2][188];
  psi_t *psi = psi_new();

  fixture_programme_packets(program, pcr_pid, stream_type, es_pid, pkts);
  psi_feed(psi, pkts[0]);
  psi_feed(psi, pkts[1]);
  return psi;
}

static inline psi_t *build_psi_with_pcr(unsigned pcr_pid) {
  return build_psi_with_program(101, pcr_pid, 0x1B, 0x0101);
}

static inline void fixture_ts_packet(unsigned char pkt[188], unsigned pid, unsigned char cc, unsigned char fill) {
  memset(pkt, fill, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x10 | (cc & 0x0F));
}

static inline size_t fixture_eit_packets(unsigned service_id, unsigned char section_number, size_t body_len, unsigned char (*pkts)[188], unsigned char *section_out) {
  size_t slen = 3 + body_len;
  size_t used = 0;
  size_t n = 0;

  section_out[0] = 0x4E;
  section_out[1] = (unsigned char)(0xF0 | ((body_len >> 8) & 0x0F));
  section_out[2] = (unsigned char)body_len;
  for (size_t i = 0; i < body_len; i++) section_out[3 + i] = (unsigned char)((0xA0 + i) & 0xFF);
  section_out[3] = (unsigned char)(service_id >> 8);
  section_out[4] = (unsigned char)service_id;
  section_out[6] = section_number;
  while (used < slen) {
    size_t room = n == 0 ? 183 : 184;
    size_t take = slen - used < room ? slen - used : room;
    unsigned char *pkt = pkts[n];

    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47;
    pkt[1] = (unsigned char)(n == 0 ? 0x40 : 0x00);
    pkt[2] = 0x12;
    pkt[3] = 0x10;
    if (n == 0) pkt[4] = 0x00;
    memcpy(pkt + (n == 0 ? 5 : 4), section_out + used, take);
    used += take;
    n++;
  }
  return n;
}

static inline void fixture_cat_packet(const unsigned char *desc, size_t desc_len, unsigned char pkt[188]) {
  unsigned char section[128];
  size_t slen = psi_build_cat(0, desc, desc_len, section, sizeof section);

  fixture_wrap_section(pkt, 0x0001, section, slen);
}

#endif
