/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/pmtbuild.h"
#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"

static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  size_t i;
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

START_TEST(pmtbuild_map_es_picks_video_first_and_drops_unsupported) {
  psi_es_t es[5];
  out_es_t out_es[8];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n, dropped;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;
  es[1].pid = 0x0102;
  es[1].cls = PID_AUDIO;
  es[1].codec = CODEC_AAC;
  snprintf(es[1].lang, sizeof es[1].lang, "deu");
  es[2].pid = 0x0103;
  es[2].cls = PID_SUBTITLE;
  es[2].sub_type = 1;
  es[2].sub_composition_page = 100;
  es[2].sub_ancillary_page = 200;
  snprintf(es[2].lang, sizeof es[2].lang, "deu");
  es[3].pid = 0x0104;
  es[3].cls = PID_DATA; /* unsupported, must be dropped */
  es[4].pid = 0x0105;
  es[4].cls = PID_TELETEXT;
  es[4].ttx_page = 777;
  es[4].ttx_type = 2;
  snprintf(es[4].ttx_lang, sizeof es[4].ttx_lang, "deu");

  n = pmtbuild_map_es(es, 5, TVSTRIP_DATA, 0x0102 /* PCR on the audio pid */, pids.video_pid, pids.es_pid_base, out_es, 8, &pcr_pid, &dropped);

  ck_assert_int_eq(n, 4); /* video + audio + subtitle + teletext, PID_DATA dropped */
  ck_assert_int_eq(dropped, 0); /* unsupported ES isn't a cap drop */
  ck_assert_uint_eq(out_es[0].out_pid, pids.video_pid);
  ck_assert_uint_eq(out_es[0].in_pid, 0x0101u);
  ck_assert_uint_eq(out_es[1].out_pid, pids.es_pid_base);
  ck_assert_uint_eq(out_es[1].in_pid, 0x0102u);
  ck_assert_uint_eq(out_es[2].in_pid, 0x0103u);
  ck_assert_uint_eq(out_es[3].in_pid, 0x0105u);
  ck_assert_uint_eq(pcr_pid, out_es[1].out_pid); /* reassigned to match src_pcr_pid, not the default video pid */
}
END_TEST

START_TEST(pmtbuild_map_es_defaults_pcr_to_first_es_when_no_match) {
  psi_es_t es[1];
  out_es_t out_es[4];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n, dropped;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;

  n = pmtbuild_map_es(es, 1, 0, 0x9999 /* no ES has this pid */, pids.video_pid, pids.es_pid_base, out_es, 4, &pcr_pid, &dropped);
  ck_assert_int_eq(n, 1);
  ck_assert_int_eq(dropped, 0);
  ck_assert_uint_eq(pcr_pid, out_es[0].out_pid);
}
END_TEST

START_TEST(pmtbuild_map_es_reports_dropped_beyond_cap) {
  psi_es_t es[4];
  out_es_t out_es[2];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n, dropped, i;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;
  for (i = 1; i < 4; i++) {
    es[i].pid = (unsigned)(0x0101 + i);
    es[i].cls = PID_AUDIO;
    es[i].codec = CODEC_AAC;
  }

  n = pmtbuild_map_es(es, 4, 0, es[0].pid, pids.video_pid, pids.es_pid_base, out_es, 2, &pcr_pid, &dropped);

  ck_assert_int_eq(n, 2); /* video + one audio, cap is 2 */
  ck_assert_int_eq(dropped, 2); /* the other two audio ES */
}
END_TEST

