/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi/psi.h"

/* builds a one-program PAT section (table_id 0x00), CRC included, returns length */
static size_t build_pat(unsigned char *out, unsigned tsid, unsigned prog_num, unsigned pmt_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(tsid >> 8);
  body[n++] = (unsigned char)tsid;
  body[n++] = 0xC1; /* reserved + version 0 + current_next_indicator */
  body[n++] = 0x00; /* section_number */
  body[n++] = 0x00; /* last_section_number */
  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = (unsigned char)(0xE0 | ((pmt_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt_pid;

  hdr = n + 4; /* + CRC */
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

/* builds a two-program PAT section, CRC included, returns length */
static size_t build_pat2(unsigned char *out, unsigned tsid, unsigned prog1, unsigned pmt1, unsigned prog2, unsigned pmt2) {
  unsigned char body[24];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(tsid >> 8);
  body[n++] = (unsigned char)tsid;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(prog1 >> 8);
  body[n++] = (unsigned char)prog1;
  body[n++] = (unsigned char)(0xE0 | ((pmt1 >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt1;
  body[n++] = (unsigned char)(prog2 >> 8);
  body[n++] = (unsigned char)prog2;
  body[n++] = (unsigned char)(0xE0 | ((pmt2 >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt2;

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

/* builds a two-service SDT-actual section (table_id 0x42), each entry with
 * a service_descriptor (0x48), CRC included, returns length */
static size_t build_sdt2(unsigned char *out, unsigned onid, unsigned sid1, const char *name1, unsigned sid2, const char *name2) {
  unsigned char body[96];
  size_t n = 0, hdr, crc_at, dlen_pos, i;
  uint32_t crc;

  body[n++] = 0x00; /* transport_stream_id, unused by parse_sdt() */
  body[n++] = 0x00;
  body[n++] = 0xC1; /* version + current_next_indicator */
  body[n++] = 0x00; /* section_number */
  body[n++] = 0x00; /* last_section_number */
  body[n++] = (unsigned char)(onid >> 8);
  body[n++] = (unsigned char)onid;
  body[n++] = 0xFF; /* reserved_future_use */

  for (i = 0; i < 2; i++) {
    unsigned sid = i == 0 ? sid1 : sid2;
    const char *name = i == 0 ? name1 : name2;
    size_t nlen = strlen(name);
    body[n++] = (unsigned char)(sid >> 8);
    body[n++] = (unsigned char)sid;
    body[n++] = 0xFD;
    dlen_pos = n; /* descriptors_loop_length, filled below */
    n += 2;
    body[n++] = 0x48; /* service_descriptor */
    body[n++] = (unsigned char)(3 + nlen); /* descriptor_length: type+provider_len+name_len+name */
    body[n++] = 0x01; /* service_type */
    body[n++] = 0x00; /* provider_name_length */
    body[n++] = (unsigned char)nlen; /* service_name_length */
    memcpy(body + n, name, nlen);
    n += nlen;
    body[dlen_pos] = (unsigned char)(0xF0 | (((n - dlen_pos - 2) >> 8) & 0x0F));
    body[dlen_pos + 1] = (unsigned char)(n - dlen_pos - 2);
  }

  hdr = n + 4;
  out[0] = 0x42;
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

/* builds a one-program, two-ES PMT section (table_id 0x02), no descriptors,
 * CRC included, returns length */
static size_t build_pmt(unsigned char *out, unsigned prog_num, unsigned pcr_pid,
                         unsigned video_pid, unsigned video_type,
                         unsigned audio_pid, unsigned audio_type) {
  unsigned char body[32];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pcr_pid;
  body[n++] = 0xF0; /* program_info_length = 0 */
  body[n++] = 0x00;
  body[n++] = (unsigned char)video_type;
  body[n++] = (unsigned char)(0xE0 | ((video_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)video_pid;
  body[n++] = 0xF0; /* ES_info_length = 0 */
  body[n++] = 0x00;
  body[n++] = (unsigned char)audio_type;
  body[n++] = (unsigned char)(0xE0 | ((audio_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)audio_pid;
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

/* builds a one-program, zero-ES PMT section with a scrambling_descriptor (tag 0x65,
 * one mode byte) in program_info, CRC included, returns length. mode == 0: omit the
 * descriptor entirely (program_info_length = 0) */
static size_t build_pmt_with_scrambling(unsigned char *out, unsigned prog_num, unsigned pcr_pid, unsigned char mode) {
  unsigned char body[32];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pcr_pid;
  if (mode) {
    body[n++] = 0xF0; /* program_info_length hi nibble + top bits */
    body[n++] = 0x03;
    body[n++] = 0x65; /* scrambling_descriptor tag */
    body[n++] = 0x01; /* descriptor_length */
    body[n++] = mode;
  } else {
    body[n++] = 0xF0; /* program_info_length = 0 */
    body[n++] = 0x00;
  }

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

/* one-program, zero-ES PMT with a CA_descriptor (tag 0x09) in program_info instead of a
 * scrambling_descriptor, CRC included, returns length */
static size_t build_pmt_with_ca(unsigned char *out, unsigned prog_num, unsigned pcr_pid, unsigned ca_system_id, unsigned ca_pid) {
  unsigned char body[32];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pcr_pid;
  body[n++] = 0xF0;
  body[n++] = 0x06;
  body[n++] = 0x09; /* CA_descriptor tag */
  body[n++] = 0x04;
  body[n++] = (unsigned char)(ca_system_id >> 8);
  body[n++] = (unsigned char)ca_system_id;
  body[n++] = (unsigned char)(0xE0 | ((ca_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)ca_pid;

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

/* builds a CAT section (table_id 0x01) with one CA_descriptor (tag 0x09,
 * ca_system_id + ca_pid, no private data), CRC included, returns length */
static size_t build_cat(unsigned char *out, unsigned ca_system_id, unsigned emm_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = 0xFF; /* reserved */
  body[n++] = 0xFF; /* reserved */
  body[n++] = 0xC1; /* reserved + version 0 + current_next_indicator */
  body[n++] = 0x00; /* section_number */
  body[n++] = 0x00; /* last_section_number */
  body[n++] = 0x09; /* CA_descriptor tag */
  body[n++] = 0x04; /* descriptor_length */
  body[n++] = (unsigned char)(ca_system_id >> 8);
  body[n++] = (unsigned char)ca_system_id;
  body[n++] = (unsigned char)(0xE0 | ((emm_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)emm_pid;

  hdr = n + 4;
  out[0] = 0x01;
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

/* builds a CAT section with no descriptors at all, CRC included, returns length */
static size_t build_cat_empty(unsigned char *out) {
  unsigned char body[8];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = 0xFF;
  body[n++] = 0xFF;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;

  hdr = n + 4;
  out[0] = 0x01;
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

/* wraps one PSI section (pusi=1, pointer_field=0) into a single 188-byte TS packet */
static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, unsigned char cc,
                            const unsigned char *section, size_t slen) {
  size_t i;
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x10 | (cc & 0x0F)); /* adaptation_field_control = payload only */
  pkt[4] = 0x00;                                /* pointer_field */
  memcpy(pkt + 5, section, slen);
  for (i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

START_TEST(psi_parses_pat_and_pmt) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen;
  int count;
  const psi_program_t *progs;
  const psi_es_t *es;

  slen = build_pat(section, 0x1234, 1, 0x0100);
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pat(p), 1);
  progs = psi_pat_programs(p, &count);
  ck_assert_int_eq(count, 1);
  ck_assert_uint_eq(progs[0].program_number, 1u);
  ck_assert_uint_eq(progs[0].pmt_pid, 0x0100u);

  slen = build_pmt(section, 1, 0x0101, 0x0101, 0x1B, 0x0102, 0x0F);
  wrap_ts_packet(pkt, 0x0100, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_ready(p), 1);
  ck_assert_uint_eq(psi_program_number(p), 1u);
  ck_assert_uint_eq(psi_pmt_pid(p), 0x0100u);
  ck_assert_uint_eq(psi_pcr_pid(p), 0x0101u);

  es = psi_es(p, &count);
  ck_assert_int_eq(count, 2);
  ck_assert_uint_eq(es[0].pid, 0x0101u);
  ck_assert_int_eq(es[0].cls, PID_VIDEO);
  ck_assert_int_eq(es[0].codec, CODEC_H264);
  ck_assert_uint_eq(es[1].pid, 0x0102u);
  ck_assert_int_eq(es[1].cls, PID_AUDIO);
  ck_assert_int_eq(es[1].codec, CODEC_AAC);
  ck_assert_int_eq(psi_audio_count(p), 1);

  ck_assert_int_eq(psi_classify(p, 0x0000), PID_PAT);
  ck_assert_int_eq(psi_classify(p, 0x0100), PID_PMT);
  ck_assert_int_eq(psi_classify(p, 0x0101), PID_VIDEO);
  ck_assert_int_eq(psi_classify(p, 0x0102), PID_AUDIO);
  ck_assert_int_eq(psi_classify(p, 0x1FFF), PID_NULL);

  psi_free(p);
}
END_TEST

START_TEST(psi_rejects_pat_with_bad_crc) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen = build_pat(section, 0x1234, 1, 0x0100);
  section[slen - 1] ^= 0xFF; /* corrupt the CRC */
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);

  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pat(p), 0);
  psi_free(p);
}
END_TEST

START_TEST(psi_ignores_pointer_field_beyond_payload) {
  psi_t *p = psi_new();
  unsigned char pkt[188];
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x40; /* pusi=1, pid hi bits = 0 */
  pkt[2] = 0x00; /* pid = 0x0000 (PAT) */
  pkt[3] = 0x10; /* adaptation_field_control = payload only */
  pkt[4] = 0xFF; /* pointer_field claims 255 bytes to skip - far beyond the 183 available */

  psi_feed(p, pkt); /* must not read out of bounds, must not crash */

  ck_assert_int_eq(psi_have_pat(p), 0);
  psi_free(p);
}
END_TEST

START_TEST(psi_parses_cat_ca_descriptor) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen;

  slen = build_cat(section, 0x0B75, 0x0021);
  wrap_ts_packet(pkt, 0x0001, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_cat(p), 1);
  ck_assert_uint_eq(psi_ca_system_id(p), 0x0B75u);
  ck_assert_uint_eq(psi_emm_pid(p), 0x0021u);
  ck_assert_int_eq(psi_classify(p, 0x0001), PID_CAT);

  psi_free(p);
}
END_TEST

START_TEST(psi_cat_with_no_ca_descriptor_leaves_emm_pid_zero) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen = build_cat_empty(section);

  wrap_ts_packet(pkt, 0x0001, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_cat(p), 1);
  ck_assert_uint_eq(psi_ca_system_id(p), 0u);
  ck_assert_uint_eq(psi_emm_pid(p), 0u);

  psi_free(p);
}
END_TEST

START_TEST(psi_rejects_cat_with_bad_crc) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen = build_cat(section, 0x0B75, 0x0021);
  section[slen - 1] ^= 0xFF; /* corrupt the CRC */
  wrap_ts_packet(pkt, 0x0001, 0, section, slen);

  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_cat(p), 0);
  ck_assert_uint_eq(psi_emm_pid(p), 0u);
  psi_free(p);
}
END_TEST

START_TEST(psi_parses_pmt_scrambling_descriptor) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen;

  slen = build_pat(section, 0x1234, 1, 0x0100);
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt_with_scrambling(section, 1, 0x0101, 0x02); /* CSA2 */
  wrap_ts_packet(pkt, 0x0100, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_ready(p), 1);
  ck_assert_uint_eq(psi_scrambling_mode(p), 0x02u);

  psi_free(p);
}
END_TEST

START_TEST(psi_parses_pmt_ca_descriptor_system_id) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen;

  slen = build_pat(section, 0x1234, 1, 0x0100);
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt_with_ca(section, 1, 0x0101, 0x2602, 0x1FFF); /* BISS2 Mode 1/E */
  wrap_ts_packet(pkt, 0x0100, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_ready(p), 1);
  ck_assert_uint_eq(psi_pmt_ca_system_id(p), 0x2602u);
  ck_assert_uint_eq(psi_scrambling_mode(p), 0u); /* BISS: no scrambling_descriptor */

  psi_free(p);
}
END_TEST

START_TEST(psi_pmt_with_no_ca_descriptor_leaves_pmt_ca_system_id_zero) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen;

  slen = build_pat(section, 0x1234, 1, 0x0100);
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt_with_scrambling(section, 1, 0x0101, 0x02);
  wrap_ts_packet(pkt, 0x0100, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_ready(p), 1);
  ck_assert_uint_eq(psi_pmt_ca_system_id(p), 0u);

  psi_free(p);
}
END_TEST

START_TEST(psi_pmt_with_no_scrambling_descriptor_leaves_mode_zero) {
  psi_t *p = psi_new();
  unsigned char section[64], pkt[188];
  size_t slen;

  slen = build_pat(section, 0x1234, 1, 0x0100);
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt_with_scrambling(section, 1, 0x0101, 0x00); /* no descriptor */
  wrap_ts_packet(pkt, 0x0100, 0, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_ready(p), 1);
  ck_assert_uint_eq(psi_scrambling_mode(p), 0u);

  psi_free(p);
}
END_TEST

START_TEST(psi_without_multi_mode_locks_first_pmt_only) {
  psi_t *p = psi_new();
  unsigned char section[128], pkt[188];
  size_t slen;

  slen = build_pat2(section, 0x1234, 1, 0x0100, 2, 0x0200);
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt(section, 1, 0x0101, 0x0101, 0x1B, 0x0102, 0x0F);
  wrap_ts_packet(pkt, 0x0100, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt(section, 2, 0x0201, 0x0201, 0x1B, 0x0202, 0x0F);
  wrap_ts_packet(pkt, 0x0200, 0, section, slen);
  psi_feed(p, pkt); /* second candidate's pid: ignored once locked */

  ck_assert_uint_eq(psi_program_number(p), 1u);
  ck_assert_uint_eq(psi_pmt_pid(p), 0x0100u);

  psi_free(p);
}
END_TEST

START_TEST(psi_multi_mode_resolves_every_pmt_and_sdt_name) {
  psi_t *p = psi_new();
  unsigned char section[128], pkt[188];
  size_t slen;
  int count;
  const psi_multi_program_t *m;

  psi_enable_multi_program(p);

  slen = build_pat2(section, 0x1234, 1, 0x0100, 2, 0x0200);
  wrap_ts_packet(pkt, 0x0000, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt(section, 1, 0x0101, 0x0101, 0x1B, 0x0102, 0x0F);
  wrap_ts_packet(pkt, 0x0100, 0, section, slen);
  psi_feed(p, pkt);

  slen = build_pmt(section, 2, 0x0201, 0x0201, 0x1B, 0x0202, 0x0F);
  wrap_ts_packet(pkt, 0x0200, 0, section, slen); /* second candidate still fed and parsed */
  psi_feed(p, pkt);

  m = psi_multi_programs(p, &count);
  ck_assert_int_eq(count, 2);
  ck_assert_uint_eq(m[0].program_number, 1u);
  ck_assert_uint_eq(m[0].pmt_pid, 0x0100u);
  ck_assert_int_eq(m[0].resolved, 1);
  ck_assert_uint_eq(m[1].program_number, 2u);
  ck_assert_uint_eq(m[1].pmt_pid, 0x0200u);
  ck_assert_int_eq(m[1].resolved, 1);
  ck_assert_str_eq(m[0].service_name, ""); /* no SDT fed yet */

  slen = build_sdt2(section, 0x0055, 1, "One", 2, "Two");
  wrap_ts_packet(pkt, 0x0011, 0, section, slen);
  psi_feed(p, pkt);

  m = psi_multi_programs(p, &count);
  ck_assert_str_eq(m[0].service_name, "One");
  ck_assert_str_eq(m[1].service_name, "Two");
  ck_assert_uint_eq(psi_original_network_id(p), 0x0055u);

  psi_free(p);
}
END_TEST

static Suite *psi_suite(void) {
  Suite *s = suite_create("psi");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, psi_parses_pat_and_pmt);
  tcase_add_test(tc, psi_rejects_pat_with_bad_crc);
  tcase_add_test(tc, psi_ignores_pointer_field_beyond_payload);
  tcase_add_test(tc, psi_parses_cat_ca_descriptor);
  tcase_add_test(tc, psi_cat_with_no_ca_descriptor_leaves_emm_pid_zero);
  tcase_add_test(tc, psi_rejects_cat_with_bad_crc);
  tcase_add_test(tc, psi_parses_pmt_scrambling_descriptor);
  tcase_add_test(tc, psi_pmt_with_no_scrambling_descriptor_leaves_mode_zero);
  tcase_add_test(tc, psi_parses_pmt_ca_descriptor_system_id);
  tcase_add_test(tc, psi_pmt_with_no_ca_descriptor_leaves_pmt_ca_system_id_zero);
  tcase_add_test(tc, psi_without_multi_mode_locks_first_pmt_only);
  tcase_add_test(tc, psi_multi_mode_resolves_every_pmt_and_sdt_name);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(psi_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
