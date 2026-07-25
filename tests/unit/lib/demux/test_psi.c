/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi.h"

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

static Suite *psi_suite(void) {
  Suite *s = suite_create("psi");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, psi_parses_pat_and_pmt);
  tcase_add_test(tc, psi_rejects_pat_with_bad_crc);
  tcase_add_test(tc, psi_ignores_pointer_field_beyond_payload);
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