START_TEST(pmtbuild_pmt_round_trips_video_audio_subtitle_teletext) {
  psi_es_t es[4];
  out_es_t out_es[8];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n, dropped;
  unsigned char section[512], pkt[188], pat_section[32];
  size_t slen, pat_len;
  psi_t *p;
  const psi_es_t *dec;
  int count;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;
  es[1].pid = 0x0102;
  es[1].cls = PID_AUDIO;
  es[1].codec = CODEC_AAC;
  snprintf(es[1].lang, sizeof es[1].lang, "deu");
  es[2].pid = 0x0103;
  es[2].cls = PID_SUBTITLE;
  es[2].sub_type = 1;
  es[2].sub_composition_page = 100;
  es[2].sub_ancillary_page = 200;
  es[3].pid = 0x0104;
  es[3].cls = PID_TELETEXT;
  es[3].ttx_page = 777;
  es[3].ttx_type = 2;
  snprintf(es[3].ttx_lang, sizeof es[3].ttx_lang, "deu");

  n = pmtbuild_map_es(es, 4, 0, 0x0101, pids.video_pid, pids.es_pid_base, out_es, 8, &pcr_pid, &dropped);
  ck_assert_int_eq(n, 4);
  ck_assert_int_eq(dropped, 0);

  slen = pmtbuild_pmt(1, 55, pcr_pid, NULL, 0, out_es, n, NULL, 0, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  p = psi_new();
  pat_len = psi_build_pat(0x1234, 0, 55, pids.pmt_pid, pat_section, sizeof pat_section);
  wrap_ts_packet(pkt, OUT_PID_PAT, pat_section, pat_len);
  psi_feed(p, pkt);
  wrap_ts_packet(pkt, pids.pmt_pid, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pmt(p), 1);
  ck_assert_uint_eq(psi_pcr_pid(p), pcr_pid);
  dec = psi_es(p, &count);
  ck_assert_int_eq(count, 4);

  ck_assert_uint_eq(dec[0].pid, pids.video_pid);
  ck_assert_int_eq(dec[0].cls, PID_VIDEO);
  ck_assert_int_eq(dec[0].codec, CODEC_H264);

  ck_assert_int_eq(dec[1].cls, PID_AUDIO);
  ck_assert_int_eq(dec[1].codec, CODEC_AAC);
  ck_assert_str_eq(dec[1].lang, "deu");

  ck_assert_int_eq(dec[2].cls, PID_SUBTITLE);
  ck_assert_uint_eq(dec[2].sub_type, 1u);
  ck_assert_uint_eq(dec[2].sub_composition_page, 100u);
  ck_assert_uint_eq(dec[2].sub_ancillary_page, 200u);

  ck_assert_int_eq(dec[3].cls, PID_TELETEXT);
  ck_assert_uint_eq(dec[3].ttx_page, 777u);
  ck_assert_int_eq(dec[3].ttx_type, 2);
  ck_assert_str_eq(dec[3].ttx_lang, "deu");

  psi_free(p);
}
END_TEST

START_TEST(pmtbuild_pmt_rejects_small_cap) {
  out_es_t es[1];
  unsigned char out[8];
  memset(es, 0, sizeof es);
  ck_assert_uint_eq(pmtbuild_pmt(0, 1, 0x100, NULL, 0, es, 0, NULL, 0, out, sizeof out), 0u);
}
END_TEST

START_TEST(pmtbuild_pmt_places_prog_desc_in_program_info) {
  static const unsigned char ca_desc[] = {0x09, 4, 0x4A, 0x75, 0xE0, 0x20};
  psi_es_t src;
  out_es_t es[1];
  out_program_pids_t pids;
  unsigned char out[64];
  size_t slen;

  out_program_pids(0, &pids);
  memset(&src, 0, sizeof src);
  src.cls = PID_VIDEO;
  memset(es, 0, sizeof es);
  es[0].out_pid = pids.video_pid;
  es[0].stream_type = 0x1B;
  es[0].src = &src;

  slen = pmtbuild_pmt(0, 1, pids.video_pid, ca_desc, sizeof ca_desc, es, 1, NULL, 0, out, sizeof out);
  ck_assert_uint_ne(slen, 0u);

  ck_assert_uint_eq(out[10], (unsigned char)(0xF0 | ((sizeof ca_desc >> 8) & 0x0F)));
  ck_assert_uint_eq(out[11], (unsigned char)sizeof ca_desc);
  ck_assert_mem_eq(out + 12, ca_desc, sizeof ca_desc);
  ck_assert_uint_eq(out[12 + sizeof ca_desc], 0x1B); /* ES loop starts right after program_info */
}
END_TEST

static Suite *pmtbuild_suite(void) {
  Suite *s = suite_create("pmtbuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pmtbuild_map_es_picks_video_first_and_drops_unsupported);
  tcase_add_test(tc, pmtbuild_map_es_defaults_pcr_to_first_es_when_no_match);
  tcase_add_test(tc, pmtbuild_map_es_reports_dropped_beyond_cap);
  tcase_add_test(tc, pmtbuild_pmt_round_trips_video_audio_subtitle_teletext);
  tcase_add_test(tc, pmtbuild_pmt_rejects_small_cap);
  tcase_add_test(tc, pmtbuild_pmt_places_prog_desc_in_program_info);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pmtbuild_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
